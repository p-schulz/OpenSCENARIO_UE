#include "Scenario/OpenScenarioParser.h"
#include "OpenScenarioModule.h"
#include "OpenScenarioXml.h"
#include "HAL/FileManager.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "XmlFile.h"
#include "XmlNode.h"

namespace
{
	/** Recursive-descent evaluator for `${...}` expressions: + - * / % ( ), numbers and $parameters. */
	struct FExpression
	{
		const FString& Text;
		const TMap<FString, FString>& Params;
		int32 Pos = 0;
		bool bOk = true;

		FExpression(const FString& InText, const TMap<FString, FString>& InParams) : Text(InText), Params(InParams) {}

		void SkipWs()
		{
			while (Pos < Text.Len() && FChar::IsWhitespace(Text[Pos]))
			{
				++Pos;
			}
		}

		double ParseExpr()
		{
			double V = ParseTerm();
			for (;;)
			{
				SkipWs();
				if (Pos < Text.Len() && Text[Pos] == '+') { ++Pos; V += ParseTerm(); }
				else if (Pos < Text.Len() && Text[Pos] == '-') { ++Pos; V -= ParseTerm(); }
				else { return V; }
			}
		}

		double ParseTerm()
		{
			double V = ParseUnary();
			for (;;)
			{
				SkipWs();
				if (Pos < Text.Len() && Text[Pos] == '*') { ++Pos; V *= ParseUnary(); }
				else if (Pos < Text.Len() && Text[Pos] == '/')
				{
					++Pos;
					const double D = ParseUnary();
					if (FMath::Abs(D) < 1e-300) { bOk = false; } else { V /= D; }
				}
				else if (Pos < Text.Len() && Text[Pos] == '%')
				{
					++Pos;
					const double D = ParseUnary();
					if (FMath::Abs(D) < 1e-300) { bOk = false; } else { V = FMath::Fmod(V, D); }
				}
				else { return V; }
			}
		}

		double ParseUnary()
		{
			SkipWs();
			if (Pos < Text.Len() && Text[Pos] == '-') { ++Pos; return -ParseUnary(); }
			if (Pos < Text.Len() && Text[Pos] == '+') { ++Pos; return ParseUnary(); }
			return ParsePrimary();
		}

		double ParsePrimary()
		{
			SkipWs();
			if (Pos >= Text.Len()) { bOk = false; return 0.0; }
			const TCHAR C = Text[Pos];
			if (C == '(')
			{
				++Pos;
				const double V = ParseExpr();
				SkipWs();
				if (Pos < Text.Len() && Text[Pos] == ')') { ++Pos; } else { bOk = false; }
				return V;
			}
			if (C == '$')
			{
				++Pos;
				const int32 Start = Pos;
				while (Pos < Text.Len() && (FChar::IsAlnum(Text[Pos]) || Text[Pos] == '_')) { ++Pos; }
				const FString* V = Params.Find(Text.Mid(Start, Pos - Start));
				if (!V || !FDefaultValueHelper::IsStringValidFloat(*V)) { bOk = false; return 0.0; }
				return FCString::Atod(**V);
			}
			const int32 Start = Pos;
			while (Pos < Text.Len() && (FChar::IsDigit(Text[Pos]) || Text[Pos] == '.' || Text[Pos] == 'e' || Text[Pos] == 'E'))
			{
				++Pos;
			}
			if (Start == Pos) { bOk = false; return 0.0; }
			return FCString::Atod(*Text.Mid(Start, Pos - Start));
		}
	};

	EOSCShape ParseShape(const FString& S)
	{
		if (S.Equals(TEXT("linear"), ESearchCase::IgnoreCase)) { return EOSCShape::Linear; }
		if (S.Equals(TEXT("cubic"), ESearchCase::IgnoreCase)) { return EOSCShape::Cubic; }
		if (S.Equals(TEXT("sinusoidal"), ESearchCase::IgnoreCase)) { return EOSCShape::Sinusoidal; }
		return EOSCShape::Step;
	}

	EOSCDimension ParseDimension(const FString& S)
	{
		if (S.Equals(TEXT("distance"), ESearchCase::IgnoreCase)) { return EOSCDimension::Distance; }
		if (S.Equals(TEXT("rate"), ESearchCase::IgnoreCase)) { return EOSCDimension::Rate; }
		return EOSCDimension::Time;
	}
}


// ------------------------------------------------------------------------------------------------
// Attribute helpers
// ------------------------------------------------------------------------------------------------

void FOpenScenarioParser::Warn(const FString& Message)
{
	UE_LOG(LogOpenScenario, Warning, TEXT("OpenSCENARIO: %s"), *Message);
	if (Messages)
	{
		Messages->Add(Message);
	}
}

FString FOpenScenarioParser::Resolve(const FString& Value)
{
	FString V = Value.TrimStartAndEnd();
	if (V.StartsWith(TEXT("${")) && V.EndsWith(TEXT("}")))
	{
		const FString Inner = V.Mid(2, V.Len() - 3);
		FExpression Expr(Inner, Params);
		const double Result = Expr.ParseExpr();
		Expr.SkipWs();
		if (!Expr.bOk || Expr.Pos != Inner.Len())
		{
			Warn(FString::Printf(TEXT("Cannot evaluate expression '%s'."), *V));
			return FString();
		}
		return FString::SanitizeFloat(Result);
	}
	if (V.StartsWith(TEXT("$")))
	{
		if (const FString* P = Params.Find(V.Mid(1)))
		{
			return *P;
		}
		Warn(FString::Printf(TEXT("Unknown parameter '%s'."), *V));
		return FString();
	}
	return Value;
}

bool FOpenScenarioParser::TryAttr(const FXmlNode* Node, const TCHAR* Name, FString& Out)
{
	FString Raw;
	if (!OSCXml::TryAttr(Node, Name, Raw))
	{
		return false;
	}
	Out = Resolve(Raw);
	return true;
}

FString FOpenScenarioParser::Attr(const FXmlNode* Node, const TCHAR* Name, const FString& Default)
{
	FString V;
	return TryAttr(Node, Name, V) ? V : Default;
}

