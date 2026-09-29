#include "OpenDrive/OpenDriveMap.h"
#include "OpenScenarioModule.h"
#include "OpenScenarioXml.h"
#include "XmlFile.h"
#include "XmlNode.h"
#include "Algo/Reverse.h"

namespace
{
	double AttrD(const FXmlNode* N, const TCHAR* Name, double Def = 0.0)
	{
		FString V;
		return OSCXml::TryAttr(N, Name, V) ? FCString::Atod(*V) : Def;
	}

	FString AttrS(const FXmlNode* N, const TCHAR* Name)
	{
		FString V;
		OSCXml::TryAttr(N, Name, V);
		return V;
	}

	FOpenDriveCubic ParseCubic(const FXmlNode* N, const TCHAR* SAttr, double SBase)
	{
		FOpenDriveCubic C;
		C.S = SBase + AttrD(N, SAttr);
		C.A = AttrD(N, TEXT("a"));
		C.B = AttrD(N, TEXT("b"));
		C.C = AttrD(N, TEXT("c"));
		C.D = AttrD(N, TEXT("d"));
		return C;
	}

	EOpenDriveContactPoint ParseContact(const FString& S)
	{
		if (S.Equals(TEXT("start"), ESearchCase::IgnoreCase)) { return EOpenDriveContactPoint::Start; }
		if (S.Equals(TEXT("end"), ESearchCase::IgnoreCase)) { return EOpenDriveContactPoint::End; }
		return EOpenDriveContactPoint::None;
	}

	void ParseLink(const FXmlNode* LinkNode, const TCHAR* Tag, EOpenDriveElementType& OutType, FString& OutId, EOpenDriveContactPoint& OutContact)
	{
		const FXmlNode* N = OSCXml::Child(LinkNode, Tag);
		if (!N)
		{
			return;
		}
		const FString Type = AttrS(N, TEXT("elementType"));
		OutType = Type.Equals(TEXT("junction"), ESearchCase::IgnoreCase) ? EOpenDriveElementType::Junction : EOpenDriveElementType::Road;
		OutId = AttrS(N, TEXT("elementId"));
		OutContact = ParseContact(AttrS(N, TEXT("contactPoint")));
	}

	/** Position on a single geometry element at local arc length U (0..Length). */
	void EvalGeometry(const FOpenDriveGeometry& G, double U, double& X, double& Y, double& H)
	{
		U = FMath::Clamp(U, 0.0, G.Length);
		const double C0 = FMath::Cos(G.Hdg);
		const double S0 = FMath::Sin(G.Hdg);

		switch (G.Type)
		{
		case EOpenDriveGeometryType::Arc:
		{
			const double K = G.Curvature;
			if (FMath::Abs(K) < 1e-12)
			{
				X = G.X + U * C0;
				Y = G.Y + U * S0;
				H = G.Hdg;
			}
			else
			{
				H = G.Hdg + K * U;
				X = G.X + (FMath::Sin(H) - S0) / K;
				Y = G.Y - (FMath::Cos(H) - C0) / K;
			}
			break;
		}
		case EOpenDriveGeometryType::Spiral:
		{
			const double Dk = G.Length > 1e-9 ? (G.CurvEnd - G.CurvStart) / G.Length : 0.0;
			H = G.Hdg + G.CurvStart * U + 0.5 * Dk * U * U;
			int32 N = FMath::Clamp(FMath::CeilToInt(U / 0.25), 2, 4000);
			if (N & 1)
			{
				++N;
			}
			const double Step = U / N;
			double SumX = 0.0, SumY = 0.0;
			for (int32 i = 0; i <= N; ++i)
			{
				const double u = i * Step;
				const double h = G.Hdg + G.CurvStart * u + 0.5 * Dk * u * u;
				const double W = (i == 0 || i == N) ? 1.0 : ((i & 1) ? 4.0 : 2.0);
				SumX += W * FMath::Cos(h);
				SumY += W * FMath::Sin(h);
			}
			X = G.X + SumX * Step / 3.0;
			Y = G.Y + SumY * Step / 3.0;
			break;
		}
		case EOpenDriveGeometryType::Poly3:
		{
			const double V = G.AV + U * (G.BV + U * (G.CV + U * G.DV));
			const double Dv = G.BV + U * (2.0 * G.CV + U * 3.0 * G.DV);
			X = G.X + U * C0 - V * S0;
			Y = G.Y + U * S0 + V * C0;
			H = G.Hdg + FMath::Atan(Dv);
			break;
		}
		case EOpenDriveGeometryType::ParamPoly3:
		{
			const double P = G.bNormalizedRange ? (G.Length > 1e-9 ? U / G.Length : 0.0) : U;
			const double Lu = G.AU + P * (G.BU + P * (G.CU + P * G.DU));
			const double Lv = G.AV + P * (G.BV + P * (G.CV + P * G.DV));
			const double Du = G.BU + P * (2.0 * G.CU + P * 3.0 * G.DU);
			const double Dv = G.BV + P * (2.0 * G.CV + P * 3.0 * G.DV);
			X = G.X + Lu * C0 - Lv * S0;
			Y = G.Y + Lu * S0 + Lv * C0;
			H = G.Hdg + FMath::Atan2(Dv, Du);
			break;
		}
		case EOpenDriveGeometryType::Line:
		default:
			X = G.X + U * C0;
			Y = G.Y + U * S0;
			H = G.Hdg;
			break;
		}
	}
}

