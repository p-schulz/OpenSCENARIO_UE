#include "OpenDriveWriter.h"
#include "OpenDrive/OpenDriveMap.h"

namespace
{
	FString Esc(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("&"), TEXT("&amp;"));
		Out.ReplaceInline(TEXT("\""), TEXT("&quot;"));
		Out.ReplaceInline(TEXT("<"), TEXT("&lt;"));
		Out.ReplaceInline(TEXT(">"), TEXT("&gt;"));
		return Out;
	}

	FString D(double V)
	{
		// Compact, round-trip-safe fixed representation (OpenDRIVE readers expect plain decimal, not "1e-05").
		FString S = FString::Printf(TEXT("%.10f"), V);
		int32 Dot;
		if (S.FindChar(TEXT('.'), Dot))
		{
			int32 End = S.Len();
			while (End > Dot + 2 && S[End - 1] == TEXT('0'))
			{
				--End;
			}
			S = S.Left(End);
		}
		return S;
	}

	void WriteCubics(FString& Out, const TCHAR* Tag, const TCHAR* SAttrName, const TArray<FOpenDriveCubic>& Entries, double SBase, const TCHAR* Indent)
	{
		for (const FOpenDriveCubic& C : Entries)
		{
			Out += FString::Printf(TEXT("%s<%s %s=\"%s\" a=\"%s\" b=\"%s\" c=\"%s\" d=\"%s\"/>\n"),
				Indent, Tag, SAttrName, *D(C.S - SBase), *D(C.A), *D(C.B), *D(C.C), *D(C.D));
		}
	}

	void WriteLink(FString& Out, const TCHAR* Tag, EOpenDriveElementType Type, const FString& Id, EOpenDriveContactPoint Contact)
	{
		if (Type == EOpenDriveElementType::None || Id.IsEmpty())
		{
			return;
		}
		const TCHAR* TypeStr = (Type == EOpenDriveElementType::Junction) ? TEXT("junction") : TEXT("road");
		const TCHAR* ContactStr = (Contact == EOpenDriveContactPoint::End) ? TEXT("end") : TEXT("start");
		if (Contact == EOpenDriveContactPoint::None)
		{
			Out += FString::Printf(TEXT("\t\t\t<%s elementType=\"%s\" elementId=\"%s\"/>\n"), Tag, TypeStr, *Esc(Id));
		}
		else
		{
			Out += FString::Printf(TEXT("\t\t\t<%s elementType=\"%s\" elementId=\"%s\" contactPoint=\"%s\"/>\n"), Tag, TypeStr, *Esc(Id), ContactStr);
		}
	}
}