double FOpenScenarioParser::AttrD(const FXmlNode* Node, const TCHAR* Name, double Default)
{
	FString V;
	return (TryAttr(Node, Name, V) && !V.IsEmpty()) ? FCString::Atod(*V) : Default;
}

int32 FOpenScenarioParser::AttrI(const FXmlNode* Node, const TCHAR* Name, int32 Default)
{
	FString V;
	return (TryAttr(Node, Name, V) && !V.IsEmpty()) ? FMath::RoundToInt(FCString::Atod(*V)) : Default;
}

bool FOpenScenarioParser::AttrB(const FXmlNode* Node, const TCHAR* Name, bool Default)
{
	FString V;
	return TryAttr(Node, Name, V) ? OSCXml::ParseBool(V) : Default;
}

void FOpenScenarioParser::ReadParameterDeclarations(const FXmlNode* Node, const TMap<FString, FString>* Overrides, TArray<FOSCParameterDeclaration>* OutDeclarations)
{
	for (const FXmlNode* P : OSCXml::Children(Node, TEXT("ParameterDeclaration")))
	{
		FString Name;
		OSCXml::TryAttr(P, TEXT("name"), Name);
		FString Value;
		OSCXml::TryAttr(P, TEXT("value"), Value);
		if (Name.IsEmpty())
		{
			continue;
		}
		if (OutDeclarations)
		{
			FOSCParameterDeclaration Decl;
			Decl.Name = Name;
			Decl.Value = Value;
			OSCXml::TryAttr(P, TEXT("parameterType"), Decl.Type);
			OutDeclarations->Add(MoveTemp(Decl));
		}
		if (Overrides)
		{
			if (const FString* O = Overrides->Find(Name))
			{
				Value = *O;
			}
		}
		// A declaration may itself reference earlier parameters.
		Params.Add(Name, Resolve(Value));
	}
}

// ------------------------------------------------------------------------------------------------
// Entry point
// ------------------------------------------------------------------------------------------------

bool FOpenScenarioParser::Parse(const FString& Xml, const FString& BaseDirectory, const TMap<FString, FString>& ParameterOverrides, FOSCScenario& Out, TArray<FString>& OutMessages)
{
	Out = FOSCScenario();
	Params.Reset();
	CatalogDirs.Reset();
	BaseDir = BaseDirectory;
	Messages = &OutMessages;

	FXmlFile File(Xml, EConstructMethod::ConstructFromBuffer);
	if (!File.IsValid())
	{
		OutMessages.Add(File.GetLastError());
		return false;
	}
	const FXmlNode* Root = File.GetRootNode();
	if (!Root || !Root->GetTag().Equals(TEXT("OpenSCENARIO"), ESearchCase::CaseSensitive))
	{
		OutMessages.Add(TEXT("Root element is not <OpenSCENARIO>."));
		return false;
	}

	if (const FXmlNode* Header = OSCXml::Child(Root, TEXT("FileHeader")))
	{
		OSCXml::TryAttr(Header, TEXT("description"), Out.Description);
		OSCXml::TryAttr(Header, TEXT("author"), Out.Author);
		OSCXml::TryAttr(Header, TEXT("date"), Out.Date);
	}

	// 1.0/1.1 put the definition directly below the root; 1.2 wraps it.
	const FXmlNode* Def = OSCXml::Child(Root, TEXT("ScenarioDefinition"));
	if (!Def)
	{
		Def = OSCXml::Child(OSCXml::Child(Root, TEXT("OpenScenarioCategory")), TEXT("ScenarioDefinition"));
	}
	if (!Def)
	{
		Def = Root;
	}

	if (!OSCXml::Child(Def, TEXT("Storyboard")) && !OSCXml::Child(Def, TEXT("Entities")))
	{
		OutMessages.Add(TEXT("Document contains neither <Entities> nor <Storyboard> (catalog or parameter-variation file?)."));
		return false;
	}

	ReadParameterDeclarations(OSCXml::Child(Def, TEXT("ParameterDeclarations")), &ParameterOverrides, &Out.ParameterDeclarations);

	if (const FXmlNode* Catalogs = OSCXml::Child(Def, TEXT("CatalogLocations")))
	{
		for (const FXmlNode* C : Catalogs->GetChildrenNodes())
		{
			FString Path;
			if (TryAttr(OSCXml::Child(C, TEXT("Directory")), TEXT("path"), Path))
			{
				CatalogDirs.Add(C->GetTag(), Path);
			}
		}
	}

	if (const FXmlNode* Road = OSCXml::Child(Def, TEXT("RoadNetwork")))
	{
		TryAttr(OSCXml::Child(Road, TEXT("LogicFile")), TEXT("filepath"), Out.RoadNetworkFile);
		for (const FXmlNode* CtrlNode : OSCXml::Children(OSCXml::Child(Road, TEXT("TrafficSignals")), TEXT("TrafficSignalController")))
		{
			FOSCSignalController Ctrl;
			Ctrl.Name = Attr(CtrlNode, TEXT("name"));
			Ctrl.Delay = AttrD(CtrlNode, TEXT("delay"));
			for (const FXmlNode* PhaseNode : OSCXml::Children(CtrlNode, TEXT("Phase")))
			{
				FOSCSignalPhase Phase;
				Phase.Name = Attr(PhaseNode, TEXT("name"));
				Phase.Duration = AttrD(PhaseNode, TEXT("duration"));
				for (const FXmlNode* StateNode : OSCXml::Children(PhaseNode, TEXT("TrafficSignalState")))
				{
					FOSCSignalStateEntry Entry;
					Entry.SignalId = Attr(StateNode, TEXT("trafficSignalId"));
					Entry.State = Attr(StateNode, TEXT("state"));
					Phase.States.Add(Entry);
				}
				Ctrl.Phases.Add(MoveTemp(Phase));
			}
			Out.SignalControllers.Add(MoveTemp(Ctrl));
		}
	}

	ParseEntities(OSCXml::Child(Def, TEXT("Entities")), Out);
	ParseStoryboard(OSCXml::Child(Def, TEXT("Storyboard")), Out);

	Out.Parameters = Params;
	Messages = nullptr;
	return true;
}

