#include "Scenario/OpenScenarioWriter.h"

namespace
{
	FString Escape(const FString& In)
	{
		FString Out = In.Replace(TEXT("&"), TEXT("&amp;"));
		Out = Out.Replace(TEXT("<"), TEXT("&lt;"));
		Out = Out.Replace(TEXT(">"), TEXT("&gt;"));
		Out = Out.Replace(TEXT("\""), TEXT("&quot;"));
		return Out;
	}

	FString Num(double V)
	{
		return FString::SanitizeFloat(V);
	}

	/** Chainable attribute list. */
	struct FAttrs
	{
		FString Text;

		FAttrs& S(const TCHAR* Name, const FString& V)
		{
			Text += FString::Printf(TEXT(" %s=\"%s\""), Name, *Escape(V));
			return *this;
		}
		FAttrs& D(const TCHAR* Name, double V) { return S(Name, Num(V)); }
		FAttrs& I(const TCHAR* Name, int32 V) { return S(Name, FString::Printf(TEXT("%d"), V)); }
		FAttrs& B(const TCHAR* Name, bool V) { return S(Name, V ? TEXT("true") : TEXT("false")); }
	};

	class FXmlOut
	{
	public:
		FString Buf;

		void Open(const TCHAR* Tag, const FAttrs& A = FAttrs())
		{
			Line(FString::Printf(TEXT("<%s%s>"), Tag, *A.Text));
			++Depth;
		}
		void Close(const TCHAR* Tag)
		{
			--Depth;
			Line(FString::Printf(TEXT("</%s>"), Tag));
		}
		void Leaf(const TCHAR* Tag, const FAttrs& A = FAttrs())
		{
			Line(FString::Printf(TEXT("<%s%s />"), Tag, *A.Text));
		}
		void Raw(const FString& S) { Line(S); }

	private:
		void Line(const FString& S)
		{
			for (int32 i = 0; i < Depth; ++i)
			{
				Buf += TEXT("  ");
			}
			Buf += S;
			Buf += TEXT("\n");
		}
		int32 Depth = 0;
	};

	const TCHAR* ShapeStr(EOSCShape S)
	{
		switch (S)
		{
		case EOSCShape::Linear: return TEXT("linear");
		case EOSCShape::Cubic: return TEXT("cubic");
		case EOSCShape::Sinusoidal: return TEXT("sinusoidal");
		default: return TEXT("step");
		}
	}

	const TCHAR* DimensionStr(EOSCDimension D)
	{
		switch (D)
		{
		case EOSCDimension::Distance: return TEXT("distance");
		case EOSCDimension::Rate: return TEXT("rate");
		default: return TEXT("time");
		}
	}

	const TCHAR* RuleStr(EOSCRule R)
	{
		switch (R)
		{
		case EOSCRule::EqualTo: return TEXT("equalTo");
		case EOSCRule::LessThan: return TEXT("lessThan");
		case EOSCRule::GreaterOrEqual: return TEXT("greaterOrEqual");
		case EOSCRule::LessOrEqual: return TEXT("lessOrEqual");
		case EOSCRule::NotEqualTo: return TEXT("notEqualTo");
		default: return TEXT("greaterThan");
		}
	}

	const TCHAR* EdgeStr(EOSCEdge E)
	{
		switch (E)
		{
		case EOSCEdge::Rising: return TEXT("rising");
		case EOSCEdge::Falling: return TEXT("falling");
		case EOSCEdge::RisingOrFalling: return TEXT("risingOrFalling");
		default: return TEXT("none");
		}
	}

	const TCHAR* PriorityStr(EOSCPriority P)
	{
		switch (P)
		{
		case EOSCPriority::Override: return TEXT("override");
		case EOSCPriority::Skip: return TEXT("skip");
		default: return TEXT("parallel");
		}
	}