double FOpenDriveCubic::EvalPiecewise(const TArray<FOpenDriveCubic>& Entries, double AbsS)
{
	if (Entries.Num() == 0)
	{
		return 0.0;
	}
	int32 Best = 0;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].S <= AbsS + 1e-9)
		{
			Best = i;
		}
		else
		{
			break;
		}
	}
	return Entries[Best].Eval(AbsS);
}

// ------------------------------------------------------------------------------------------------
// Loading
// ------------------------------------------------------------------------------------------------

bool FOpenDriveMap::LoadFromString(const FString& Xml, FString& OutError)
{
	Roads.Reset();
	RoadIndex.Reset();
	Junctions.Reset();
	JunctionIndex.Reset();
	Name.Reset();

	FXmlFile File(Xml, EConstructMethod::ConstructFromBuffer);
	if (!File.IsValid())
	{
		OutError = File.GetLastError();
		return false;
	}
	const FXmlNode* Root = File.GetRootNode();
	if (!Root || !Root->GetTag().Equals(TEXT("OpenDRIVE"), ESearchCase::CaseSensitive))
	{
		OutError = TEXT("Root element is not <OpenDRIVE>.");
		return false;
	}

	Name = AttrS(OSCXml::Child(Root, TEXT("header")), TEXT("name"));

	for (const FXmlNode* RoadNode : OSCXml::Children(Root, TEXT("road")))
	{
		FOpenDriveRoad Road;
		Road.Id = AttrS(RoadNode, TEXT("id"));
		Road.Name = AttrS(RoadNode, TEXT("name"));
		Road.Length = AttrD(RoadNode, TEXT("length"));
		const FString Junction = AttrS(RoadNode, TEXT("junction"));
		Road.JunctionId = (Junction == TEXT("-1")) ? FString() : Junction;

		if (const FXmlNode* Link = OSCXml::Child(RoadNode, TEXT("link")))
		{
			ParseLink(Link, TEXT("predecessor"), Road.PredecessorType, Road.PredecessorId, Road.PredecessorContact);
			ParseLink(Link, TEXT("successor"), Road.SuccessorType, Road.SuccessorId, Road.SuccessorContact);
		}

		if (const FXmlNode* Plan = OSCXml::Child(RoadNode, TEXT("planView")))
		{
			for (const FXmlNode* GeoNode : OSCXml::Children(Plan, TEXT("geometry")))
			{
				FOpenDriveGeometry G;
				G.S = AttrD(GeoNode, TEXT("s"));
				G.X = AttrD(GeoNode, TEXT("x"));
				G.Y = AttrD(GeoNode, TEXT("y"));
				G.Hdg = AttrD(GeoNode, TEXT("hdg"));
				G.Length = AttrD(GeoNode, TEXT("length"));

				if (const FXmlNode* Arc = OSCXml::Child(GeoNode, TEXT("arc")))
				{
					G.Type = EOpenDriveGeometryType::Arc;
					G.Curvature = AttrD(Arc, TEXT("curvature"));
				}
				else if (const FXmlNode* Spiral = OSCXml::Child(GeoNode, TEXT("spiral")))
				{
					G.Type = EOpenDriveGeometryType::Spiral;
					G.CurvStart = AttrD(Spiral, TEXT("curvStart"));
					G.CurvEnd = AttrD(Spiral, TEXT("curvEnd"));
				}
				else if (const FXmlNode* Poly = OSCXml::Child(GeoNode, TEXT("poly3")))
				{
					G.Type = EOpenDriveGeometryType::Poly3;
					G.AV = AttrD(Poly, TEXT("a"));
					G.BV = AttrD(Poly, TEXT("b"));
					G.CV = AttrD(Poly, TEXT("c"));
					G.DV = AttrD(Poly, TEXT("d"));
				}
				else if (const FXmlNode* PP = OSCXml::Child(GeoNode, TEXT("paramPoly3")))
				{
					G.Type = EOpenDriveGeometryType::ParamPoly3;
					G.AU = AttrD(PP, TEXT("aU"));
					G.BU = AttrD(PP, TEXT("bU"));
					G.CU = AttrD(PP, TEXT("cU"));
					G.DU = AttrD(PP, TEXT("dU"));
					G.AV = AttrD(PP, TEXT("aV"));
					G.BV = AttrD(PP, TEXT("bV"));
					G.CV = AttrD(PP, TEXT("cV"));
					G.DV = AttrD(PP, TEXT("dV"));
					G.bNormalizedRange = AttrS(PP, TEXT("pRange")).Equals(TEXT("normalized"), ESearchCase::IgnoreCase);
				}
				else
				{
					G.Type = EOpenDriveGeometryType::Line;
				}
				Road.Geometry.Add(G);
			}
			Road.Geometry.Sort([](const FOpenDriveGeometry& A, const FOpenDriveGeometry& B) { return A.S < B.S; });
		}

		if (const FXmlNode* Elev = OSCXml::Child(RoadNode, TEXT("elevationProfile")))
		{
			for (const FXmlNode* E : OSCXml::Children(Elev, TEXT("elevation")))
			{
				Road.Elevation.Add(ParseCubic(E, TEXT("s"), 0.0));
			}
			Road.Elevation.Sort([](const FOpenDriveCubic& A, const FOpenDriveCubic& B) { return A.S < B.S; });
		}

		if (const FXmlNode* Lanes = OSCXml::Child(RoadNode, TEXT("lanes")))
		{
			for (const FXmlNode* Off : OSCXml::Children(Lanes, TEXT("laneOffset")))
			{
				Road.LaneOffset.Add(ParseCubic(Off, TEXT("s"), 0.0));
			}
			Road.LaneOffset.Sort([](const FOpenDriveCubic& A, const FOpenDriveCubic& B) { return A.S < B.S; });

			for (const FXmlNode* SecNode : OSCXml::Children(Lanes, TEXT("laneSection")))
			{
				FOpenDriveLaneSection Section;
				Section.S = AttrD(SecNode, TEXT("s"));

				static const TCHAR* const Sides[] = { TEXT("left"), TEXT("right") };
				for (const TCHAR* Side : Sides)
				{
					const FXmlNode* SideNode = OSCXml::Child(SecNode, Side);
					for (const FXmlNode* LaneNode : OSCXml::Children(SideNode, TEXT("lane")))
					{
						FOpenDriveLane Lane;
						Lane.Id = FCString::Atoi(*AttrS(LaneNode, TEXT("id")));
						Lane.Type = AttrS(LaneNode, TEXT("type"));
						if (const FXmlNode* LaneLink = OSCXml::Child(LaneNode, TEXT("link")))
						{
							Lane.Predecessor = FCString::Atoi(*AttrS(OSCXml::Child(LaneLink, TEXT("predecessor")), TEXT("id")));
							Lane.Successor = FCString::Atoi(*AttrS(OSCXml::Child(LaneLink, TEXT("successor")), TEXT("id")));
						}
						for (const FXmlNode* W : OSCXml::Children(LaneNode, TEXT("width")))
						{
							Lane.Widths.Add(ParseCubic(W, TEXT("sOffset"), Section.S));
						}
						Lane.Widths.Sort([](const FOpenDriveCubic& A, const FOpenDriveCubic& B) { return A.S < B.S; });
						if (Lane.Id != 0)
						{
							Section.Lanes.Add(MoveTemp(Lane));
						}
					}
				}
				Section.Lanes.Sort([](const FOpenDriveLane& A, const FOpenDriveLane& B) { return A.Id < B.Id; });
				Road.LaneSections.Add(MoveTemp(Section));
			}
			Road.LaneSections.Sort([](const FOpenDriveLaneSection& A, const FOpenDriveLaneSection& B) { return A.S < B.S; });
			for (int32 i = 0; i < Road.LaneSections.Num(); ++i)
			{
				Road.LaneSections[i].EndS = Road.LaneSections.IsValidIndex(i + 1) ? Road.LaneSections[i + 1].S : Road.Length;
			}
		}

		if (Road.Geometry.Num() == 0)
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("OpenDRIVE road '%s' has no geometry and is ignored."), *Road.Id);
			continue;
		}
		if (Road.Length <= 0.0)
		{
			const FOpenDriveGeometry& Last = Road.Geometry.Last();
			Road.Length = Last.S + Last.Length;
		}
		ComputeRoadBounds(Road);
		RoadIndex.Add(Road.Id, Roads.Num());
		Roads.Add(MoveTemp(Road));
	}

	for (const FXmlNode* JuncNode : OSCXml::Children(Root, TEXT("junction")))
	{
		FOpenDriveJunction Junction;
		Junction.Id = AttrS(JuncNode, TEXT("id"));
		Junction.Name = AttrS(JuncNode, TEXT("name"));
		for (const FXmlNode* ConNode : OSCXml::Children(JuncNode, TEXT("connection")))
		{
			FOpenDriveJunctionConnection Con;
			Con.Id = AttrS(ConNode, TEXT("id"));
			Con.IncomingRoad = AttrS(ConNode, TEXT("incomingRoad"));
			Con.ConnectingRoad = AttrS(ConNode, TEXT("connectingRoad"));
			Con.ContactPoint = ParseContact(AttrS(ConNode, TEXT("contactPoint")));
			if (Con.ContactPoint == EOpenDriveContactPoint::None)
			{
				Con.ContactPoint = EOpenDriveContactPoint::Start;
			}
			for (const FXmlNode* LL : OSCXml::Children(ConNode, TEXT("laneLink")))
			{
				Con.LaneLinks.Emplace(FCString::Atoi(*AttrS(LL, TEXT("from"))), FCString::Atoi(*AttrS(LL, TEXT("to"))));
			}
			Junction.Connections.Add(MoveTemp(Con));
		}
		JunctionIndex.Add(Junction.Id, Junctions.Num());
		Junctions.Add(MoveTemp(Junction));
	}

	if (Roads.Num() == 0)
	{
		OutError = TEXT("OpenDRIVE file contains no usable roads.");
		return false;
	}
	return true;
}