// ------------------------------------------------------------------------------------------------
// Entities
// ------------------------------------------------------------------------------------------------

void FOpenScenarioParser::ParseEntityNode(const FXmlNode* Node, FOSCEntity& E)
{
	const FString& Tag = Node->GetTag();
	if (Tag == TEXT("Vehicle"))
	{
		E.Kind = EOSCEntityKind::Vehicle;
		E.Category = Attr(Node, TEXT("vehicleCategory"));
	}
	else if (Tag == TEXT("Pedestrian"))
	{
		E.Kind = EOSCEntityKind::Pedestrian;
		E.Category = Attr(Node, TEXT("pedestrianCategory"));
		E.Length = 0.6; E.Width = 0.6; E.Height = 1.8;
		E.CenterX = 0.0; E.CenterY = 0.0; E.CenterZ = 0.9;
		E.MaxSpeed = 10.0;
	}
	else if (Tag == TEXT("MiscObject"))
	{
		E.Kind = EOSCEntityKind::MiscObject;
		E.Category = Attr(Node, TEXT("miscObjectCategory"));
		E.Length = 1.0; E.Width = 1.0; E.Height = 1.0;
		E.CenterX = 0.0; E.CenterY = 0.0; E.CenterZ = 0.5;
		E.MaxSpeed = 0.0;
	}
	E.Model3d = Attr(Node, TEXT("model3d"), E.Model3d);

	if (const FXmlNode* BB = OSCXml::Child(Node, TEXT("BoundingBox")))
	{
		if (const FXmlNode* C = OSCXml::Child(BB, TEXT("Center")))
		{
			E.CenterX = AttrD(C, TEXT("x"), E.CenterX);
			E.CenterY = AttrD(C, TEXT("y"), E.CenterY);
			E.CenterZ = AttrD(C, TEXT("z"), E.CenterZ);
		}
		if (const FXmlNode* D = OSCXml::Child(BB, TEXT("Dimensions")))
		{
			E.Width = AttrD(D, TEXT("width"), E.Width);
			E.Length = AttrD(D, TEXT("length"), E.Length);
			E.Height = AttrD(D, TEXT("height"), E.Height);
		}
	}
	if (const FXmlNode* Perf = OSCXml::Child(Node, TEXT("Performance")))
	{
		E.MaxSpeed = AttrD(Perf, TEXT("maxSpeed"), E.MaxSpeed);
		E.MaxAcceleration = AttrD(Perf, TEXT("maxAcceleration"), E.MaxAcceleration);
		E.MaxDeceleration = AttrD(Perf, TEXT("maxDeceleration"), E.MaxDeceleration);
	}
}

bool FOpenScenarioParser::ParseCatalogEntity(const FXmlNode* RefNode, FOSCEntity& Entity)
{
	const FString CatalogName = Attr(RefNode, TEXT("catalogName"));
	const FString EntryName = Attr(RefNode, TEXT("entryName"));
	Entity.CatalogName = CatalogName;
	Entity.CatalogEntry = EntryName;

	static const TCHAR* const Kinds[] = { TEXT("VehicleCatalog"), TEXT("PedestrianCatalog"), TEXT("MiscObjectCatalog") };
	for (const TCHAR* Kind : Kinds)
	{
		const FString* Dir = CatalogDirs.Find(Kind);
		if (!Dir)
		{
			continue;
		}
		const FString AbsDir = FPaths::IsRelative(*Dir) ? FPaths::ConvertRelativePathToFull(BaseDir, *Dir) : *Dir;

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(AbsDir / TEXT("*.xosc")), true, false);
		for (const FString& FileName : Files)
		{
			FString CatalogXml;
			if (!FFileHelper::LoadFileToString(CatalogXml, *(AbsDir / FileName)))
			{
				continue;
			}
			FXmlFile CatalogFile(CatalogXml, EConstructMethod::ConstructFromBuffer);
			if (!CatalogFile.IsValid())
			{
				continue;
			}
			const FXmlNode* Catalog = OSCXml::Child(CatalogFile.GetRootNode(), TEXT("Catalog"));
			FString ThisName;
			OSCXml::TryAttr(Catalog, TEXT("name"), ThisName);
			if (!Catalog || ThisName != CatalogName)
			{
				continue;
			}
			for (const FXmlNode* Entry : Catalog->GetChildrenNodes())
			{
				FString N;
				if (!OSCXml::TryAttr(Entry, TEXT("name"), N) || N != EntryName)
				{
					continue;
				}
				// Assignment values are evaluated in the caller's scope, then applied over the
				// catalog entry's own defaults.
				TArray<TPair<FString, FString>> Assignments;
				for (const FXmlNode* A : OSCXml::Children(OSCXml::Child(RefNode, TEXT("ParameterAssignments")), TEXT("ParameterAssignment")))
				{
					Assignments.Emplace(Attr(A, TEXT("parameterRef")), Attr(A, TEXT("value")));
				}
				const TMap<FString, FString> Saved = Params;
				ReadParameterDeclarations(OSCXml::Child(Entry, TEXT("ParameterDeclarations")), nullptr);
				for (const TPair<FString, FString>& A : Assignments)
				{
					Params.Add(A.Key, A.Value);
				}
				ParseEntityNode(Entry, Entity);
				Params = Saved;
				return true;
			}
		}
	}
	return false;
}