	void WriteOrientation(FXmlOut& X, const FOSCPosition& P)
	{
		if (P.bHasOrientation)
		{
			X.Leaf(TEXT("Orientation"), FAttrs().S(TEXT("type"), P.bOrientationRelative ? TEXT("relative") : TEXT("absolute")).D(TEXT("h"), P.H).D(TEXT("p"), P.P).D(TEXT("r"), P.R));
		}
	}

	/** Writes the <Position> wrapper element. */
	void WritePosition(FXmlOut& X, const FOSCPosition& P)
	{
		X.Open(TEXT("Position"));
		switch (P.Type)
		{
		case EOSCPositionType::World:
		{
			FAttrs A;
			A.D(TEXT("x"), P.X).D(TEXT("y"), P.Y).D(TEXT("z"), P.Z);
			if (P.bHasOrientation)
			{
				A.D(TEXT("h"), P.H).D(TEXT("p"), P.P).D(TEXT("r"), P.R);
			}
			X.Leaf(TEXT("WorldPosition"), A);
			break;
		}
		case EOSCPositionType::Road:
		case EOSCPositionType::Lane:
		case EOSCPositionType::RelativeWorld:
		case EOSCPositionType::RelativeObject:
		case EOSCPositionType::RelativeLane:
		{
			const TCHAR* Tag = TEXT("RoadPosition");
			FAttrs A;
			if (P.Type == EOSCPositionType::Road)
			{
				A.S(TEXT("roadId"), P.RoadId).D(TEXT("s"), P.S).D(TEXT("t"), P.T);
			}
			else if (P.Type == EOSCPositionType::Lane)
			{
				Tag = TEXT("LanePosition");
				A.S(TEXT("roadId"), P.RoadId).I(TEXT("laneId"), P.LaneId).D(TEXT("offset"), P.Offset).D(TEXT("s"), P.S);
			}
			else if (P.Type == EOSCPositionType::RelativeWorld || P.Type == EOSCPositionType::RelativeObject)
			{
				Tag = (P.Type == EOSCPositionType::RelativeWorld) ? TEXT("RelativeWorldPosition") : TEXT("RelativeObjectPosition");
				A.S(TEXT("entityRef"), P.EntityRef).D(TEXT("dx"), P.DX).D(TEXT("dy"), P.DY).D(TEXT("dz"), P.DZ);
			}
			else
			{
				Tag = TEXT("RelativeLanePosition");
				A.S(TEXT("entityRef"), P.EntityRef).I(TEXT("dLane"), P.DLane).D(TEXT("ds"), P.DS).D(TEXT("offset"), P.Offset);
			}
			if (P.bHasOrientation)
			{
				X.Open(Tag, A);
				WriteOrientation(X, P);
				X.Close(Tag);
			}
			else
			{
				X.Leaf(Tag, A);
			}
			break;
		}
		case EOSCPositionType::None:
		default:
			// An empty position is still valid XML for the editor to keep editing; the parser reports it.
			break;
		}
		X.Close(TEXT("Position"));
	}

	void WriteDynamics(FXmlOut& X, const TCHAR* Tag, const FOSCDynamics& D)
	{
		X.Leaf(Tag, FAttrs().S(TEXT("dynamicsShape"), ShapeStr(D.Shape)).D(TEXT("value"), D.Value).S(TEXT("dynamicsDimension"), DimensionStr(D.Dimension)));
	}