void FOpenDriveMap::ComputeRoadBounds(FOpenDriveRoad& Road) const
{
	bool bFirst = true;
	double MaxExtent = 0.0;
	const int32 N = FMath::Max(1, FMath::CeilToInt(Road.Length / 5.0));
	for (int32 i = 0; i <= N; ++i)
	{
		const double S = FMath::Min(Road.Length, i * 5.0);
		double X, Y, H;
		EvaluateReferenceLine(Road, S, X, Y, H);
		if (bFirst)
		{
			Road.MinX = Road.MaxX = X;
			Road.MinY = Road.MaxY = Y;
			bFirst = false;
		}
		Road.MinX = FMath::Min(Road.MinX, X);
		Road.MaxX = FMath::Max(Road.MaxX, X);
		Road.MinY = FMath::Min(Road.MinY, Y);
		Road.MaxY = FMath::Max(Road.MaxY, Y);
		MaxExtent = FMath::Max(MaxExtent, FMath::Max(FMath::Abs(GetOuterBorderT(Road, S, true)), FMath::Abs(GetOuterBorderT(Road, S, false))));
	}
	Road.MaxExtent = MaxExtent;
}

double FOpenDriveMap::GetTotalLength() const
{
	double Sum = 0.0;
	for (const FOpenDriveRoad& R : Roads)
	{
		Sum += R.Length;
	}
	return Sum;
}