void FOpenScenarioParser::ParseEntities(const FXmlNode* EntitiesNode, FOSCScenario& Out)
{
	for (const FXmlNode* Obj : OSCXml::Children(EntitiesNode, TEXT("ScenarioObject")))
	{
		FOSCEntity E;
		E.Name = Attr(Obj, TEXT("name"));

		// EntityObject is an XSD group, so the entity definition is a direct child of ScenarioObject
		// (some tools still emit an <EntityObject> wrapper, which is accepted as well).
		const FXmlNode* Holder = OSCXml::Child(Obj, TEXT("EntityObject"));
		if (!Holder)
		{
			Holder = Obj;
		}
		const FXmlNode* Def = nullptr;
		for (const FXmlNode* C : Holder->GetChildrenNodes())
		{
			const FString& Tag = C->GetTag();
			if (Tag == TEXT("Vehicle") || Tag == TEXT("Pedestrian") || Tag == TEXT("MiscObject")
				|| Tag == TEXT("CatalogReference") || Tag == TEXT("ExternalObjectReference"))
			{
				Def = C;
				break;
			}
		}
		if (!Def)
		{
			Warn(FString::Printf(TEXT("ScenarioObject '%s' has no Vehicle, Pedestrian, MiscObject or CatalogReference."), *E.Name));
		}
		else if (Def->GetTag() == TEXT("CatalogReference"))
		{
			if (!ParseCatalogEntity(Def, E))
			{
				Warn(FString::Printf(TEXT("Catalog entry '%s/%s' for '%s' not found; using default vehicle bounding box."),
					*Attr(Def, TEXT("catalogName")), *Attr(Def, TEXT("entryName")), *E.Name));
			}
		}
		else if (Def->GetTag() == TEXT("ExternalObjectReference"))
		{
			E.Kind = EOSCEntityKind::External;
		}
		else
		{
			ParseEntityNode(Def, E);
		}
		Out.Entities.Add(MoveTemp(E));
	}
}

// ------------------------------------------------------------------------------------------------
// Positions
// ------------------------------------------------------------------------------------------------

void FOpenScenarioParser::ParseOrientation(const FXmlNode* PosChild, FOSCPosition& Pos)
{
	if (const FXmlNode* O = OSCXml::Child(PosChild, TEXT("Orientation")))
	{
		Pos.bHasOrientation = true;
		Pos.bOrientationRelative = Attr(O, TEXT("type"), TEXT("relative")).Equals(TEXT("relative"), ESearchCase::IgnoreCase);
		Pos.H = AttrD(O, TEXT("h"));
		Pos.P = AttrD(O, TEXT("p"));
		Pos.R = AttrD(O, TEXT("r"));
	}
}

FOSCPosition FOpenScenarioParser::ParsePosition(const FXmlNode* PositionNode)
{
	FOSCPosition P;
	const FXmlNode* N = OSCXml::FirstChild(PositionNode);
	if (!N)
	{
		return P;
	}
	const FString& Tag = N->GetTag();

	if (Tag == TEXT("WorldPosition"))
	{
		P.Type = EOSCPositionType::World;
		P.X = AttrD(N, TEXT("x"));
		P.Y = AttrD(N, TEXT("y"));
		P.Z = AttrD(N, TEXT("z"));
		FString Dummy;
		if (TryAttr(N, TEXT("h"), Dummy))
		{
			P.bHasOrientation = true;
			P.bOrientationRelative = false;
			P.H = AttrD(N, TEXT("h"));
			P.P = AttrD(N, TEXT("p"));
			P.R = AttrD(N, TEXT("r"));
		}
	}
	else if (Tag == TEXT("RelativeWorldPosition") || Tag == TEXT("RelativeObjectPosition"))
	{
		P.Type = (Tag == TEXT("RelativeWorldPosition")) ? EOSCPositionType::RelativeWorld : EOSCPositionType::RelativeObject;
		P.EntityRef = Attr(N, TEXT("entityRef"));
		P.DX = AttrD(N, TEXT("dx"));
		P.DY = AttrD(N, TEXT("dy"));
		P.DZ = AttrD(N, TEXT("dz"));
		ParseOrientation(N, P);
	}
	else if (Tag == TEXT("RoadPosition"))
	{
		P.Type = EOSCPositionType::Road;
		P.RoadId = Attr(N, TEXT("roadId"));
		P.S = AttrD(N, TEXT("s"));
		P.T = AttrD(N, TEXT("t"));
		ParseOrientation(N, P);
	}
	else if (Tag == TEXT("LanePosition"))
	{
		P.Type = EOSCPositionType::Lane;
		P.RoadId = Attr(N, TEXT("roadId"));
		P.LaneId = AttrI(N, TEXT("laneId"));
		P.S = AttrD(N, TEXT("s"));
		P.Offset = AttrD(N, TEXT("offset"));
		ParseOrientation(N, P);
	}
	else if (Tag == TEXT("RelativeLanePosition"))
	{
		P.Type = EOSCPositionType::RelativeLane;
		P.EntityRef = Attr(N, TEXT("entityRef"));
		P.DLane = AttrI(N, TEXT("dLane"));
		P.DS = AttrD(N, TEXT("ds"));
		P.Offset = AttrD(N, TEXT("offset"));
		ParseOrientation(N, P);
	}
	else
	{
		Warn(FString::Printf(TEXT("Position type <%s> is not supported."), *Tag));
	}
	return P;
}

// ------------------------------------------------------------------------------------------------
// Actions
// ------------------------------------------------------------------------------------------------

FOSCDynamics FOpenScenarioParser::ParseDynamics(const FXmlNode* Node)
{
	FOSCDynamics D;
	if (Node)
	{
		D.Shape = ParseShape(Attr(Node, TEXT("dynamicsShape")));
		D.Dimension = ParseDimension(Attr(Node, TEXT("dynamicsDimension")));
		D.Value = AttrD(Node, TEXT("value"));
	}
	return D;
}

void FOpenScenarioParser::ParseTrajectory(const FXmlNode* TrajectoryNode, FOSCAction& Action)
{
	const FXmlNode* Polyline = OSCXml::Child(OSCXml::Child(TrajectoryNode, TEXT("Shape")), TEXT("Polyline"));
	if (!Polyline)
	{
		Warn(FString::Printf(TEXT("Trajectory '%s': only Polyline shapes are supported."), *Attr(TrajectoryNode, TEXT("name"))));
		Action.Type = EOSCActionType::Unsupported;
		Action.UnsupportedTag = TEXT("Trajectory/Shape");
		return;
	}
	for (const FXmlNode* V : OSCXml::Children(Polyline, TEXT("Vertex")))
	{
		FOSCTrajectoryVertex Vertex;
		FString TimeStr;
		if (TryAttr(V, TEXT("time"), TimeStr) && !TimeStr.IsEmpty())
		{
			Vertex.bHasTime = true;
			Vertex.Time = FCString::Atod(*TimeStr);
		}
		Vertex.Position = ParsePosition(OSCXml::Child(V, TEXT("Position")));
		Action.Vertices.Add(Vertex);
	}
}