	/** Writes <PrivateAction>; returns false (and writes nothing) for unsupported actions. */
	bool WritePrivateAction(FXmlOut& X, const FOSCAction& A)
	{
		switch (A.Type)
		{
		case EOSCActionType::Teleport:
			X.Open(TEXT("PrivateAction"));
			X.Open(TEXT("TeleportAction"));
			WritePosition(X, A.Position);
			X.Close(TEXT("TeleportAction"));
			X.Close(TEXT("PrivateAction"));
			return true;

		case EOSCActionType::Speed:
			X.Open(TEXT("PrivateAction"));
			X.Open(TEXT("LongitudinalAction"));
			X.Open(TEXT("SpeedAction"));
			WriteDynamics(X, TEXT("SpeedActionDynamics"), A.Dynamics);
			X.Open(TEXT("SpeedActionTarget"));
			if (A.bSpeedRelative)
			{
				X.Leaf(TEXT("RelativeTargetSpeed"), FAttrs().S(TEXT("entityRef"), A.RefEntity).D(TEXT("value"), A.SpeedValue)
					.S(TEXT("speedTargetValueType"), A.bSpeedFactor ? TEXT("factor") : TEXT("delta")).B(TEXT("continuous"), false));
			}
			else
			{
				X.Leaf(TEXT("AbsoluteTargetSpeed"), FAttrs().D(TEXT("value"), A.SpeedValue));
			}
			X.Close(TEXT("SpeedActionTarget"));
			X.Close(TEXT("SpeedAction"));
			X.Close(TEXT("LongitudinalAction"));
			X.Close(TEXT("PrivateAction"));
			return true;

		case EOSCActionType::LaneChange:
			X.Open(TEXT("PrivateAction"));
			X.Open(TEXT("LateralAction"));
			X.Open(TEXT("LaneChangeAction"), FAttrs().D(TEXT("targetLaneOffset"), A.LaneOffset));
			WriteDynamics(X, TEXT("LaneChangeActionDynamics"), A.Dynamics);
			X.Open(TEXT("LaneChangeTarget"));
			if (A.bLaneRelative)
			{
				X.Leaf(TEXT("RelativeTargetLane"), FAttrs().S(TEXT("entityRef"), A.RefEntity).I(TEXT("value"), A.LaneValue));
			}
			else
			{
				X.Leaf(TEXT("AbsoluteTargetLane"), FAttrs().S(TEXT("value"), FString::Printf(TEXT("%d"), A.LaneValue)));
			}
			X.Close(TEXT("LaneChangeTarget"));
			X.Close(TEXT("LaneChangeAction"));
			X.Close(TEXT("LateralAction"));
			X.Close(TEXT("PrivateAction"));
			return true;

		case EOSCActionType::AssignRoute:
			X.Open(TEXT("PrivateAction"));
			X.Open(TEXT("RoutingAction"));
			X.Open(TEXT("AssignRouteAction"));
			X.Open(TEXT("Route"), FAttrs().S(TEXT("name"), A.Name).B(TEXT("closed"), false));
			for (const FOSCPosition& W : A.Waypoints)
			{
				X.Open(TEXT("Waypoint"), FAttrs().S(TEXT("routeStrategy"), TEXT("shortest")));
				WritePosition(X, W);
				X.Close(TEXT("Waypoint"));
			}
			X.Close(TEXT("Route"));
			X.Close(TEXT("AssignRouteAction"));
			X.Close(TEXT("RoutingAction"));
			X.Close(TEXT("PrivateAction"));
			return true;

		case EOSCActionType::FollowTrajectory:
			X.Open(TEXT("PrivateAction"));
			X.Open(TEXT("RoutingAction"));
			X.Open(TEXT("FollowTrajectoryAction"));
			X.Open(TEXT("TrajectoryRef"));
			X.Open(TEXT("Trajectory"), FAttrs().S(TEXT("name"), A.Name).B(TEXT("closed"), false));
			X.Open(TEXT("Shape"));
			X.Open(TEXT("Polyline"));
			for (const FOSCTrajectoryVertex& V : A.Vertices)
			{
				X.Open(TEXT("Vertex"), V.bHasTime ? FAttrs().D(TEXT("time"), V.Time) : FAttrs());
				WritePosition(X, V.Position);
				X.Close(TEXT("Vertex"));
			}
			X.Close(TEXT("Polyline"));
			X.Close(TEXT("Shape"));
			X.Close(TEXT("Trajectory"));
			X.Close(TEXT("TrajectoryRef"));
			X.Open(TEXT("TimeReference"));
			if (A.bTimeReference)
			{
				X.Leaf(TEXT("Timing"), FAttrs().S(TEXT("domainAbsoluteRelative"), A.bTimeAbsolute ? TEXT("absolute") : TEXT("relative")).D(TEXT("offset"), A.TimeOffset).D(TEXT("scale"), A.TimeScale));
			}
			else
			{
				X.Leaf(TEXT("None"));
			}
			X.Close(TEXT("TimeReference"));
			X.Leaf(TEXT("TrajectoryFollowingMode"), FAttrs().S(TEXT("followingMode"), TEXT("position")));
			X.Close(TEXT("FollowTrajectoryAction"));
			X.Close(TEXT("RoutingAction"));
			X.Close(TEXT("PrivateAction"));
			return true;

		case EOSCActionType::Unsupported:
		default:
			return false;
		}
	}