const FOpenDriveRoad* FOpenDriveMap::FindRoad(const FString& RoadId) const
{
	const int32* Idx = RoadIndex.Find(RoadId);
	return Idx ? &Roads[*Idx] : nullptr;
}

const FOpenDriveJunction* FOpenDriveMap::FindJunction(const FString& JunctionId) const
{
	const int32* Idx = JunctionIndex.Find(JunctionId);
	return Idx ? &Junctions[*Idx] : nullptr;
}

// ------------------------------------------------------------------------------------------------
// Geometry
// ------------------------------------------------------------------------------------------------

int32 FOpenDriveMap::FindGeometryIndex(const FOpenDriveRoad& Road, double S) const
{
	int32 Lo = 0;
	int32 Hi = Road.Geometry.Num() - 1;
	while (Lo < Hi)
	{
		const int32 Mid = (Lo + Hi + 1) / 2;
		if (Road.Geometry[Mid].S <= S)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return Lo;
}

void FOpenDriveMap::EvaluateReferenceLine(const FOpenDriveRoad& Road, double S, double& X, double& Y, double& Heading) const
{
	S = FMath::Clamp(S, 0.0, Road.Length);
	const FOpenDriveGeometry& G = Road.Geometry[FindGeometryIndex(Road, S)];
	EvalGeometry(G, S - G.S, X, Y, Heading);
}

FOpenDrivePose FOpenDriveMap::EvaluatePose(const FOpenDriveRoad& Road, double S, double T) const
{
	S = FMath::Clamp(S, 0.0, Road.Length);
	double X, Y, H;
	EvaluateReferenceLine(Road, S, X, Y, H);

	FOpenDrivePose Pose;
	Pose.X = X - T * FMath::Sin(H);
	Pose.Y = Y + T * FMath::Cos(H);
	Pose.Z = FOpenDriveCubic::EvalPiecewise(Road.Elevation, S);
	Pose.Heading = H;
	return Pose;
}

// ------------------------------------------------------------------------------------------------
// Lanes
// ------------------------------------------------------------------------------------------------

const FOpenDriveLaneSection* FOpenDriveMap::FindLaneSection(const FOpenDriveRoad& Road, double S) const
{
	const FOpenDriveLaneSection* Best = nullptr;
	for (const FOpenDriveLaneSection& Sec : Road.LaneSections)
	{
		if (Sec.S <= S + 1e-9)
		{
			Best = &Sec;
		}
		else
		{
			break;
		}
	}
	if (!Best && Road.LaneSections.Num() > 0)
	{
		Best = &Road.LaneSections[0];
	}
	return Best;
}

double FOpenDriveMap::GetLaneOffset(const FOpenDriveRoad& Road, double S) const
{
	return FOpenDriveCubic::EvalPiecewise(Road.LaneOffset, S);
}

double FOpenDriveMap::GetLaneWidth(const FOpenDriveRoad& Road, double S, int32 LaneId) const
{
	const FOpenDriveLaneSection* Sec = FindLaneSection(Road, S);
	const FOpenDriveLane* Lane = Sec ? Sec->FindLane(LaneId) : nullptr;
	return Lane ? Lane->GetWidth(S) : 0.0;
}

double FOpenDriveMap::GetLaneCenterT(const FOpenDriveRoad& Road, double S, int32 LaneId) const
{
	const double Offset = GetLaneOffset(Road, S);
	if (LaneId == 0)
	{
		return Offset;
	}
	const FOpenDriveLaneSection* Sec = FindLaneSection(Road, S);
	if (!Sec)
	{
		return Offset;
	}
	const int32 Sign = LaneId > 0 ? 1 : -1;
	double T = 0.0;
	for (int32 Id = Sign; Id != LaneId; Id += Sign)
	{
		const FOpenDriveLane* L = Sec->FindLane(Id);
		T += L ? L->GetWidth(S) : 0.0;
	}
	const FOpenDriveLane* Target = Sec->FindLane(LaneId);
	T += (Target ? Target->GetWidth(S) : 0.0) * 0.5;
	return Offset + Sign * T;
}

double FOpenDriveMap::GetOuterBorderT(const FOpenDriveRoad& Road, double S, bool bLeft) const
{
	double T = GetLaneOffset(Road, S);
	if (const FOpenDriveLaneSection* Sec = FindLaneSection(Road, S))
	{
		double Sum = 0.0;
		for (const FOpenDriveLane& L : Sec->Lanes)
		{
			if ((L.Id > 0) == bLeft)
			{
				Sum += L.GetWidth(S);
			}
		}
		T += bLeft ? Sum : -Sum;
	}
	return T;
}

int32 FOpenDriveMap::FindLaneAt(const FOpenDriveRoad& Road, double S, double T) const
{
	const FOpenDriveLaneSection* Sec = FindLaneSection(Road, S);
	if (!Sec)
	{
		return 0;
	}
	const double Rel = T - GetLaneOffset(Road, S);
	const bool bLeft = Rel >= 0.0;
	const int32 Sign = bLeft ? 1 : -1;
	double Cum = 0.0;
	int32 Last = 0;
	for (int32 Id = Sign;; Id += Sign)
	{
		const FOpenDriveLane* L = Sec->FindLane(Id);
		if (!L)
		{
			// Gap or end of the lane list on this side.
			bool bAnyFurther = false;
			for (const FOpenDriveLane& Other : Sec->Lanes)
			{
				if (Other.Id * Sign > Id * Sign)
				{
					bAnyFurther = true;
					break;
				}
			}
			if (!bAnyFurther)
			{
				break;
			}
			continue;
		}
		Cum += L->GetWidth(S);
		Last = Id;
		if (FMath::Abs(Rel) <= Cum)
		{
			return Id;
		}
	}
	return Last;
}

int32 FOpenDriveMap::ClampLaneId(const FOpenDriveRoad& Road, double S, int32 LaneId) const
{
	const FOpenDriveLaneSection* Sec = FindLaneSection(Road, S);
	if (!Sec || Sec->FindLane(LaneId))
	{
		return LaneId;
	}
	int32 Best = LaneId;
	int32 BestDist = MAX_int32;
	for (const FOpenDriveLane& L : Sec->Lanes)
	{
		if ((L.Id > 0) == (LaneId > 0))
		{
			const int32 D = FMath::Abs(L.Id - LaneId);
			if (D < BestDist)
			{
				BestDist = D;
				Best = L.Id;
			}
		}
	}
	return Best;
}

// ------------------------------------------------------------------------------------------------
// Spatial query
// ------------------------------------------------------------------------------------------------

bool FOpenDriveMap::FindClosestRoadPosition(double X, double Y, double MaxDistance, FOpenDriveRoadPosition& Out) const
{
	double BestScore = TNumericLimits<double>::Max();
	bool bFound = false;

	for (const FOpenDriveRoad& Road : Roads)
	{
		const double Margin = Road.MaxExtent + MaxDistance;
		if (X < Road.MinX - Margin || X > Road.MaxX + Margin || Y < Road.MinY - Margin || Y > Road.MaxY + Margin)
		{
			continue;
		}

		auto DistSq = [&](double S)
		{
			double Rx, Ry, Rh;
			EvaluateReferenceLine(Road, S, Rx, Ry, Rh);
			return FMath::Square(X - Rx) + FMath::Square(Y - Ry);
		};

		constexpr double Step = 2.0;
		const int32 N = FMath::Max(1, FMath::CeilToInt(Road.Length / Step));
		double BestS = 0.0;
		double BestD = TNumericLimits<double>::Max();
		for (int32 i = 0; i <= N; ++i)
		{
			const double S = FMath::Min(Road.Length, i * Step);
			const double D = DistSq(S);
			if (D < BestD)
			{
				BestD = D;
				BestS = S;
			}
		}

		double Lo = FMath::Max(0.0, BestS - Step);
		double Hi = FMath::Min(Road.Length, BestS + Step);
		for (int32 It = 0; It < 30; ++It)
		{
			const double M1 = Lo + (Hi - Lo) / 3.0;
			const double M2 = Hi - (Hi - Lo) / 3.0;
			if (DistSq(M1) < DistSq(M2))
			{
				Hi = M2;
			}
			else
			{
				Lo = M1;
			}
		}
		const double S = 0.5 * (Lo + Hi);

		double Rx, Ry, Rh;
		EvaluateReferenceLine(Road, S, Rx, Ry, Rh);
		const double Dx = X - Rx;
		const double Dy = Y - Ry;
		const double T = -Dx * FMath::Sin(Rh) + Dy * FMath::Cos(Rh);
		const double Along = Dx * FMath::Cos(Rh) + Dy * FMath::Sin(Rh);

		const double Left = GetOuterBorderT(Road, S, true);
		const double Right = GetOuterBorderT(Road, S, false);
		double Off = 0.0;
		if (T > Left)
		{
			Off = T - Left;
		}
		else if (T < Right)
		{
			Off = Right - T;
		}
		// Beyond either end of the road the projection is not on the road.
		if ((S <= 1e-6 && Along < 0.0) || (S >= Road.Length - 1e-6 && Along > 0.0))
		{
			Off = FMath::Sqrt(Off * Off + Along * Along);
		}
		if (Off > MaxDistance)
		{
			continue;
		}

		const int32 Lane = FindLaneAt(Road, S, T);
		const double Score = Off + 1e-3 * FMath::Abs(T - GetLaneCenterT(Road, S, Lane));
		if (Score < BestScore)
		{
			BestScore = Score;
			bFound = true;
			Out.RoadId = Road.Id;
			Out.S = S;
			Out.T = T;
			Out.LaneId = Lane;
			Out.DistanceToRoad = Off;
		}
	}
	return bFound;
}

// ------------------------------------------------------------------------------------------------
// Routing
// ------------------------------------------------------------------------------------------------

void FOpenDriveMap::GetSuccessors(const FOpenDriveRoad& Road, bool bForward, TArray<FOpenDriveSuccessor>& Out) const
{
	Out.Reset();

	const EOpenDriveElementType Type = bForward ? Road.SuccessorType : Road.PredecessorType;
	const FString& Id = bForward ? Road.SuccessorId : Road.PredecessorId;
	EOpenDriveContactPoint Contact = bForward ? Road.SuccessorContact : Road.PredecessorContact;

	if (Type == EOpenDriveElementType::Road)
	{
		const FOpenDriveRoad* Next = FindRoad(Id);
		if (!Next)
		{
			return;
		}
		if (Contact == EOpenDriveContactPoint::None)
		{
			Contact = bForward ? EOpenDriveContactPoint::Start : EOpenDriveContactPoint::End;
		}
		FOpenDriveSuccessor S;
		S.RoadId = Next->Id;
		S.bForward = (Contact == EOpenDriveContactPoint::Start);

		// Lane links live in the lane section touching the shared end.
		if (Road.LaneSections.Num() > 0)
		{
			const FOpenDriveLaneSection& Sec = bForward ? Road.LaneSections.Last() : Road.LaneSections[0];
			for (const FOpenDriveLane& L : Sec.Lanes)
			{
				const int32 Target = bForward ? L.Successor : L.Predecessor;
				if (Target != 0)
				{
					S.LaneMap.Add(L.Id, Target);
				}
			}
		}
		Out.Add(MoveTemp(S));
	}
	else if (Type == EOpenDriveElementType::Junction)
	{
		const FOpenDriveJunction* Junction = FindJunction(Id);
		if (!Junction)
		{
			return;
		}
		for (const FOpenDriveJunctionConnection& Con : Junction->Connections)
		{
			if (Con.IncomingRoad != Road.Id)
			{
				continue;
			}
			const FOpenDriveRoad* Connecting = FindRoad(Con.ConnectingRoad);
			if (!Connecting)
			{
				continue;
			}
			FOpenDriveSuccessor S;
			S.RoadId = Connecting->Id;
			S.bForward = (Con.ContactPoint != EOpenDriveContactPoint::End);
			for (const TPair<int32, int32>& Link : Con.LaneLinks)
			{
				S.LaneMap.Add(Link.Key, Link.Value);
			}
			Out.Add(MoveTemp(S));
		}
	}
}

bool FOpenDriveMap::FindRoute(const FString& StartRoadId, bool bStartForward, const FString& TargetRoadId, TArray<FOpenDriveRouteStep>& OutRoute) const
{
	OutRoute.Reset();
	const int32* StartIdx = RoadIndex.Find(StartRoadId);
	if (!StartIdx || !RoadIndex.Contains(TargetRoadId))
	{
		return false;
	}

	// Dijkstra over (road, direction) nodes; edge cost = length of the road being entered.
	const int32 NumNodes = Roads.Num() * 2;
	TArray<double> Dist;
	Dist.Init(TNumericLimits<double>::Max(), NumNodes);
	TArray<int32> Prev;
	Prev.Init(INDEX_NONE, NumNodes);
	TArray<bool> Done;
	Done.Init(false, NumNodes);

	const int32 Start = *StartIdx * 2 + (bStartForward ? 1 : 0);
	Dist[Start] = Roads[*StartIdx].Length;

	int32 EndNode = INDEX_NONE;
	TArray<FOpenDriveSuccessor> Succ;
	while (true)
	{
		int32 Cur = INDEX_NONE;
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i < NumNodes; ++i)
		{
			if (!Done[i] && Dist[i] < Best)
			{
				Best = Dist[i];
				Cur = i;
			}
		}
		if (Cur == INDEX_NONE)
		{
			break;
		}
		Done[Cur] = true;

		const FOpenDriveRoad& CurRoad = Roads[Cur / 2];
		if (CurRoad.Id == TargetRoadId)
		{
			EndNode = Cur;
			break;
		}

		GetSuccessors(CurRoad, (Cur & 1) != 0, Succ);
		for (const FOpenDriveSuccessor& S : Succ)
		{
			const int32* Idx = RoadIndex.Find(S.RoadId);
			if (!Idx)
			{
				continue;
			}
			const int32 Node = *Idx * 2 + (S.bForward ? 1 : 0);
			const double NewDist = Dist[Cur] + Roads[*Idx].Length;
			if (!Done[Node] && NewDist < Dist[Node])
			{
				Dist[Node] = NewDist;
				Prev[Node] = Cur;
			}
		}
	}

	if (EndNode == INDEX_NONE)
	{
		return false;
	}
	for (int32 Node = EndNode; Node != INDEX_NONE; Node = Prev[Node])
	{
		FOpenDriveRouteStep Step;
		Step.RoadId = Roads[Node / 2].Id;
		Step.bForward = (Node & 1) != 0;
		OutRoute.Add(Step);
	}
	Algo::Reverse(OutRoute);
	return true;
}