void FOpenScenarioParser::ParseTrafficAction(const FXmlNode* TrafficNode, FOSCAction& A)
{
	const FXmlNode* Inner = OSCXml::FirstChild(TrafficNode);
	if (!Inner)
	{
		A.UnsupportedTag = TEXT("TrafficAction");
		return;
	}
	const FString& Tag = Inner->GetTag();
	FOSCTraffic& T = A.Traffic;
	T.TrafficName = Attr(TrafficNode, TEXT("trafficName"), Attr(Inner, TEXT("trafficName"), A.Name));

	if (Tag == TEXT("TrafficSwarmAction"))
	{
		T.Kind = EOSCTrafficKind::Swarm;
		T.CentralObject = Attr(OSCXml::Child(Inner, TEXT("CentralObject")), TEXT("entityRef"));
		T.SemiMajorAxis = AttrD(Inner, TEXT("semiMajorAxis"), T.SemiMajorAxis);
		T.SemiMinorAxis = AttrD(Inner, TEXT("semiMinorAxis"), T.SemiMinorAxis);
		T.InnerRadius = AttrD(Inner, TEXT("innerRadius"), T.InnerRadius);
		T.Offset = AttrD(Inner, TEXT("offset"), 0.0);
		T.NumberOfVehicles = AttrI(Inner, TEXT("numberOfVehicles"), T.NumberOfVehicles);
		T.Velocity = AttrD(Inner, TEXT("velocity"), 0.0);
	}
	else if (Tag == TEXT("TrafficSourceAction"))
	{
		T.Kind = EOSCTrafficKind::Source;
		T.Rate = AttrD(Inner, TEXT("rate"), T.Rate);
		T.Radius = AttrD(Inner, TEXT("radius"), T.Radius);
		T.Velocity = AttrD(Inner, TEXT("velocity"), 0.0);
		T.Position = ParsePosition(OSCXml::Child(Inner, TEXT("Position")));
	}
	else if (Tag == TEXT("TrafficSinkAction"))
	{
		T.Kind = EOSCTrafficKind::Sink;
		T.Rate = AttrD(Inner, TEXT("rate"), T.Rate);
		T.Radius = AttrD(Inner, TEXT("radius"), T.Radius);
		T.Position = ParsePosition(OSCXml::Child(Inner, TEXT("Position")));
	}
	else if (Tag == TEXT("TrafficStopAction"))
	{
		T.Kind = EOSCTrafficKind::Stop;
		T.TrafficName = Attr(Inner, TEXT("trafficName"), T.TrafficName);
		A.Type = EOSCActionType::Traffic;
		return;
	}
	else
	{
		A.UnsupportedTag = Tag;
		return;
	}

	if (const FXmlNode* Def = OSCXml::Child(Inner, TEXT("TrafficDefinition")))
	{
		for (const FXmlNode* Entry : OSCXml::Children(OSCXml::Child(Def, TEXT("VehicleCategoryDistribution")), TEXT("VehicleCategoryDistributionEntry")))
		{
			FOSCTrafficCategory C;
			C.Category = Attr(Entry, TEXT("category"), TEXT("car"));
			C.Weight = AttrD(Entry, TEXT("weight"), 1.0);
			T.Distribution.Add(C);
		}
	}
	A.Type = EOSCActionType::Traffic;
}