	void WriteEntityCondition(FXmlOut& X, const FOSCCondition& C)
	{
		X.Open(TEXT("ByEntityCondition"));
		X.Open(TEXT("TriggeringEntities"), FAttrs().S(TEXT("triggeringEntitiesRule"), C.bAllTriggeringEntities ? TEXT("all") : TEXT("any")));
		for (const FString& E : C.TriggeringEntities)
		{
			X.Leaf(TEXT("EntityRef"), FAttrs().S(TEXT("entityRef"), E));
		}
		X.Close(TEXT("TriggeringEntities"));
		X.Open(TEXT("EntityCondition"));
		switch (C.Type)
		{
		case EOSCConditionType::Speed:
			X.Leaf(TEXT("SpeedCondition"), FAttrs().D(TEXT("value"), C.Value).S(TEXT("rule"), RuleStr(C.Rule)));
			break;
		case EOSCConditionType::RelativeSpeed:
			X.Leaf(TEXT("RelativeSpeedCondition"), FAttrs().S(TEXT("entityRef"), C.EntityRef).D(TEXT("value"), C.Value).S(TEXT("rule"), RuleStr(C.Rule)));
			break;
		case EOSCConditionType::Acceleration:
			X.Leaf(TEXT("AccelerationCondition"), FAttrs().D(TEXT("value"), C.Value).S(TEXT("rule"), RuleStr(C.Rule)));
			break;
		case EOSCConditionType::TraveledDistance:
			X.Leaf(TEXT("TraveledDistanceCondition"), FAttrs().D(TEXT("value"), C.Value));
			break;
		case EOSCConditionType::StandStill:
			X.Leaf(TEXT("StandStillCondition"), FAttrs().D(TEXT("duration"), C.Value));
			break;
		case EOSCConditionType::ReachPosition:
			X.Open(TEXT("ReachPositionCondition"), FAttrs().D(TEXT("tolerance"), C.Tolerance));
			WritePosition(X, C.Position);
			X.Close(TEXT("ReachPositionCondition"));
			break;
		case EOSCConditionType::Distance:
			X.Open(TEXT("DistanceCondition"), FAttrs().D(TEXT("value"), C.Value).B(TEXT("freespace"), C.bFreespace).S(TEXT("rule"), RuleStr(C.Rule)));
			WritePosition(X, C.Position);
			X.Close(TEXT("DistanceCondition"));
			break;
		case EOSCConditionType::RelativeDistance:
			X.Leaf(TEXT("RelativeDistanceCondition"), FAttrs().S(TEXT("entityRef"), C.EntityRef).S(TEXT("relativeDistanceType"), C.RelativeDistanceType)
				.D(TEXT("value"), C.Value).B(TEXT("freespace"), C.bFreespace).S(TEXT("rule"), RuleStr(C.Rule)));
			break;
		case EOSCConditionType::TimeHeadway:
			X.Leaf(TEXT("TimeHeadwayCondition"), FAttrs().S(TEXT("entityRef"), C.EntityRef).D(TEXT("value"), C.Value).B(TEXT("freespace"), C.bFreespace).S(TEXT("rule"), RuleStr(C.Rule)));
			break;
		case EOSCConditionType::Collision:
			X.Open(TEXT("CollisionCondition"));
			X.Leaf(TEXT("EntityRef"), FAttrs().S(TEXT("entityRef"), C.EntityRef));
			X.Close(TEXT("CollisionCondition"));
			break;
		default:
			break;
		}
		X.Close(TEXT("EntityCondition"));
		X.Close(TEXT("ByEntityCondition"));
	}