FString FOpenDriveWriter::Write(const FOpenDriveMap& Map)
{
	FString Out;
	Out += TEXT("<?xml version=\"1.0\" standalone=\"yes\"?>\n");
	Out += TEXT("<OpenDRIVE>\n");
	Out += FString::Printf(TEXT("\t<header revMajor=\"1\" revMinor=\"7\" name=\"%s\" version=\"1.00\" north=\"0\" south=\"0\" east=\"0\" west=\"0\"/>\n"), *Esc(Map.GetName()));

	for (const FOpenDriveRoad& Road : Map.GetRoads())
	{
		const FString JunctionAttr = Road.JunctionId.IsEmpty() ? FString(TEXT("-1")) : Road.JunctionId;
		Out += FString::Printf(TEXT("\t<road name=\"%s\" length=\"%s\" id=\"%s\" junction=\"%s\">\n"),
			*Esc(Road.Name), *D(Road.Length), *Esc(Road.Id), *Esc(JunctionAttr));

		if (Road.PredecessorType != EOpenDriveElementType::None || Road.SuccessorType != EOpenDriveElementType::None)
		{
			Out += TEXT("\t\t<link>\n");
			WriteLink(Out, TEXT("predecessor"), Road.PredecessorType, Road.PredecessorId, Road.PredecessorContact);
			WriteLink(Out, TEXT("successor"), Road.SuccessorType, Road.SuccessorId, Road.SuccessorContact);
			Out += TEXT("\t\t</link>\n");
		}

		if (Road.Types.Num() > 0)
		{
			for (const FOpenDriveRoadTypeEntry& TypeEntry : Road.Types)
			{
				const FOpenDriveSpeedLimit* Speed = Road.SpeedLimits.FindByPredicate([&](const FOpenDriveSpeedLimit& L) { return FMath::Abs(L.S - TypeEntry.S) < 1e-6; });
				const FString CountryAttr = TypeEntry.Country.IsEmpty() ? FString() : FString::Printf(TEXT(" country=\"%s\""), *Esc(TypeEntry.Country));
				if (Speed)
				{
					Out += FString::Printf(TEXT("\t\t<type s=\"%s\" type=\"%s\"%s>\n\t\t\t<speed max=\"%s\" unit=\"m/s\"/>\n\t\t</type>\n"),
						*D(TypeEntry.S), *OpenDriveRoadTypeToString(TypeEntry.Type), *CountryAttr, *D(Speed->MaxSpeed));
				}
				else
				{
					Out += FString::Printf(TEXT("\t\t<type s=\"%s\" type=\"%s\"%s/>\n"), *D(TypeEntry.S), *OpenDriveRoadTypeToString(TypeEntry.Type), *CountryAttr);
				}
			}
		}
		else if (Road.SpeedLimits.Num() > 0)
		{
			// No explicit road-type entries (e.g. a road built entirely from the editor tool): default to "town".
			for (const FOpenDriveSpeedLimit& Limit : Road.SpeedLimits)
			{
				Out += FString::Printf(TEXT("\t\t<type s=\"%s\" type=\"town\">\n\t\t\t<speed max=\"%s\" unit=\"m/s\"/>\n\t\t</type>\n"), *D(Limit.S), *D(Limit.MaxSpeed));
			}
		}

		Out += TEXT("\t\t<planView>\n");
		for (const FOpenDriveGeometry& G : Road.Geometry)
		{
			Out += FString::Printf(TEXT("\t\t\t<geometry s=\"%s\" x=\"%s\" y=\"%s\" hdg=\"%s\" length=\"%s\">\n"),
				*D(G.S), *D(G.X), *D(G.Y), *D(G.Hdg), *D(G.Length));
			switch (G.Type)
			{
			case EOpenDriveGeometryType::Arc:
				Out += FString::Printf(TEXT("\t\t\t\t<arc curvature=\"%s\"/>\n"), *D(G.Curvature));
				break;
			case EOpenDriveGeometryType::Spiral:
				Out += FString::Printf(TEXT("\t\t\t\t<spiral curvStart=\"%s\" curvEnd=\"%s\"/>\n"), *D(G.CurvStart), *D(G.CurvEnd));
				break;
			case EOpenDriveGeometryType::Poly3:
				Out += FString::Printf(TEXT("\t\t\t\t<poly3 a=\"%s\" b=\"%s\" c=\"%s\" d=\"%s\"/>\n"), *D(G.AV), *D(G.BV), *D(G.CV), *D(G.DV));
				break;
			case EOpenDriveGeometryType::ParamPoly3:
				Out += FString::Printf(TEXT("\t\t\t\t<paramPoly3 aU=\"%s\" bU=\"%s\" cU=\"%s\" dU=\"%s\" aV=\"%s\" bV=\"%s\" cV=\"%s\" dV=\"%s\" pRange=\"%s\"/>\n"),
					*D(G.AU), *D(G.BU), *D(G.CU), *D(G.DU), *D(G.AV), *D(G.BV), *D(G.CV), *D(G.DV), G.bNormalizedRange ? TEXT("normalized") : TEXT("arcLength"));
				break;
			case EOpenDriveGeometryType::Line:
			default:
				Out += TEXT("\t\t\t\t<line/>\n");
				break;
			}
			Out += TEXT("\t\t\t</geometry>\n");
		}
		Out += TEXT("\t\t</planView>\n");

		if (Road.Elevation.Num() > 0)
		{
			Out += TEXT("\t\t<elevationProfile>\n");
			WriteCubics(Out, TEXT("elevation"), TEXT("s"), Road.Elevation, 0.0, TEXT("\t\t\t"));
			Out += TEXT("\t\t</elevationProfile>\n");
		}

		if (Road.Superelevation.Num() > 0 || Road.Crossfall.Num() > 0 || Road.Shape.Num() > 0)
		{
			Out += TEXT("\t\t<lateralProfile>\n");
			WriteCubics(Out, TEXT("superelevation"), TEXT("s"), Road.Superelevation, 0.0, TEXT("\t\t\t"));
			for (const FOpenDriveCrossfallEntry& Entry : Road.Crossfall)
			{
				const TCHAR* SideStr = Entry.Side == EOpenDriveCrossfallSide::Left ? TEXT("left") : Entry.Side == EOpenDriveCrossfallSide::Right ? TEXT("right") : TEXT("both");
				Out += FString::Printf(TEXT("\t\t\t<crossfall side=\"%s\" s=\"%s\" a=\"%s\" b=\"%s\" c=\"%s\" d=\"%s\"/>\n"),
					SideStr, *D(Entry.Cubic.S), *D(Entry.Cubic.A), *D(Entry.Cubic.B), *D(Entry.Cubic.C), *D(Entry.Cubic.D));
			}
			for (const FOpenDriveShapeEntry& Entry : Road.Shape)
			{
				Out += FString::Printf(TEXT("\t\t\t<shape s=\"%s\" t=\"%s\" a=\"%s\" b=\"%s\" c=\"%s\" d=\"%s\"/>\n"),
					*D(Entry.S), *D(Entry.T), *D(Entry.A), *D(Entry.B), *D(Entry.C), *D(Entry.D));
			}
			Out += TEXT("\t\t</lateralProfile>\n");
		}

		Out += TEXT("\t\t<lanes>\n");
		WriteCubics(Out, TEXT("laneOffset"), TEXT("s"), Road.LaneOffset, 0.0, TEXT("\t\t\t"));
		for (const FOpenDriveLaneSection& Sec : Road.LaneSections)
		{
			Out += FString::Printf(TEXT("\t\t\t<laneSection s=\"%s\">\n"), *D(Sec.S));

			TArray<const FOpenDriveLane*> Left, Right;
			const FOpenDriveLane* Center = nullptr;
			for (const FOpenDriveLane& L : Sec.Lanes)
			{
				if (L.Id > 0) { Left.Add(&L); }
				else if (L.Id < 0) { Right.Add(&L); }
				else { Center = &L; }
			}
			Left.Sort([](const FOpenDriveLane& A, const FOpenDriveLane& B) { return A.Id > B.Id; });
			Right.Sort([](const FOpenDriveLane& A, const FOpenDriveLane& B) { return A.Id > B.Id; });

			auto WriteSide = [&Out, &Sec](const TCHAR* SideTag, const TArray<const FOpenDriveLane*>& LaneList)
			{
				if (LaneList.Num() == 0)
				{
					return;
				}
				Out += FString::Printf(TEXT("\t\t\t\t<%s>\n"), SideTag);
				for (const FOpenDriveLane* LanePtr : LaneList)
				{
					const FOpenDriveLane& L = *LanePtr;
					Out += FString::Printf(TEXT("\t\t\t\t\t<lane id=\"%d\" type=\"%s\" level=\"false\">\n"), L.Id, *Esc(L.Type.IsEmpty() ? TEXT("driving") : L.Type));
					if (L.Predecessor != 0 || L.Successor != 0)
					{
						Out += TEXT("\t\t\t\t\t\t<link>\n");
						if (L.Predecessor != 0) { Out += FString::Printf(TEXT("\t\t\t\t\t\t\t<predecessor id=\"%d\"/>\n"), L.Predecessor); }
						if (L.Successor != 0) { Out += FString::Printf(TEXT("\t\t\t\t\t\t\t<successor id=\"%d\"/>\n"), L.Successor); }
						Out += TEXT("\t\t\t\t\t\t</link>\n");
					}
					WriteCubics(Out, TEXT("width"), TEXT("sOffset"), L.Widths, Sec.S, TEXT("\t\t\t\t\t\t"));
					for (const FOpenDriveRoadMarkEntry& RM : L.RoadMarks)
					{
						const FString WidthAttr = RM.Width >= 0.0 ? FString::Printf(TEXT(" width=\"%s\""), *D(RM.Width)) : FString();
						const FString HeightAttr = FMath::Abs(RM.Height) > 1e-9 ? FString::Printf(TEXT(" height=\"%s\""), *D(RM.Height)) : FString();
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<roadMark sOffset=\"%s\" type=\"%s\" weight=\"%s\" color=\"%s\"%s laneChange=\"%s\"%s/>\n"),
							*D(RM.S - Sec.S), *OpenDriveRoadMarkTypeToString(RM.Type), RM.Weight == EOpenDriveRoadMarkWeight::Bold ? TEXT("bold") : TEXT("standard"),
							*OpenDriveRoadMarkColorToString(RM.Color), *WidthAttr, *OpenDriveLaneChangeToString(RM.LaneChange), *HeightAttr);
					}
					for (const FOpenDriveLaneMaterialEntry& Mat : L.Materials)
					{
						const FString SurfaceAttr = Mat.Surface.IsEmpty() ? FString() : FString::Printf(TEXT(" surface=\"%s\""), *Esc(Mat.Surface));
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<material sOffset=\"%s\" friction=\"%s\" roughness=\"%s\"%s/>\n"),
							*D(Mat.S - Sec.S), *D(Mat.Friction), *D(Mat.Roughness), *SurfaceAttr);
					}
					for (const FOpenDriveSpeedLimit& Limit : L.SpeedLimits)
					{
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<speed sOffset=\"%s\" max=\"%s\" unit=\"m/s\"/>\n"), *D(Limit.S - Sec.S), *D(Limit.MaxSpeed));
					}
					for (const FOpenDriveLaneAccessEntry& Acc : L.Access)
					{
						const FString RestrictionAttr = Acc.Restriction.IsEmpty() ? FString() : FString::Printf(TEXT(" restriction=\"%s\""), *Esc(Acc.Restriction));
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<access sOffset=\"%s\" rule=\"%s\"%s/>\n"), *D(Acc.S - Sec.S), Acc.bAllow ? TEXT("allow") : TEXT("deny"), *RestrictionAttr);
					}
					for (const FOpenDriveLaneRuleEntry& Rule : L.Rules)
					{
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<rule sOffset=\"%s\" value=\"%s\"/>\n"), *D(Rule.S - Sec.S), *Esc(Rule.Value));
					}
					for (const FOpenDriveLaneHeightEntry& Ht : L.Heights)
					{
						Out += FString::Printf(TEXT("\t\t\t\t\t\t<height sOffset=\"%s\" inner=\"%s\" outer=\"%s\"/>\n"), *D(Ht.S - Sec.S), *D(Ht.InnerHeight), *D(Ht.OuterHeight));
					}
					Out += TEXT("\t\t\t\t\t</lane>\n");
				}
				Out += FString::Printf(TEXT("\t\t\t\t</%s>\n"), SideTag);
			};
			WriteSide(TEXT("left"), Left);
			if (Center)
			{
				TArray<const FOpenDriveLane*> CenterList;
				CenterList.Add(Center);
				WriteSide(TEXT("center"), CenterList);
			}
			else
			{
				Out += TEXT("\t\t\t\t<center>\n\t\t\t\t\t<lane id=\"0\" type=\"none\" level=\"false\"/>\n\t\t\t\t</center>\n");
			}
			WriteSide(TEXT("right"), Right);

			Out += TEXT("\t\t\t</laneSection>\n");
		}
		Out += TEXT("\t\t</lanes>\n");

		if (Road.Signals.Num() > 0)
		{
			Out += TEXT("\t\t<signals>\n");
			for (const FOpenDriveSignal& Sig : Road.Signals)
			{
				Out += FString::Printf(TEXT("\t\t\t<signal s=\"%s\" t=\"%s\" id=\"%s\" name=\"%s\" dynamic=\"%s\" orientation=\"%s\" zOffset=\"%s\" country=\"%s\" type=\"%s\" subtype=\"%s\" value=\"%s\" unit=\"%s\" height=\"%s\" width=\"%s\" hOffset=\"%s\" pitch=\"%s\" roll=\"%s\""),
					*D(Sig.S), *D(Sig.T), *Esc(Sig.Id), *Esc(Sig.Name), Sig.bDynamic ? TEXT("yes") : TEXT("no"), *OpenDriveSignalOrientationToString(Sig.Orientation),
					*D(Sig.ZOffset), *Esc(Sig.Country), *Esc(Sig.Type), *Esc(Sig.Subtype), *D(Sig.Value), *Esc(Sig.Unit), *D(Sig.Height), *D(Sig.Width), *D(Sig.HOffset), *D(Sig.Pitch), *D(Sig.Roll));
				if (Sig.Text.IsEmpty())
				{
					Out += TEXT("/>\n");
				}
				else
				{
					Out += FString::Printf(TEXT(" text=\"%s\"/>\n"), *Esc(Sig.Text));
				}
			}
			Out += TEXT("\t\t</signals>\n");
		}

		if (Road.Objects.Num() > 0)
		{
			Out += TEXT("\t\t<objects>\n");
			for (const FOpenDriveObject& Obj : Road.Objects)
			{
				Out += FString::Printf(TEXT("\t\t\t<object id=\"%s\" name=\"%s\" type=\"%s\" s=\"%s\" t=\"%s\" zOffset=\"%s\" hdg=\"%s\" pitch=\"%s\" roll=\"%s\" orientation=\"%s\""),
					*Esc(Obj.Id), *Esc(Obj.Name), *Esc(Obj.Type), *D(Obj.S), *D(Obj.T), *D(Obj.ZOffset), *D(Obj.HOffset), *D(Obj.Pitch), *D(Obj.Roll), *OpenDriveSignalOrientationToString(Obj.Orientation));
				if (Obj.Radius > 0.0)
				{
					Out += FString::Printf(TEXT(" radius=\"%s\" height=\"%s\"/>\n"), *D(Obj.Radius), *D(Obj.Height));
				}
				else
				{
					Out += FString::Printf(TEXT(" length=\"%s\" width=\"%s\" height=\"%s\"/>\n"), *D(Obj.Length), *D(Obj.Width), *D(Obj.Height));
				}
			}
			Out += TEXT("\t\t</objects>\n");
		}

		Out += TEXT("\t</road>\n");
	}

	for (const FOpenDriveJunction& Junction : Map.GetJunctions())
	{
		Out += FString::Printf(TEXT("\t<junction name=\"%s\" id=\"%s\">\n"), *Esc(Junction.Name), *Esc(Junction.Id));
		for (const FOpenDriveJunctionConnection& Con : Junction.Connections)
		{
			const TCHAR* ContactStr = (Con.ContactPoint == EOpenDriveContactPoint::End) ? TEXT("end") : TEXT("start");
			Out += FString::Printf(TEXT("\t\t<connection id=\"%s\" incomingRoad=\"%s\" connectingRoad=\"%s\" contactPoint=\"%s\">\n"),
				*Esc(Con.Id), *Esc(Con.IncomingRoad), *Esc(Con.ConnectingRoad), ContactStr);
			for (const TPair<int32, int32>& Link : Con.LaneLinks)
			{
				Out += FString::Printf(TEXT("\t\t\t<laneLink from=\"%d\" to=\"%d\"/>\n"), Link.Key, Link.Value);
			}
			Out += TEXT("\t\t</connection>\n");
		}
		Out += TEXT("\t</junction>\n");
	}

	for (const FOpenDriveController& Controller : Map.GetControllers())
	{
		Out += FString::Printf(TEXT("\t<controller id=\"%s\" name=\"%s\">\n"), *Esc(Controller.Id), *Esc(Controller.Name));
		for (const FOpenDriveControllerEntry& Entry : Controller.Controls)
		{
			const FString TypeAttr = Entry.Type.IsEmpty() ? FString() : FString::Printf(TEXT(" type=\"%s\""), *Esc(Entry.Type));
			Out += FString::Printf(TEXT("\t\t<control signalId=\"%s\"%s/>\n"), *Esc(Entry.SignalId), *TypeAttr);
		}
		Out += TEXT("\t</controller>\n");
	}

	for (const FOpenDriveJunctionGroup& Group : Map.GetJunctionGroups())
	{
		Out += FString::Printf(TEXT("\t<junctionGroup id=\"%s\" name=\"%s\" type=\"%s\">\n"), *Esc(Group.Id), *Esc(Group.Name), *Esc(Group.Type));
		for (const FString& Ref : Group.JunctionRefs)
		{
			Out += FString::Printf(TEXT("\t\t<junctionReference junction=\"%s\"/>\n"), *Esc(Ref));
		}
		Out += TEXT("\t</junctionGroup>\n");
	}

	Out += TEXT("</OpenDRIVE>\n");
	return Out;
}