FOSCAction FOpenScenarioParser::ParseAction(const FXmlNode* Node)
{
	FOSCAction A;
	A.Name = Attr(Node, TEXT("name"));

	const FXmlNode* Global = Node->GetTag() == TEXT("GlobalAction") ? Node : OSCXml::Child(Node, TEXT("GlobalAction"));
	if (Global)
	{
		if (const FXmlNode* Traffic = OSCXml::Child(Global, TEXT("TrafficAction")))
		{
			ParseTrafficAction(Traffic, A);
		}
		else if (const FXmlNode* Signal = OSCXml::Child(OSCXml::Child(Global, TEXT("InfrastructureAction")), TEXT("TrafficSignalAction")))
		{
			if (const FXmlNode* State = OSCXml::Child(Signal, TEXT("TrafficSignalStateAction")))
			{
				A.Type = EOSCActionType::TrafficSignalState;
				A.SignalId = Attr(State, TEXT("name"));
				A.SignalState = Attr(State, TEXT("state"));
			}
			else if (const FXmlNode* Ctrl = OSCXml::Child(Signal, TEXT("TrafficSignalControllerAction")))
			{
				A.Type = EOSCActionType::TrafficSignalController;
				A.ControllerRef = Attr(Ctrl, TEXT("trafficSignalControllerRef"));
				A.ControllerPhase = Attr(Ctrl, TEXT("phase"));
			}
			else
			{
				A.UnsupportedTag = TEXT("TrafficSignalAction");
			}
		}
		else
		{
			const FXmlNode* Other = OSCXml::FirstChild(Global);
			A.UnsupportedTag = Other ? Other->GetTag() : TEXT("GlobalAction");
		}
		if (A.Type == EOSCActionType::Unsupported)
		{
			Warn(FString::Printf(TEXT("Action '%s' (<%s>) is not supported and will be ignored."), *A.Name, *A.UnsupportedTag));
		}
		return A;
	}

	const FXmlNode* Private = Node->GetTag() == TEXT("PrivateAction") ? Node : OSCXml::Child(Node, TEXT("PrivateAction"));
	if (!Private)
	{
		const FXmlNode* Other = OSCXml::FirstChild(Node);
		A.UnsupportedTag = Other ? Other->GetTag() : Node->GetTag();
		return A;
	}
	const FXmlNode* Category = OSCXml::FirstChild(Private);
	if (!Category)
	{
		return A;
	}
	const FString& CatTag = Category->GetTag();

	if (CatTag == TEXT("TeleportAction"))
	{
		A.Type = EOSCActionType::Teleport;
		A.Position = ParsePosition(OSCXml::Child(Category, TEXT("Position")));
	}
	else if (CatTag == TEXT("LongitudinalAction"))
	{
		const FXmlNode* Inner = OSCXml::FirstChild(Category);
		if (Inner && Inner->GetTag() == TEXT("SpeedAction"))
		{
			A.Type = EOSCActionType::Speed;
			A.Dynamics = ParseDynamics(OSCXml::Child(Inner, TEXT("SpeedActionDynamics")));
			const FXmlNode* Target = OSCXml::Child(Inner, TEXT("SpeedActionTarget"));
			if (const FXmlNode* Abs = OSCXml::Child(Target, TEXT("AbsoluteTargetSpeed")))
			{
				A.SpeedValue = AttrD(Abs, TEXT("value"));
			}
			else if (const FXmlNode* Rel = OSCXml::Child(Target, TEXT("RelativeTargetSpeed")))
			{
				A.bSpeedRelative = true;
				A.RefEntity = Attr(Rel, TEXT("entityRef"));
				A.SpeedValue = AttrD(Rel, TEXT("value"));
				A.bSpeedFactor = Attr(Rel, TEXT("speedTargetValueType")).Equals(TEXT("factor"), ESearchCase::IgnoreCase)
					|| Attr(Rel, TEXT("valueType")).Equals(TEXT("factor"), ESearchCase::IgnoreCase);
			}
			else
			{
				A.Type = EOSCActionType::Unsupported;
				A.UnsupportedTag = TEXT("SpeedAction/SpeedActionTarget");
			}
		}
		else
		{
			A.UnsupportedTag = Inner ? Inner->GetTag() : CatTag;
		}
	}
	else if (CatTag == TEXT("LateralAction"))
	{
		const FXmlNode* Inner = OSCXml::FirstChild(Category);
		if (Inner && Inner->GetTag() == TEXT("LaneChangeAction"))
		{
			A.Type = EOSCActionType::LaneChange;
			A.LaneOffset = AttrD(Inner, TEXT("targetLaneOffset"));
			A.Dynamics = ParseDynamics(OSCXml::Child(Inner, TEXT("LaneChangeActionDynamics")));
			const FXmlNode* Target = OSCXml::Child(Inner, TEXT("LaneChangeTarget"));
			if (const FXmlNode* Abs = OSCXml::Child(Target, TEXT("AbsoluteTargetLane")))
			{
				A.LaneValue = AttrI(Abs, TEXT("value"));
			}
			else if (const FXmlNode* Rel = OSCXml::Child(Target, TEXT("RelativeTargetLane")))
			{
				A.bLaneRelative = true;
				A.RefEntity = Attr(Rel, TEXT("entityRef"));
				A.LaneValue = AttrI(Rel, TEXT("value"));
			}
			else
			{
				A.Type = EOSCActionType::Unsupported;
				A.UnsupportedTag = TEXT("LaneChangeAction/LaneChangeTarget");
			}
		}
		else
		{
			A.UnsupportedTag = Inner ? Inner->GetTag() : CatTag;
		}
	}
	else if (CatTag == TEXT("RoutingAction"))
	{
		const FXmlNode* Inner = OSCXml::FirstChild(Category);
		if (Inner && Inner->GetTag() == TEXT("AssignRouteAction"))
		{
			const FXmlNode* Route = OSCXml::Child(Inner, TEXT("Route"));
			if (!Route)
			{
				Route = OSCXml::Child(OSCXml::Child(Inner, TEXT("RouteRef")), TEXT("Route"));
			}
			if (Route)
			{
				A.Type = EOSCActionType::AssignRoute;
				for (const FXmlNode* W : OSCXml::Children(Route, TEXT("Waypoint")))
				{
					A.Waypoints.Add(ParsePosition(OSCXml::Child(W, TEXT("Position"))));
				}
			}
			else
			{
				A.UnsupportedTag = TEXT("AssignRouteAction/CatalogReference");
			}
		}
		else if (Inner && Inner->GetTag() == TEXT("FollowTrajectoryAction"))
		{
			const FXmlNode* Traj = OSCXml::Child(Inner, TEXT("Trajectory"));
			if (!Traj)
			{
				Traj = OSCXml::Child(OSCXml::Child(Inner, TEXT("TrajectoryRef")), TEXT("Trajectory"));
			}
			if (Traj)
			{
				A.Type = EOSCActionType::FollowTrajectory;
				ParseTrajectory(Traj, A);
				if (const FXmlNode* Timing = OSCXml::Child(OSCXml::Child(Inner, TEXT("TimeReference")), TEXT("Timing")))
				{
					A.bTimeReference = true;
					A.bTimeAbsolute = Attr(Timing, TEXT("domainAbsoluteRelative"), TEXT("absolute")).Equals(TEXT("absolute"), ESearchCase::IgnoreCase);
					A.TimeOffset = AttrD(Timing, TEXT("offset"));
					A.TimeScale = AttrD(Timing, TEXT("scale"), 1.0);
				}
			}
			else
			{
				A.UnsupportedTag = TEXT("FollowTrajectoryAction/CatalogReference");
			}
		}
		else
		{
			A.UnsupportedTag = Inner ? Inner->GetTag() : CatTag;
		}
	}
	else
	{
		A.UnsupportedTag = CatTag;
	}

	if (A.Type == EOSCActionType::Unsupported)
	{
		Warn(FString::Printf(TEXT("Action '%s' (<%s>) is not supported and will be ignored."), *A.Name, *A.UnsupportedTag));
	}
	return A;
}

// ------------------------------------------------------------------------------------------------
// Triggers
// ------------------------------------------------------------------------------------------------

EOSCRule FOpenScenarioParser::ParseRule(const FString& S)
{
	if (S.Equals(TEXT("equalTo"), ESearchCase::IgnoreCase)) { return EOSCRule::EqualTo; }
	if (S.Equals(TEXT("lessThan"), ESearchCase::IgnoreCase)) { return EOSCRule::LessThan; }
	if (S.Equals(TEXT("greaterOrEqual"), ESearchCase::IgnoreCase)) { return EOSCRule::GreaterOrEqual; }
	if (S.Equals(TEXT("lessOrEqual"), ESearchCase::IgnoreCase)) { return EOSCRule::LessOrEqual; }
	if (S.Equals(TEXT("notEqualTo"), ESearchCase::IgnoreCase)) { return EOSCRule::NotEqualTo; }
	return EOSCRule::GreaterThan;
}