	bool IsWritable(const FOSCCondition& C)
	{
		return C.Type != EOSCConditionType::Unsupported;
	}

	void WriteCondition(FXmlOut& X, const FOSCCondition& C)
	{
		X.Open(TEXT("Condition"), FAttrs().S(TEXT("name"), C.Name).D(TEXT("delay"), C.Delay).S(TEXT("conditionEdge"), EdgeStr(C.Edge)));
		switch (C.Type)
		{
		case EOSCConditionType::SimulationTime:
			X.Open(TEXT("ByValueCondition"));
			X.Leaf(TEXT("SimulationTimeCondition"), FAttrs().D(TEXT("value"), C.Value).S(TEXT("rule"), RuleStr(C.Rule)));
			X.Close(TEXT("ByValueCondition"));
			break;
		case EOSCConditionType::StoryboardElementState:
			X.Open(TEXT("ByValueCondition"));
			X.Leaf(TEXT("StoryboardElementStateCondition"), FAttrs().S(TEXT("storyboardElementType"), C.ElementType).S(TEXT("storyboardElementRef"), C.ElementRef).S(TEXT("state"), C.ElementState));
			X.Close(TEXT("ByValueCondition"));
			break;
		case EOSCConditionType::Parameter:
			X.Open(TEXT("ByValueCondition"));
			X.Leaf(TEXT("ParameterCondition"), FAttrs().S(TEXT("parameterRef"), C.ParameterRef).S(TEXT("value"), C.StringValue).S(TEXT("rule"), RuleStr(C.Rule)));
			X.Close(TEXT("ByValueCondition"));
			break;
		default:
			WriteEntityCondition(X, C);
			break;
		}
		X.Close(TEXT("Condition"));
	}

	/** Writes `<Tag>` with its condition groups. Empty triggers become `<Tag />`. */
	void WriteTrigger(FXmlOut& X, const TCHAR* Tag, const FOSCTrigger& T)
	{
		bool bAny = false;
		for (const FOSCConditionGroup& G : T.Groups)
		{
			for (const FOSCCondition& C : G.Conditions)
			{
				bAny |= IsWritable(C);
			}
		}
		if (!bAny)
		{
			X.Leaf(Tag);
			return;
		}
		X.Open(Tag);
		for (const FOSCConditionGroup& G : T.Groups)
		{
			bool bHas = false;
			for (const FOSCCondition& C : G.Conditions)
			{
				bHas |= IsWritable(C);
			}
			if (!bHas)
			{
				continue;
			}
			X.Open(TEXT("ConditionGroup"));
			for (const FOSCCondition& C : G.Conditions)
			{
				if (IsWritable(C))
				{
					WriteCondition(X, C);
				}
			}
			X.Close(TEXT("ConditionGroup"));
		}
		X.Close(Tag);
	}

	void WriteBoundingBox(FXmlOut& X, const FOSCEntity& E)
	{
		X.Open(TEXT("BoundingBox"));
		X.Leaf(TEXT("Center"), FAttrs().D(TEXT("x"), E.CenterX).D(TEXT("y"), E.CenterY).D(TEXT("z"), E.CenterZ));
		X.Leaf(TEXT("Dimensions"), FAttrs().D(TEXT("width"), E.Width).D(TEXT("length"), E.Length).D(TEXT("height"), E.Height));
		X.Close(TEXT("BoundingBox"));
	}

	void WriteEntity(FXmlOut& X, const FOSCEntity& E)
	{
		X.Open(TEXT("ScenarioObject"), FAttrs().S(TEXT("name"), E.Name));
		const FString TypeName = E.CatalogEntry.IsEmpty() ? E.Name : E.CatalogEntry;
		switch (E.Kind)
		{
		case EOSCEntityKind::Pedestrian:
			X.Open(TEXT("Pedestrian"), FAttrs().S(TEXT("model"), E.Model3d.IsEmpty() ? TEXT("pedestrian") : E.Model3d).D(TEXT("mass"), 80.0)
				.S(TEXT("name"), TypeName).S(TEXT("pedestrianCategory"), E.Category.IsEmpty() ? TEXT("pedestrian") : E.Category));
			WriteBoundingBox(X, E);
			X.Leaf(TEXT("Properties"));
			X.Close(TEXT("Pedestrian"));
			break;
		case EOSCEntityKind::MiscObject:
		case EOSCEntityKind::External:
			X.Open(TEXT("MiscObject"), FAttrs().S(TEXT("miscObjectCategory"), E.Category.IsEmpty() ? TEXT("none") : E.Category).D(TEXT("mass"), 0.0).S(TEXT("name"), TypeName));
			WriteBoundingBox(X, E);
			X.Leaf(TEXT("Properties"));
			X.Close(TEXT("MiscObject"));
			break;
		case EOSCEntityKind::Vehicle:
		default:
		{
			FAttrs A;
			A.S(TEXT("name"), TypeName).S(TEXT("vehicleCategory"), E.Category.IsEmpty() ? TEXT("car") : E.Category);
			if (!E.Model3d.IsEmpty())
			{
				A.S(TEXT("model3d"), E.Model3d);
			}
			X.Open(TEXT("Vehicle"), A);
			WriteBoundingBox(X, E);
			X.Leaf(TEXT("Performance"), FAttrs().D(TEXT("maxSpeed"), E.MaxSpeed).D(TEXT("maxAcceleration"), E.MaxAcceleration).D(TEXT("maxDeceleration"), E.MaxDeceleration));
			// The schema requires axles; nominal passenger-car values are written since the model has none.
			X.Open(TEXT("Axles"));
			X.Leaf(TEXT("FrontAxle"), FAttrs().D(TEXT("maxSteering"), 0.5).D(TEXT("wheelDiameter"), 0.8).D(TEXT("trackWidth"), 1.68).D(TEXT("positionX"), 2.98).D(TEXT("positionZ"), 0.4));
			X.Leaf(TEXT("RearAxle"), FAttrs().D(TEXT("maxSteering"), 0.0).D(TEXT("wheelDiameter"), 0.8).D(TEXT("trackWidth"), 1.68).D(TEXT("positionX"), 0.0).D(TEXT("positionZ"), 0.4));
			X.Close(TEXT("Axles"));
			X.Leaf(TEXT("Properties"));
			X.Close(TEXT("Vehicle"));
			break;
		}
		}
		X.Close(TEXT("ScenarioObject"));
	}
}