void FOpenScenarioParser::ParseEntityCondition(const FXmlNode* N, FOSCCondition& C)
{
	if (!N)
	{
		return;
	}
	const FString& Tag = N->GetTag();
	C.Rule = ParseRule(Attr(N, TEXT("rule")));
	C.Value = AttrD(N, TEXT("value"));
	C.bFreespace = AttrB(N, TEXT("freespace"));

	if (Tag == TEXT("SpeedCondition")) { C.Type = EOSCConditionType::Speed; }
	else if (Tag == TEXT("RelativeSpeedCondition")) { C.Type = EOSCConditionType::RelativeSpeed; C.EntityRef = Attr(N, TEXT("entityRef")); }
	else if (Tag == TEXT("AccelerationCondition")) { C.Type = EOSCConditionType::Acceleration; }
	else if (Tag == TEXT("TraveledDistanceCondition")) { C.Type = EOSCConditionType::TraveledDistance; }
	else if (Tag == TEXT("StandStillCondition")) { C.Type = EOSCConditionType::StandStill; C.Value = AttrD(N, TEXT("duration")); }
	else if (Tag == TEXT("ReachPositionCondition"))
	{
		C.Type = EOSCConditionType::ReachPosition;
		C.Tolerance = AttrD(N, TEXT("tolerance"), 1.0);
		C.Position = ParsePosition(OSCXml::Child(N, TEXT("Position")));
	}
	else if (Tag == TEXT("DistanceCondition"))
	{
		C.Type = EOSCConditionType::Distance;
		C.Position = ParsePosition(OSCXml::Child(N, TEXT("Position")));
	}
	else if (Tag == TEXT("RelativeDistanceCondition"))
	{
		C.Type = EOSCConditionType::RelativeDistance;
		C.EntityRef = Attr(N, TEXT("entityRef"));
		C.RelativeDistanceType = Attr(N, TEXT("relativeDistanceType"), Attr(N, TEXT("type"), TEXT("euclidianDistance")));
	}
	else if (Tag == TEXT("TimeHeadwayCondition"))
	{
		C.Type = EOSCConditionType::TimeHeadway;
		C.EntityRef = Attr(N, TEXT("entityRef"));
	}
	else if (Tag == TEXT("CollisionCondition"))
	{
		C.Type = EOSCConditionType::Collision;
		C.EntityRef = Attr(OSCXml::Child(N, TEXT("EntityRef")), TEXT("entityRef"));
		if (C.EntityRef.IsEmpty())
		{
			C.Type = EOSCConditionType::Unsupported;
			C.UnsupportedTag = TEXT("CollisionCondition/ByType");
		}
	}
	else
	{
		C.UnsupportedTag = Tag;
	}
}

FOSCCondition FOpenScenarioParser::ParseCondition(const FXmlNode* Node)
{
	FOSCCondition C;
	C.Name = Attr(Node, TEXT("name"));
	C.Delay = AttrD(Node, TEXT("delay"));

	const FString Edge = Attr(Node, TEXT("conditionEdge"));
	if (Edge.Equals(TEXT("rising"), ESearchCase::IgnoreCase)) { C.Edge = EOSCEdge::Rising; }
	else if (Edge.Equals(TEXT("falling"), ESearchCase::IgnoreCase)) { C.Edge = EOSCEdge::Falling; }
	else if (Edge.Equals(TEXT("risingOrFalling"), ESearchCase::IgnoreCase)) { C.Edge = EOSCEdge::RisingOrFalling; }
	else { C.Edge = EOSCEdge::None; }

	if (const FXmlNode* ByEntity = OSCXml::Child(Node, TEXT("ByEntityCondition")))
	{
		const FXmlNode* Trig = OSCXml::Child(ByEntity, TEXT("TriggeringEntities"));
		C.bAllTriggeringEntities = Attr(Trig, TEXT("triggeringEntitiesRule"), TEXT("any")).Equals(TEXT("all"), ESearchCase::IgnoreCase);
		for (const FXmlNode* Ref : OSCXml::Children(Trig, TEXT("EntityRef")))
		{
			C.TriggeringEntities.Add(Attr(Ref, TEXT("entityRef")));
		}
		ParseEntityCondition(OSCXml::FirstChild(OSCXml::Child(ByEntity, TEXT("EntityCondition"))), C);
	}
	else if (const FXmlNode* ByValue = OSCXml::Child(Node, TEXT("ByValueCondition")))
	{
		const FXmlNode* V = OSCXml::FirstChild(ByValue);
		if (V)
		{
			const FString& Tag = V->GetTag();
			C.Rule = ParseRule(Attr(V, TEXT("rule")));
			if (Tag == TEXT("SimulationTimeCondition"))
			{
				C.Type = EOSCConditionType::SimulationTime;
				C.Value = AttrD(V, TEXT("value"));
			}
			else if (Tag == TEXT("StoryboardElementStateCondition"))
			{
				C.Type = EOSCConditionType::StoryboardElementState;
				C.ElementType = Attr(V, TEXT("storyboardElementType"));
				C.ElementRef = Attr(V, TEXT("storyboardElementRef"));
				C.ElementState = Attr(V, TEXT("state"));
			}
			else if (Tag == TEXT("TrafficSignalCondition"))
			{
				C.Type = EOSCConditionType::TrafficSignal;
				C.SignalId = Attr(V, TEXT("name"));
				C.SignalState = Attr(V, TEXT("state"));
			}
			else if (Tag == TEXT("ParameterCondition"))
			{
				C.Type = EOSCConditionType::Parameter;
				C.ParameterRef = Attr(V, TEXT("parameterRef"));
				C.StringValue = Attr(V, TEXT("value"));
				C.Value = AttrD(V, TEXT("value"));
			}
			else
			{
				C.UnsupportedTag = Tag;
			}
		}
	}

	if (C.Type == EOSCConditionType::Unsupported)
	{
		if (C.UnsupportedTag.IsEmpty())
		{
			C.UnsupportedTag = TEXT("Condition");
		}
		Warn(FString::Printf(TEXT("Condition '%s' (<%s>) is not supported and never becomes true."), *C.Name, *C.UnsupportedTag));
	}
	return C;
}

FOSCTrigger FOpenScenarioParser::ParseTrigger(const FXmlNode* TriggerNode)
{
	FOSCTrigger T;
	if (!TriggerNode)
	{
		return T;
	}
	T.bPresent = true;
	for (const FXmlNode* GroupNode : OSCXml::Children(TriggerNode, TEXT("ConditionGroup")))
	{
		FOSCConditionGroup Group;
		for (const FXmlNode* CondNode : OSCXml::Children(GroupNode, TEXT("Condition")))
		{
			Group.Conditions.Add(ParseCondition(CondNode));
		}
		T.Groups.Add(MoveTemp(Group));
	}
	return T;
}

// ------------------------------------------------------------------------------------------------
// Storyboard
// ------------------------------------------------------------------------------------------------

void FOpenScenarioParser::ParseInit(const FXmlNode* InitNode, FOSCScenario& Out)
{
	const FXmlNode* Actions = OSCXml::Child(InitNode, TEXT("Actions"));
	for (const FXmlNode* Priv : OSCXml::Children(Actions, TEXT("Private")))
	{
		FOSCInitActions Init;
		Init.EntityRef = Attr(Priv, TEXT("entityRef"));
		for (const FXmlNode* PA : OSCXml::Children(Priv, TEXT("PrivateAction")))
		{
			Init.Actions.Add(ParseAction(PA));
		}
		Out.InitActions.Add(MoveTemp(Init));
	}
	// Global actions are kept as a group without entity.
	FOSCInitActions Global;
	for (const FXmlNode* GA : OSCXml::Children(Actions, TEXT("GlobalAction")))
	{
		Global.Actions.Add(ParseAction(GA));
	}
	if (Global.Actions.Num() > 0)
	{
		Out.InitActions.Add(MoveTemp(Global));
	}
}

void FOpenScenarioParser::ParseStoryboard(const FXmlNode* SB, FOSCScenario& Out)
{
	if (!SB)
	{
		return;
	}
	ParseInit(OSCXml::Child(SB, TEXT("Init")), Out);

	for (const FXmlNode* StoryNode : OSCXml::Children(SB, TEXT("Story")))
	{
		FOSCStory Story;
		Story.Name = Attr(StoryNode, TEXT("name"));

		const TMap<FString, FString> SavedParams = Params;
		ReadParameterDeclarations(OSCXml::Child(StoryNode, TEXT("ParameterDeclarations")), nullptr);

		for (const FXmlNode* ActNode : OSCXml::Children(StoryNode, TEXT("Act")))
		{
			FOSCAct Act;
			Act.Name = Attr(ActNode, TEXT("name"));
			Act.StartTrigger = ParseTrigger(OSCXml::Child(ActNode, TEXT("StartTrigger")));
			Act.StopTrigger = ParseTrigger(OSCXml::Child(ActNode, TEXT("StopTrigger")));

			for (const FXmlNode* MGNode : OSCXml::Children(ActNode, TEXT("ManeuverGroup")))
			{
				FOSCManeuverGroup MG;
				MG.Name = Attr(MGNode, TEXT("name"));
				MG.MaxExecutions = FMath::Max(1, AttrI(MGNode, TEXT("maximumExecutionCount"), 1));

				const FXmlNode* Actors = OSCXml::Child(MGNode, TEXT("Actors"));
				MG.bSelectTriggeringEntities = AttrB(Actors, TEXT("selectTriggeringEntities"));
				for (const FXmlNode* Ref : OSCXml::Children(Actors, TEXT("EntityRef")))
				{
					MG.Actors.Add(Attr(Ref, TEXT("entityRef")));
				}
				if (MG.bSelectTriggeringEntities)
				{
					Warn(FString::Printf(TEXT("ManeuverGroup '%s': selectTriggeringEntities is not supported; only listed EntityRefs act."), *MG.Name));
				}
				if (OSCXml::Child(MGNode, TEXT("CatalogReference")))
				{
					Warn(FString::Printf(TEXT("ManeuverGroup '%s': maneuver catalog references are not supported."), *MG.Name));
				}

				for (const FXmlNode* ManNode : OSCXml::Children(MGNode, TEXT("Maneuver")))
				{
					FOSCManeuver Man;
					Man.Name = Attr(ManNode, TEXT("name"));
					for (const FXmlNode* EvNode : OSCXml::Children(ManNode, TEXT("Event")))
					{
						FOSCEvent Ev;
						Ev.Name = Attr(EvNode, TEXT("name"));
						Ev.MaxExecutions = FMath::Max(1, AttrI(EvNode, TEXT("maximumExecutionCount"), 1));
						const FString Prio = Attr(EvNode, TEXT("priority"));
						Ev.Priority = (Prio.Equals(TEXT("override"), ESearchCase::IgnoreCase) || Prio.Equals(TEXT("overwrite"), ESearchCase::IgnoreCase)) ? EOSCPriority::Override
							: Prio.Equals(TEXT("skip"), ESearchCase::IgnoreCase) ? EOSCPriority::Skip : EOSCPriority::Parallel;
						Ev.StartTrigger = ParseTrigger(OSCXml::Child(EvNode, TEXT("StartTrigger")));
						for (const FXmlNode* ActionNode : OSCXml::Children(EvNode, TEXT("Action")))
						{
							Ev.Actions.Add(ParseAction(ActionNode));
						}
						Man.Events.Add(MoveTemp(Ev));
					}
					MG.Maneuvers.Add(MoveTemp(Man));
				}
				Act.ManeuverGroups.Add(MoveTemp(MG));
			}
			Story.Acts.Add(MoveTemp(Act));
		}
		Params = SavedParams;
		Out.Stories.Add(MoveTemp(Story));
	}

	Out.StopTrigger = ParseTrigger(OSCXml::Child(SB, TEXT("StopTrigger")));
}