FString FOpenScenarioWriter::Write(const FOSCScenario& S)
{
	FXmlOut X;
	X.Raw(TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
	X.Open(TEXT("OpenSCENARIO"));
	X.Leaf(TEXT("FileHeader"), FAttrs().I(TEXT("revMajor"), 1).I(TEXT("revMinor"), 1).S(TEXT("date"), S.Date).S(TEXT("description"), S.Description).S(TEXT("author"), S.Author));

	if (S.ParameterDeclarations.Num() > 0)
	{
		X.Open(TEXT("ParameterDeclarations"));
		for (const FOSCParameterDeclaration& P : S.ParameterDeclarations)
		{
			X.Leaf(TEXT("ParameterDeclaration"), FAttrs().S(TEXT("name"), P.Name).S(TEXT("parameterType"), P.Type).S(TEXT("value"), P.Value));
		}
		X.Close(TEXT("ParameterDeclarations"));
	}

	X.Leaf(TEXT("CatalogLocations"));
	if (S.RoadNetworkFile.IsEmpty())
	{
		X.Leaf(TEXT("RoadNetwork"));
	}
	else
	{
		X.Open(TEXT("RoadNetwork"));
		X.Leaf(TEXT("LogicFile"), FAttrs().S(TEXT("filepath"), S.RoadNetworkFile));
		X.Close(TEXT("RoadNetwork"));
	}

	X.Open(TEXT("Entities"));
	for (const FOSCEntity& E : S.Entities)
	{
		WriteEntity(X, E);
	}
	X.Close(TEXT("Entities"));

	X.Open(TEXT("Storyboard"));
	X.Open(TEXT("Init"));
	X.Open(TEXT("Actions"));
	for (const FOSCInitActions& G : S.InitActions)
	{
		X.Open(TEXT("Private"), FAttrs().S(TEXT("entityRef"), G.EntityRef));
		for (const FOSCAction& A : G.Actions)
		{
			WritePrivateAction(X, A);
		}
		X.Close(TEXT("Private"));
	}
	X.Close(TEXT("Actions"));
	X.Close(TEXT("Init"));

	for (const FOSCStory& Story : S.Stories)
	{
		X.Open(TEXT("Story"), FAttrs().S(TEXT("name"), Story.Name));
		for (const FOSCAct& Act : Story.Acts)
		{
			X.Open(TEXT("Act"), FAttrs().S(TEXT("name"), Act.Name));
			for (const FOSCManeuverGroup& MG : Act.ManeuverGroups)
			{
				X.Open(TEXT("ManeuverGroup"), FAttrs().I(TEXT("maximumExecutionCount"), FMath::Max(1, MG.MaxExecutions)).S(TEXT("name"), MG.Name));
				X.Open(TEXT("Actors"), FAttrs().B(TEXT("selectTriggeringEntities"), false));
				for (const FString& Actor : MG.Actors)
				{
					X.Leaf(TEXT("EntityRef"), FAttrs().S(TEXT("entityRef"), Actor));
				}
				X.Close(TEXT("Actors"));
				for (const FOSCManeuver& Man : MG.Maneuvers)
				{
					X.Open(TEXT("Maneuver"), FAttrs().S(TEXT("name"), Man.Name));
					for (const FOSCEvent& Ev : Man.Events)
					{
						X.Open(TEXT("Event"), FAttrs().S(TEXT("name"), Ev.Name).S(TEXT("priority"), PriorityStr(Ev.Priority)).I(TEXT("maximumExecutionCount"), FMath::Max(1, Ev.MaxExecutions)));
						for (const FOSCAction& A : Ev.Actions)
						{
							if (A.Type == EOSCActionType::Unsupported)
							{
								continue;
							}
							X.Open(TEXT("Action"), FAttrs().S(TEXT("name"), A.Name));
							WritePrivateAction(X, A);
							X.Close(TEXT("Action"));
						}
						WriteTrigger(X, TEXT("StartTrigger"), Ev.StartTrigger);
						X.Close(TEXT("Event"));
					}
					X.Close(TEXT("Maneuver"));
				}
				X.Close(TEXT("ManeuverGroup"));
			}
			WriteTrigger(X, TEXT("StartTrigger"), Act.StartTrigger);
			if (Act.StopTrigger.bPresent)
			{
				WriteTrigger(X, TEXT("StopTrigger"), Act.StopTrigger);
			}
			X.Close(TEXT("Act"));
		}
		X.Close(TEXT("Story"));
	}
	WriteTrigger(X, TEXT("StopTrigger"), S.StopTrigger);
	X.Close(TEXT("Storyboard"));
	X.Close(TEXT("OpenSCENARIO"));
	return X.Buf;
}
