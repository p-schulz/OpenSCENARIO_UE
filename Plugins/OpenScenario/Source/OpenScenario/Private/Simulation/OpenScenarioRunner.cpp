#include "Simulation/OpenScenarioRunner.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Simulation/OpenScenarioEntityActor.h"
#include "OpenScenarioCoordinates.h"
#include "OpenScenarioModule.h"
#include "Engine/World.h"
#include "Misc/DefaultValueHelper.h"

namespace
{
	constexpr double kPi = UE_DOUBLE_PI;

	/** Global actions have no acting entity. */
	bool IsGlobalAction(EOSCActionType Type)
	{
		return Type == EOSCActionType::Traffic || Type == EOSCActionType::TrafficSignalState || Type == EOSCActionType::TrafficSignalController;
	}
	/** Spacing of the look-ahead samples (m). */
	constexpr double LookAheadStep = 2.0;

	/** Lane ids skip 0: moving `Delta` lanes from `Lane` crosses the centre without landing on it. */
	int32 ShiftLane(int32 Lane, int32 Delta)
	{
		int32 R = Lane + Delta;
		if (Lane < 0 && R >= 0) { R += 1; }
		else if (Lane > 0 && R <= 0) { R -= 1; }
		return R;
	}

	struct FObb2D
	{
		double Cx, Cy, Ax, Ay, Bx, By, Hx, Hy;
	};

	FObb2D MakeBox(const FOSCEntityState& E)
	{
		const double C = FMath::Cos(E.Heading);
		const double S = FMath::Sin(E.Heading);
		FObb2D B;
		B.Cx = E.X + C * E.Def.CenterX - S * E.Def.CenterY;
		B.Cy = E.Y + S * E.Def.CenterX + C * E.Def.CenterY;
		B.Ax = C; B.Ay = S;
		B.Bx = -S; B.By = C;
		B.Hx = 0.5 * E.Def.Length;
		B.Hy = 0.5 * E.Def.Width;
		return B;
	}
}

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

bool UOpenScenarioRunner::Initialize(UWorld* InWorld, UOpenScenarioAsset* InAsset)
{
	Asset = InAsset;
	World = InWorld;
	if (!Asset || !Asset->IsScenarioValid())
	{
		UE_LOG(LogOpenScenario, Error, TEXT("Cannot start scenario: asset is missing or failed to parse."));
		return false;
	}

	Scenario = Asset->GetScenario();
	Map = Asset->ResolveRoadNetwork();
	if (!Map.IsValid() && !Scenario.RoadNetworkFile.IsEmpty())
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("Road network '%s' could not be loaded; lane/road positions and routing are unavailable."), *Scenario.RoadNetworkFile);
	}

	Entities.Reset();
	EntityIndex.Reset();
	Instances.Reset();
	EntityActors.Reset();
	Generators.Reset();
	LaneSegments.Reset();
	LaneGrid.Reset();
	bTrafficIndexBuilt = false;
	NextGeneratorId = 0;
	TrafficCounter = 0;
	Random.Initialize(TrafficSettings.RandomSeed);
	for (const FOSCEntity& Def : Scenario.Entities)
	{
		FOSCEntityState State;
		State.Name = Def.Name;
		State.Def = Def;
		EntityIndex.Add(Def.Name, Entities.Add(MoveTemp(State)));
	}
	if (bSpawnActors && World)
	{
		for (FOSCEntityState& E : Entities)
		{
			SpawnEntityActor(E);
		}
	}

	BuildElementIndex();
	BuildSignalTable();

	SimTime = 0.0;
	CurrentDt = 0.0;
	Tick = 0;
	bRunning = true;
	bFinished = false;

	RunInitActions();
	UpdateSignals(0.0);
	UpdateStoryboard();
	SyncActors();
	return true;
}

void UOpenScenarioRunner::Stop(bool bDestroyActors)
{
	bRunning = false;
	if (bDestroyActors)
	{
		for (TPair<FString, TObjectPtr<AActor>>& Pair : EntityActors)
		{
			if (Pair.Value)
			{
				Pair.Value->Destroy();
			}
		}
		EntityActors.Reset();
	}
}

void UOpenScenarioRunner::Finish()
{
	if (bFinished)
	{
		return;
	}
	bRunning = false;
	bFinished = true;
	UE_LOG(LogOpenScenario, Log, TEXT("Scenario finished at t=%.2fs."), SimTime);
	OnFinished.Broadcast();
}

AActor* UOpenScenarioRunner::GetEntityActor(const FString& EntityName) const
{
	const TObjectPtr<AActor>* A = EntityActors.Find(EntityName);
	return A ? A->Get() : nullptr;
}

const FOSCEntityState* UOpenScenarioRunner::GetEntityState(const FString& EntityName) const
{
	return FindEntity(EntityName);
}

FOSCEntityState* UOpenScenarioRunner::FindEntity(const FString& Name)
{
	const int32* Idx = EntityIndex.Find(Name);
	return Idx ? &Entities[*Idx] : nullptr;
}

const FOSCEntityState* UOpenScenarioRunner::FindEntity(const FString& Name) const
{
	const int32* Idx = EntityIndex.Find(Name);
	return Idx ? &Entities[*Idx] : nullptr;
}

void UOpenScenarioRunner::SpawnEntityActor(FOSCEntityState& E)
{
	// Precedence: actor per-entity override, asset per-entity mapping, actor per-kind default,
	// asset per-kind mapping, built-in box actor.
	TSubclassOf<AActor> Class;
	if (const TSubclassOf<AActor>* Override = EntityClassOverrides.Find(E.Name))
	{
		Class = *Override;
	}
	if (!Class && Asset)
	{
		Class = Asset->FindEntityActorClass(E.Name);
	}
	if (!Class)
	{
		switch (E.Def.Kind)
		{
		case EOSCEntityKind::Pedestrian: Class = DefaultPedestrianClass; break;
		case EOSCEntityKind::MiscObject: Class = DefaultMiscObjectClass; break;
		default: Class = DefaultVehicleClass; break;
		}
	}
	if (!Class && Asset)
	{
		Class = Asset->FindKindActorClass(E.Def.Kind);
	}
	if (!Class)
	{
		Class = AOpenScenarioEntityActor::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
	if (!World->IsGameWorld())
	{
		// Simulating in the editor viewport: entity actors must not be saved or recorded for undo.
		Params.bTemporaryEditorActor = true;
		Params.ObjectFlags |= RF_Transient;
	}
#endif
	AActor* Actor = World->SpawnActor(Class.Get(), &Origin, Params);
	if (!Actor)
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("Failed to spawn actor for entity '%s'."), *E.Name);
		return;
	}
#if WITH_EDITOR
	Actor->SetActorLabel(E.Name);
#endif
	if (AOpenScenarioEntityActor* EntityActor = Cast<AOpenScenarioEntityActor>(Actor))
	{
		EntityActor->ConfigureFromEntity(E.Def);
	}
	EntityActors.Add(E.Name, Actor);
	OnEntitySpawned.Broadcast(E.Name, Actor);
}

void UOpenScenarioRunner::SyncActors()
{
	for (FOSCEntityState& E : Entities)
	{
		if (!E.bActive)
		{
			continue;
		}
		TObjectPtr<AActor>* Found = EntityActors.Find(E.Name);
		if (!Found || !Found->Get())
		{
			continue;
		}
		const FVector Local = OpenScenarioCoords::ToUnrealLocation(E.X, E.Y, E.Z);
		const FQuat LocalRot(FRotator(0.0, OpenScenarioCoords::HeadingToYawDegrees(E.Heading), 0.0));
		const FVector Loc = Origin.TransformPosition(Local);
		const FQuat Rot = Origin.GetRotation() * LocalRot;
		(*Found)->SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
		E.bTeleported = false;
	}
}

void UOpenScenarioRunner::BuildElementIndex()
{
	ElementIndex.Reset();
	auto Add = [this](const TCHAR* Type, const FString& Name, FOSCRuntime& R)
	{
		ElementIndex.Add(FString(Type).ToLower() + TEXT("|") + Name, &R);
	};
	for (FOSCStory& Story : Scenario.Stories)
	{
		Add(TEXT("story"), Story.Name, Story.Runtime);
		for (FOSCAct& Act : Story.Acts)
		{
			Add(TEXT("act"), Act.Name, Act.Runtime);
			for (FOSCManeuverGroup& MG : Act.ManeuverGroups)
			{
				Add(TEXT("maneuverGroup"), MG.Name, MG.Runtime);
				for (FOSCManeuver& Man : MG.Maneuvers)
				{
					Add(TEXT("maneuver"), Man.Name, Man.Runtime);
					for (FOSCEvent& Ev : Man.Events)
					{
						Add(TEXT("event"), Ev.Name, Ev.Runtime);
						for (FOSCAction& Action : Ev.Actions)
						{
							Add(TEXT("action"), Action.Name, Action.Runtime);
						}
					}
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Step
// ------------------------------------------------------------------------------------------------

void UOpenScenarioRunner::Step(double Dt)
{
	if (!bRunning || Dt <= 0.0)
	{
		return;
	}
	CurrentDt = Dt;
	++Tick;
	SimTime += Dt;

	TArray<double> PrevSpeed;
	PrevSpeed.Reserve(Entities.Num());
	for (const FOSCEntityState& E : Entities)
	{
		PrevSpeed.Add(E.Speed);
	}

	for (int32 i = 0; i < Instances.Num(); ++i)
	{
		if (!Instances[i].bDone)
		{
			UpdateInstance(Instances[i], Dt);
		}
	}
	UpdateSignals(Dt);
	UpdateStoryboard();

	UpdateTraffic(Dt);

	for (FOSCEntityState& E : Entities)
	{
		if (!E.bActive)
		{
			continue;
		}
		UpdateDynamics(E, Dt);
		UpdateMotion(E, Dt);
	}
	// Traffic may have added entities during this step; they have no previous speed yet.
	for (int32 i = 0; i < PrevSpeed.Num() && i < Entities.Num(); ++i)
	{
		Entities[i].Accel = (Entities[i].Speed - PrevSpeed[i]) / Dt;
	}
	SyncActors();
}

// ------------------------------------------------------------------------------------------------
// Storyboard state machine
// ------------------------------------------------------------------------------------------------

void UOpenScenarioRunner::StartElement(FOSCRuntime& R)
{
	R.State = EOSCElementState::Running;
	R.StartTick = Tick;
}

void UOpenScenarioRunner::EndElement(FOSCRuntime& R)
{
	R.State = EOSCElementState::Complete;
	R.EndTick = Tick;
}

void UOpenScenarioRunner::StopElement(FOSCRuntime& R)
{
	R.State = EOSCElementState::Complete;
	R.StopTick = Tick;
}

void UOpenScenarioRunner::UpdateStoryboard()
{
	for (FOSCStory& Story : Scenario.Stories)
	{
		if (Story.Runtime.State == EOSCElementState::Standby)
		{
			StartElement(Story.Runtime);
		}
		if (Story.Runtime.State == EOSCElementState::Running)
		{
			bool bAllComplete = true;
			for (FOSCAct& Act : Story.Acts)
			{
				UpdateAct(Act);
				if (Act.Runtime.State != EOSCElementState::Complete)
				{
					bAllComplete = false;
				}
			}
			if (bAllComplete)
			{
				EndElement(Story.Runtime);
			}
		}
	}

	if (Scenario.StopTrigger.bPresent && EvaluateTrigger(Scenario.StopTrigger, false))
	{
		Finish();
		return;
	}

	// A scenario with a StopTrigger runs until that trigger fires (as the standard requires).
	if (bStopWhenStoryboardComplete && !Scenario.StopTrigger.bPresent && Scenario.Stories.Num() > 0 && !HasActiveGenerators())
	{
		bool bAllComplete = true;
		for (const FOSCStory& Story : Scenario.Stories)
		{
			bAllComplete &= (Story.Runtime.State == EOSCElementState::Complete);
		}
		if (bAllComplete)
		{
			for (const FOSCActionInstance& Inst : Instances)
			{
				if (!Inst.bDone)
				{
					bAllComplete = false;
					break;
				}
			}
		}
		if (bAllComplete)
		{
			Finish();
		}
	}
}

void UOpenScenarioRunner::BeginManeuverGroup(FOSCManeuverGroup& MG, bool bFirst)
{
	if (bFirst)
	{
		StartElement(MG.Runtime);
	}
	for (FOSCManeuver& Man : MG.Maneuvers)
	{
		StartElement(Man.Runtime);
		for (FOSCEvent& Ev : Man.Events)
		{
			Ev.Runtime.State = EOSCElementState::Standby;
			Ev.Runtime.Executions = 0;
			Ev.Runtime.Instances.Reset();
			for (FOSCAction& Action : Ev.Actions)
			{
				Action.Runtime.State = EOSCElementState::Standby;
			}
		}
	}
}

void UOpenScenarioRunner::UpdateAct(FOSCAct& Act)
{
	FOSCRuntime& R = Act.Runtime;
	if (R.State == EOSCElementState::Standby)
	{
		if (!EvaluateTrigger(Act.StartTrigger, true))
		{
			return;
		}
		StartElement(R);
		for (FOSCManeuverGroup& MG : Act.ManeuverGroups)
		{
			MG.Runtime.Executions = 0;
			BeginManeuverGroup(MG, true);
		}
	}

	if (R.State == EOSCElementState::Running)
	{
		if (Act.StopTrigger.bPresent && EvaluateTrigger(Act.StopTrigger, false))
		{
			StopAct(Act);
			return;
		}
		bool bAllComplete = true;
		for (FOSCManeuverGroup& MG : Act.ManeuverGroups)
		{
			UpdateManeuverGroup(MG);
			if (MG.Runtime.State != EOSCElementState::Complete)
			{
				bAllComplete = false;
			}
		}
		if (bAllComplete)
		{
			EndElement(R);
		}
	}
}

void UOpenScenarioRunner::UpdateManeuverGroup(FOSCManeuverGroup& MG)
{
	if (MG.Runtime.State != EOSCElementState::Running)
	{
		return;
	}
	bool bAllManeuvers = true;
	for (FOSCManeuver& Man : MG.Maneuvers)
	{
		if (Man.Runtime.State == EOSCElementState::Running)
		{
			bool bAllEvents = true;
			for (FOSCEvent& Ev : Man.Events)
			{
				UpdateEvent(MG, Man, Ev);
				if (Ev.Runtime.State != EOSCElementState::Complete)
				{
					bAllEvents = false;
				}
			}
			if (bAllEvents)
			{
				EndElement(Man.Runtime);
			}
		}
		if (Man.Runtime.State != EOSCElementState::Complete)
		{
			bAllManeuvers = false;
		}
	}
	if (bAllManeuvers)
	{
		++MG.Runtime.Executions;
		if (MG.Runtime.Executions < MG.MaxExecutions)
		{
			BeginManeuverGroup(MG, false);
		}
		else
		{
			EndElement(MG.Runtime);
		}
	}
}

bool UOpenScenarioRunner::AllInstancesDone(const FOSCEvent& Ev) const
{
	for (const int32 Idx : Ev.Runtime.Instances)
	{
		if (!Instances[Idx].bDone)
		{
			return false;
		}
	}
	return true;
}

void UOpenScenarioRunner::UpdateEvent(FOSCManeuverGroup& MG, FOSCManeuver& Man, FOSCEvent& Ev)
{
	FOSCRuntime& R = Ev.Runtime;
	if (R.State == EOSCElementState::Standby)
	{
		if (!EvaluateTrigger(Ev.StartTrigger, true))
		{
			return;
		}
		bool bOthersRunning = false;
		for (FOSCEvent& Other : Man.Events)
		{
			if (&Other != &Ev && Other.Runtime.State == EOSCElementState::Running)
			{
				bOthersRunning = true;
			}
		}
		if (Ev.Priority == EOSCPriority::Skip && bOthersRunning)
		{
			R.State = EOSCElementState::Complete;
			R.SkipTick = Tick;
			return;
		}
		if (Ev.Priority == EOSCPriority::Override)
		{
			for (FOSCEvent& Other : Man.Events)
			{
				if (&Other != &Ev && Other.Runtime.State == EOSCElementState::Running)
				{
					StopEvent(Other);
				}
			}
		}
		StartEvent(MG, Ev);
	}

	if (R.State == EOSCElementState::Running && AllInstancesDone(Ev))
	{
		++R.Executions;
		EndElement(R);
		if (R.Executions < Ev.MaxExecutions)
		{
			R.State = EOSCElementState::Standby;
		}
	}
}

void UOpenScenarioRunner::StartEvent(FOSCManeuverGroup& MG, FOSCEvent& Ev)
{
	StartElement(Ev.Runtime);
	Ev.Runtime.Instances.Reset();
	for (FOSCAction& Action : Ev.Actions)
	{
		if (IsGlobalAction(Action.Type))
		{
			// Global action: one instance, independent of the maneuver group's actors.
			Ev.Runtime.Instances.Add(CreateInstance(Action, FString()));
			continue;
		}
		for (const FString& Actor : MG.Actors)
		{
			Ev.Runtime.Instances.Add(CreateInstance(Action, Actor));
		}
	}
}

void UOpenScenarioRunner::StopEvent(FOSCEvent& Ev)
{
	for (const int32 Idx : Ev.Runtime.Instances)
	{
		FOSCActionInstance& Inst = Instances[Idx];
		if (!Inst.bDone)
		{
			CancelInstance(Inst);
		}
	}
	for (FOSCAction& Action : Ev.Actions)
	{
		StopElement(Action.Runtime);
	}
	StopElement(Ev.Runtime);
}

void UOpenScenarioRunner::StopAct(FOSCAct& Act)
{
	for (FOSCManeuverGroup& MG : Act.ManeuverGroups)
	{
		for (FOSCManeuver& Man : MG.Maneuvers)
		{
			for (FOSCEvent& Ev : Man.Events)
			{
				if (Ev.Runtime.State == EOSCElementState::Running)
				{
					StopEvent(Ev);
				}
				else if (Ev.Runtime.State == EOSCElementState::Standby)
				{
					StopElement(Ev.Runtime);
				}
			}
			StopElement(Man.Runtime);
		}
		StopElement(MG.Runtime);
	}
	StopElement(Act.Runtime);
}

// ------------------------------------------------------------------------------------------------
// Triggers
// ------------------------------------------------------------------------------------------------

bool UOpenScenarioRunner::Compare(double Lhs, EOSCRule Rule, double Rhs)
{
	constexpr double Eps = 1e-6;
	switch (Rule)
	{
	case EOSCRule::EqualTo: return FMath::Abs(Lhs - Rhs) <= Eps;
	case EOSCRule::NotEqualTo: return FMath::Abs(Lhs - Rhs) > Eps;
	case EOSCRule::GreaterThan: return Lhs > Rhs;
	case EOSCRule::LessThan: return Lhs < Rhs;
	case EOSCRule::GreaterOrEqual: return Lhs >= Rhs - Eps;
	case EOSCRule::LessOrEqual: return Lhs <= Rhs + Eps;
	}
	return false;
}

bool UOpenScenarioRunner::EvaluateTrigger(FOSCTrigger& Trigger, bool bEmptyResult)
{
	if (!Trigger.bPresent || Trigger.Groups.Num() == 0)
	{
		return bEmptyResult;
	}
	bool bAny = false;
	for (FOSCConditionGroup& Group : Trigger.Groups)
	{
		// Evaluate every condition (no short-circuit) so edge/delay bookkeeping stays current.
		bool bAll = Group.Conditions.Num() > 0;
		for (FOSCCondition& Cond : Group.Conditions)
		{
			if (!EvaluateCondition(Cond))
			{
				bAll = false;
			}
		}
		bAny |= bAll;
	}
	return bAny;
}

bool UOpenScenarioRunner::EvaluateCondition(FOSCCondition& Cond)
{
	constexpr double Eps = 1e-9;
	FOSCConditionRuntime& R = Cond.Runtime;
	const bool bRaw = EvaluateRaw(Cond);
	bool bResult = false;

	if (Cond.Edge == EOSCEdge::None)
	{
		if (bRaw)
		{
			if (!R.bPrev)
			{
				R.TrueSince = SimTime;
			}
			bResult = (SimTime - R.TrueSince + Eps >= Cond.Delay);
		}
		else
		{
			R.TrueSince = -1.0;
		}
	}
	else
	{
		const bool bRising = bRaw && !R.bPrev;
		const bool bFalling = !bRaw && R.bPrev;
		const bool bEdge = (Cond.Edge == EOSCEdge::Rising && bRising)
			|| (Cond.Edge == EOSCEdge::Falling && bFalling)
			|| (Cond.Edge == EOSCEdge::RisingOrFalling && (bRising || bFalling));
		if (bEdge)
		{
			R.PendingAt = SimTime + Cond.Delay;
		}
		if (R.PendingAt >= 0.0 && SimTime + Eps >= R.PendingAt)
		{
			bResult = true;
			R.PendingAt = -1.0;
		}
	}
	R.bPrev = bRaw;
	return bResult;
}

bool UOpenScenarioRunner::EvaluateElementState(const FOSCCondition& Cond) const
{
	const FOSCRuntime* const* Found = ElementIndex.Find(Cond.ElementType.ToLower() + TEXT("|") + Cond.ElementRef);
	if (!Found)
	{
		return false;
	}
	const FOSCRuntime& R = **Found;
	const FString& S = Cond.ElementState;
	auto Is = [&S](const TCHAR* Name) { return S.Equals(Name, ESearchCase::IgnoreCase); };

	if (Is(TEXT("standbyState"))) { return R.State == EOSCElementState::Standby; }
	if (Is(TEXT("runningState"))) { return R.State == EOSCElementState::Running; }
	if (Is(TEXT("completeState"))) { return R.State == EOSCElementState::Complete; }
	// Transitions are visible for the tick they happen in and the following one.
	if (Is(TEXT("startTransition"))) { return R.StartTick >= Tick - 1; }
	if (Is(TEXT("endTransition"))) { return R.EndTick >= Tick - 1; }
	if (Is(TEXT("stopTransition"))) { return R.StopTick >= Tick - 1; }
	if (Is(TEXT("skipTransition"))) { return R.SkipTick >= Tick - 1; }
	return false;
}

bool UOpenScenarioRunner::EvaluateRaw(const FOSCCondition& Cond)
{
	switch (Cond.Type)
	{
	case EOSCConditionType::Unsupported:
		return false;
	case EOSCConditionType::SimulationTime:
		return Compare(SimTime, Cond.Rule, Cond.Value);
	case EOSCConditionType::StoryboardElementState:
		return EvaluateElementState(Cond);
	case EOSCConditionType::TrafficSignal:
	{
		EOpenScenarioSignalState State;
		return GetSignalState(Cond.SignalId, State) && State == ParseSignalState(Cond.SignalState);
	}
	case EOSCConditionType::Parameter:
	{
		const FString* Value = Scenario.Parameters.Find(Cond.ParameterRef);
		if (!Value)
		{
			return false;
		}
		if (FDefaultValueHelper::IsStringValidFloat(*Value) && FDefaultValueHelper::IsStringValidFloat(Cond.StringValue))
		{
			return Compare(FCString::Atod(**Value), Cond.Rule, FCString::Atod(*Cond.StringValue));
		}
		const bool bEqual = Value->Equals(Cond.StringValue, ESearchCase::CaseSensitive);
		return Cond.Rule == EOSCRule::NotEqualTo ? !bEqual : (Cond.Rule == EOSCRule::EqualTo && bEqual);
	}
	default:
		break;
	}

	if (Cond.TriggeringEntities.Num() == 0)
	{
		return false;
	}
	bool bAll = true;
	bool bAny = false;
	for (const FString& Name : Cond.TriggeringEntities)
	{
		const FOSCEntityState* E = FindEntity(Name);
		if (!E)
		{
			bAll = false;
			continue;
		}
		const bool bResult = EvaluateEntityCondition(Cond, *E);
		bAny |= bResult;
		bAll &= bResult;
	}
	return Cond.bAllTriggeringEntities ? bAll : bAny;
}

bool UOpenScenarioRunner::EvaluateEntityCondition(const FOSCCondition& Cond, const FOSCEntityState& E)
{
	auto Ref = [&]() { return FindEntity(Cond.EntityRef); };

	switch (Cond.Type)
	{
	case EOSCConditionType::Speed:
		return Compare(E.Speed, Cond.Rule, Cond.Value);
	case EOSCConditionType::Acceleration:
		return Compare(E.Accel, Cond.Rule, Cond.Value);
	case EOSCConditionType::TraveledDistance:
		return Compare(E.TravelledDistance, Cond.Rule, Cond.Value);
	case EOSCConditionType::StandStill:
		return FMath::Abs(E.Speed) < 0.01;
	case EOSCConditionType::RelativeSpeed:
	{
		const FOSCEntityState* R = Ref();
		return R && Compare(E.Speed - R->Speed, Cond.Rule, Cond.Value);
	}
	case EOSCConditionType::ReachPosition:
	{
		FOSCEntityState Target;
		return ResolvePosition(Cond.Position, Target, false) && FMath::Sqrt(FMath::Square(E.X - Target.X) + FMath::Square(E.Y - Target.Y)) <= Cond.Tolerance;
	}
	case EOSCConditionType::Distance:
	{
		FOSCEntityState Target;
		return ResolvePosition(Cond.Position, Target, false)
			&& Compare(FMath::Sqrt(FMath::Square(E.X - Target.X) + FMath::Square(E.Y - Target.Y)), Cond.Rule, Cond.Value);
	}
	case EOSCConditionType::RelativeDistance:
	{
		const FOSCEntityState* R = Ref();
		if (!R)
		{
			return false;
		}
		const double Dx = E.X - R->X;
		const double Dy = E.Y - R->Y;
		double D;
		if (Cond.RelativeDistanceType.Equals(TEXT("longitudinal"), ESearchCase::IgnoreCase))
		{
			D = FMath::Abs(Dx * FMath::Cos(R->Heading) + Dy * FMath::Sin(R->Heading));
		}
		else if (Cond.RelativeDistanceType.Equals(TEXT("lateral"), ESearchCase::IgnoreCase))
		{
			D = FMath::Abs(-Dx * FMath::Sin(R->Heading) + Dy * FMath::Cos(R->Heading));
		}
		else
		{
			D = FMath::Sqrt(Dx * Dx + Dy * Dy);
		}
		return Compare(D, Cond.Rule, Cond.Value);
	}
	case EOSCConditionType::TimeHeadway:
	{
		const FOSCEntityState* R = Ref();
		if (!R)
		{
			return false;
		}
		const double Dist = FMath::Sqrt(FMath::Square(E.X - R->X) + FMath::Square(E.Y - R->Y));
		const double Headway = E.Speed > 0.01 ? Dist / E.Speed : TNumericLimits<double>::Max();
		return Compare(Headway, Cond.Rule, Cond.Value);
	}
	case EOSCConditionType::Collision:
	{
		const FOSCEntityState* R = Ref();
		return R && R != &E && BoxesOverlap(E, *R);
	}
	default:
		return false;
	}
}

bool UOpenScenarioRunner::BoxesOverlap(const FOSCEntityState& A, const FOSCEntityState& B)
{
	const FObb2D Ba = MakeBox(A);
	const FObb2D Bb = MakeBox(B);
	const double Dx = Bb.Cx - Ba.Cx;
	const double Dy = Bb.Cy - Ba.Cy;

	const double Axes[4][2] = { { Ba.Ax, Ba.Ay }, { Ba.Bx, Ba.By }, { Bb.Ax, Bb.Ay }, { Bb.Bx, Bb.By } };
	for (const double* Axis : Axes)
	{
		const double Ra = Ba.Hx * FMath::Abs(Axis[0] * Ba.Ax + Axis[1] * Ba.Ay) + Ba.Hy * FMath::Abs(Axis[0] * Ba.Bx + Axis[1] * Ba.By);
		const double Rb = Bb.Hx * FMath::Abs(Axis[0] * Bb.Ax + Axis[1] * Bb.Ay) + Bb.Hy * FMath::Abs(Axis[0] * Bb.Bx + Axis[1] * Bb.By);
		if (FMath::Abs(Dx * Axis[0] + Dy * Axis[1]) > Ra + Rb)
		{
			return false;
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// Actions
// ------------------------------------------------------------------------------------------------

double UOpenScenarioRunner::ShapeFactor(EOSCShape Shape, double P)
{
	P = FMath::Clamp(P, 0.0, 1.0);
	switch (Shape)
	{
	case EOSCShape::Step: return P > 0.0 ? 1.0 : 0.0;
	case EOSCShape::Linear: return P;
	case EOSCShape::Cubic: return P * P * (3.0 - 2.0 * P);
	case EOSCShape::Sinusoidal: return 0.5 * (1.0 - FMath::Cos(kPi * P));
	}
	return P;
}

void UOpenScenarioRunner::RunInitActions()
{
	// Speeds set in Init apply immediately, even under simple dynamics.
	bInitPhase = true;
	// Teleports first so that relative positions and lane bindings are established before speeds.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (FOSCInitActions& Group : Scenario.InitActions)
		{
			for (FOSCAction& Action : Group.Actions)
			{
				const bool bTeleport = Action.Type == EOSCActionType::Teleport;
				if ((Pass == 0) == bTeleport)
				{
					CreateInstance(Action, Group.EntityRef);
				}
			}
		}
	}
	bInitPhase = false;
}

int32 UOpenScenarioRunner::CreateInstance(FOSCAction& Action, const FString& EntityName)
{
	FOSCActionInstance Inst;
	Inst.Def = &Action;
	Inst.StartTime = SimTime;
	const int32* Idx = EntityIndex.Find(EntityName);
	if (Idx)
	{
		Inst.EntityIndex = *Idx;
	}
	else if (IsGlobalAction(Action.Type))
	{
		// Global action without an acting entity.
	}
	else
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("Action '%s' refers to unknown entity '%s'."), *Action.Name, *EntityName);
		Inst.bDone = true;
	}
	const int32 InstanceIdx = Instances.Add(MoveTemp(Inst));
	StartElement(Action.Runtime);
	if (!Instances[InstanceIdx].bDone)
	{
		InitInstance(Instances[InstanceIdx]);
	}
	else
	{
		EndElement(Action.Runtime);
	}
	return InstanceIdx;
}

void UOpenScenarioRunner::FinishInstance(FOSCActionInstance& Inst)
{
	Inst.bDone = true;
	if (Inst.Def)
	{
		EndElement(Inst.Def->Runtime);
	}
}

void UOpenScenarioRunner::EndLaneChange(FOSCEntityState& E, bool bToTarget)
{
	if (!E.bLaneChanging || !Map.IsValid())
	{
		E.bLaneChanging = false;
		return;
	}
	const FOpenDriveRoad* Road = Map->FindRoad(E.RoadId);
	if (Road)
	{
		const int32 Src = Map->ClampLaneId(*Road, E.S, E.LCSourceLane);
		const int32 Tgt = Map->ClampLaneId(*Road, E.S, E.LCTargetLane);
		const double CurrentT = FMath::Lerp(Map->GetLaneCenterT(*Road, E.S, Src), Map->GetLaneCenterT(*Road, E.S, Tgt), E.LCFactor) + E.LaneOffset;
		E.LaneId = bToTarget ? Tgt : Src;
		E.LaneOffset = CurrentT - Map->GetLaneCenterT(*Road, E.S, E.LaneId);
	}
	E.bLaneChanging = false;
	E.LookAhead.bValid = false;
}

void UOpenScenarioRunner::CancelInstance(FOSCActionInstance& Inst)
{
	if (Inst.bDone)
	{
		return;
	}
	if (Inst.EntityIndex != INDEX_NONE && Inst.Def)
	{
		FOSCEntityState& E = Entities[Inst.EntityIndex];
		if (Inst.Def->Type == EOSCActionType::LaneChange)
		{
			EndLaneChange(E, E.LCFactor >= 0.5);
		}
		else if (Inst.Def->Type == EOSCActionType::FollowTrajectory)
		{
			E.bTrajectoryControlled = false;
			AttachToRoad(E);
		}
	}
	Inst.bDone = true;
	if (Inst.Def)
	{
		StopElement(Inst.Def->Runtime);
	}
}

void UOpenScenarioRunner::InitInstance(FOSCActionInstance& Inst)
{
	FOSCAction& A = *Inst.Def;
	if (A.Type == EOSCActionType::Traffic)
	{
		StartTrafficAction(A);
		FinishInstance(Inst);
		return;
	}
	if (A.Type == EOSCActionType::TrafficSignalState || A.Type == EOSCActionType::TrafficSignalController)
	{
		StartSignalAction(A);
		FinishInstance(Inst);
		return;
	}
	FOSCEntityState& E = Entities[Inst.EntityIndex];

	switch (A.Type)
	{
	case EOSCActionType::Teleport:
	{
		FOSCEntityState Target;
		Target.Name = E.Name;
		if (ResolvePosition(A.Position, Target, true))
		{
			E.X = Target.X; E.Y = Target.Y; E.Z = Target.Z; E.Heading = Target.Heading;
			E.bOnRoad = Target.bOnRoad;
			E.bDirForward = Target.bDirForward;
			E.RoadId = Target.RoadId;
			E.S = Target.S;
			E.LaneId = Target.LaneId;
			E.LaneOffset = Target.LaneOffset;
			E.bLaneChanging = false;
			E.Route.Reset();
			E.RouteIndex = 0;
			E.LookAhead.bValid = false;
			E.bTeleported = true;
		}
		else
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("TeleportAction '%s' for '%s': position could not be resolved."), *A.Name, *E.Name);
		}
		FinishInstance(Inst);
		break;
	}
	case EOSCActionType::Speed:
	{
		Inst.V0 = UsesSimpleDynamics(E) ? E.DesiredSpeed : E.Speed;
		Inst.VTarget = A.SpeedValue;
		if (A.bSpeedRelative)
		{
			if (const FOSCEntityState* Ref = FindEntity(A.RefEntity))
			{
				Inst.VTarget = A.bSpeedFactor ? Ref->Speed * A.SpeedValue : Ref->Speed + A.SpeedValue;
			}
			else
			{
				UE_LOG(LogOpenScenario, Warning, TEXT("SpeedAction '%s': reference entity '%s' not found."), *A.Name, *A.RefEntity);
			}
		}
		const double Dv = FMath::Abs(Inst.VTarget - Inst.V0);
		switch (A.Dynamics.Dimension)
		{
		case EOSCDimension::Time: Inst.Duration = A.Dynamics.Value; break;
		case EOSCDimension::Rate: Inst.Duration = A.Dynamics.Value > 1e-9 ? Dv / A.Dynamics.Value : 0.0; break;
		case EOSCDimension::Distance:
		{
			const double Avg = 0.5 * (Inst.V0 + Inst.VTarget);
			Inst.Duration = Avg > 1e-6 ? A.Dynamics.Value / Avg : 0.0;
			break;
		}
		}
		if (A.Dynamics.Shape == EOSCShape::Step || Inst.Duration <= 1e-9)
		{
			SetCommandedSpeed(E, Inst.VTarget);
			// Under simple dynamics the vehicle still has to get there; UpdateSpeedInstance finishes the action.
			if (!UsesSimpleDynamics(E) || bInitPhase)
			{
				FinishInstance(Inst);
			}
			else
			{
				Inst.Duration = 0.0;
			}
		}
		break;
	}
	case EOSCActionType::LaneChange:
	{
		if (!Map.IsValid() || !E.bOnRoad)
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("LaneChangeAction '%s': entity '%s' is not on a road network."), *A.Name, *E.Name);
			FinishInstance(Inst);
			break;
		}
		const FOpenDriveRoad* Road = Map->FindRoad(E.RoadId);
		if (!Road)
		{
			FinishInstance(Inst);
			break;
		}
		int32 Target = A.LaneValue;
		if (A.bLaneRelative)
		{
			const FOSCEntityState* Ref = A.RefEntity.IsEmpty() ? &E : FindEntity(A.RefEntity);
			if (!Ref || !Ref->bOnRoad)
			{
				UE_LOG(LogOpenScenario, Warning, TEXT("LaneChangeAction '%s': reference entity '%s' is not on a road."), *A.Name, *A.RefEntity);
				FinishInstance(Inst);
				break;
			}
			Target = ShiftLane(Ref->LaneId, A.LaneValue);
		}
		Target = Map->ClampLaneId(*Road, E.S, Target);

		E.LookAhead.bValid = false;
		E.bLaneChanging = true;
		E.LCSourceLane = E.LaneId;
		E.LCTargetLane = Target;
		E.LCFactor = 0.0;

		const double Delta = FMath::Abs(Map->GetLaneCenterT(*Road, E.S, Target) - Map->GetLaneCenterT(*Road, E.S, E.LaneId));
		switch (A.Dynamics.Dimension)
		{
		case EOSCDimension::Time: Inst.Duration = A.Dynamics.Value; break;
		case EOSCDimension::Rate: Inst.Duration = A.Dynamics.Value > 1e-9 ? Delta / A.Dynamics.Value : 0.0; break;
		case EOSCDimension::Distance: Inst.LaneDistance = A.Dynamics.Value; break;
		}
		const bool bInstant = A.Dynamics.Shape == EOSCShape::Step || Target == E.LaneId
			|| (A.Dynamics.Dimension != EOSCDimension::Distance && Inst.Duration <= 1e-9)
			|| (A.Dynamics.Dimension == EOSCDimension::Distance && Inst.LaneDistance <= 1e-9);
		if (bInstant)
		{
			E.LCFactor = 1.0;
			EndLaneChange(E, true);
			E.LaneOffset = A.LaneOffset;
			FinishInstance(Inst);
		}
		break;
	}
	case EOSCActionType::AssignRoute:
		AssignRoute(A, E);
		FinishInstance(Inst);
		break;
	case EOSCActionType::FollowTrajectory:
	{
		TArray<FVector> Points;
		TArray<double> Times;
		bool bAllTimed = A.bTimeReference && A.Vertices.Num() > 0;
		for (const FOSCTrajectoryVertex& V : A.Vertices)
		{
			FOSCEntityState Tmp;
			if (!ResolvePosition(V.Position, Tmp, false))
			{
				UE_LOG(LogOpenScenario, Warning, TEXT("FollowTrajectoryAction '%s': vertex position could not be resolved; skipped."), *A.Name);
				continue;
			}
			Points.Add(FVector(Tmp.X, Tmp.Y, Tmp.Z));
			Times.Add(V.Time);
			bAllTimed &= V.bHasTime;
		}
		if (Points.Num() == 0)
		{
			FinishInstance(Inst);
			break;
		}
		Inst.bPathTimed = bAllTimed;
		if (bAllTimed)
		{
			Inst.Path = Points;
			Inst.PathTimes = Times;
		}
		else
		{
			Inst.Path.Add(FVector(E.X, E.Y, E.Z));
			Inst.Path.Append(Points);
		}
		Inst.PathU = 0.0;
		E.bTrajectoryControlled = true;
		E.bOnRoad = false;
		E.bLaneChanging = false;
		break;
	}
	case EOSCActionType::Unsupported:
	default:
		FinishInstance(Inst);
		break;
	}
}

void UOpenScenarioRunner::UpdateInstance(FOSCActionInstance& Inst, double Dt)
{
	if (!Inst.Def || Inst.EntityIndex == INDEX_NONE)
	{
		FinishInstance(Inst);
		return;
	}
	FOSCEntityState& E = Entities[Inst.EntityIndex];
	switch (Inst.Def->Type)
	{
	case EOSCActionType::Speed: UpdateSpeedInstance(Inst, E, Dt); break;
	case EOSCActionType::LaneChange: UpdateLaneChangeInstance(Inst, E, Dt); break;
	case EOSCActionType::FollowTrajectory: UpdateTrajectoryInstance(Inst, E, Dt); break;
	default: FinishInstance(Inst); break;
	}
}

void UOpenScenarioRunner::UpdateSpeedInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt)
{
	Inst.Elapsed += Dt;
	const double P = Inst.Duration > 1e-9 ? Inst.Elapsed / Inst.Duration : 1.0;
	SetCommandedSpeed(E, Inst.V0 + (Inst.VTarget - Inst.V0) * ShapeFactor(Inst.Def->Dynamics.Shape, P));
	if (P >= 1.0)
	{
		SetCommandedSpeed(E, Inst.VTarget);
		// With simple dynamics the action completes once the vehicle has reached the target, or as close
		// as limits, curves or traffic allow.
		if (!UsesSimpleDynamics(E) || FMath::Abs(E.Speed - Inst.VTarget) < 0.2 || E.bSpeedConstrained)
		{
			FinishInstance(Inst);
		}
	}
}

void UOpenScenarioRunner::UpdateLaneChangeInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt)
{
	if (!E.bLaneChanging)
	{
		// Cancelled from outside (e.g. entity was teleported).
		FinishInstance(Inst);
		return;
	}
	double P;
	if (Inst.Def->Dynamics.Dimension == EOSCDimension::Distance)
	{
		Inst.ProgressDistance += FMath::Abs(E.Speed) * Dt;
		P = Inst.ProgressDistance / Inst.LaneDistance;
	}
	else
	{
		Inst.Elapsed += Dt;
		P = Inst.Elapsed / Inst.Duration;
	}
	E.LCFactor = ShapeFactor(Inst.Def->Dynamics.Shape, P);
	if (P >= 1.0)
	{
		E.LCFactor = 1.0;
		EndLaneChange(E, true);
		E.LaneOffset = Inst.Def->LaneOffset;
		FinishInstance(Inst);
	}
}

void UOpenScenarioRunner::UpdateTrajectoryInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt)
{
	FVector Pos = FVector(E.X, E.Y, E.Z);
	double Heading = E.Heading;
	bool bDone = false;
	const FOSCAction& A = *Inst.Def;

	if (Inst.bPathTimed)
	{
		const double Base = A.bTimeAbsolute ? SimTime : (SimTime - Inst.StartTime);
		const double T = A.TimeOffset + A.TimeScale * Base;
		const int32 N = Inst.Path.Num();
		if (T <= Inst.PathTimes[0])
		{
			Pos = Inst.Path[0];
		}
		else if (T >= Inst.PathTimes[N - 1])
		{
			Pos = Inst.Path[N - 1];
			bDone = true;
		}
		else
		{
			int32 I = 0;
			while (I < N - 2 && T >= Inst.PathTimes[I + 1])
			{
				++I;
			}
			const double Span = Inst.PathTimes[I + 1] - Inst.PathTimes[I];
			const double Alpha = Span > 1e-9 ? (T - Inst.PathTimes[I]) / Span : 1.0;
			Pos = FMath::Lerp(Inst.Path[I], Inst.Path[I + 1], Alpha);
			const FVector Seg = Inst.Path[I + 1] - Inst.Path[I];
			if (Seg.SizeSquared2D() > 1e-12)
			{
				Heading = FMath::Atan2(Seg.Y, Seg.X);
				E.Speed = Span > 1e-9 ? Seg.Size() / Span : E.Speed;
			}
		}
	}
	else
	{
		Inst.PathU += FMath::Max(0.0, E.Speed) * Dt;
		double Remaining = Inst.PathU;
		const int32 N = Inst.Path.Num();
		bDone = true;
		for (int32 I = 0; I < N - 1; ++I)
		{
			const FVector Seg = Inst.Path[I + 1] - Inst.Path[I];
			const double Len = Seg.Size();
			if (Remaining <= Len)
			{
				Pos = Len > 1e-9 ? Inst.Path[I] + Seg * (Remaining / Len) : Inst.Path[I + 1];
				if (Seg.SizeSquared2D() > 1e-12)
				{
					Heading = FMath::Atan2(Seg.Y, Seg.X);
				}
				bDone = false;
				break;
			}
			Remaining -= Len;
		}
		if (bDone)
		{
			Pos = Inst.Path.Last();
		}
	}

	E.TravelledDistance += FVector(Pos.X - E.X, Pos.Y - E.Y, Pos.Z - E.Z).Size();
	E.X = Pos.X; E.Y = Pos.Y; E.Z = Pos.Z;
	E.Heading = Heading;

	E.DesiredSpeed = E.Speed;
	if (bDone)
	{
		E.bTrajectoryControlled = false;
		AttachToRoad(E);
		E.LookAhead.bValid = false;
		FinishInstance(Inst);
	}
}

void UOpenScenarioRunner::AssignRoute(const FOSCAction& Action, FOSCEntityState& E)
{
	if (!Map.IsValid() || !E.bOnRoad)
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("AssignRouteAction '%s': entity '%s' is not on a road network."), *Action.Name, *E.Name);
		return;
	}

	TArray<FOpenDriveRouteStep> Route;
	FOpenDriveRouteStep First;
	First.RoadId = E.RoadId;
	First.bForward = E.bDirForward;
	Route.Add(First);

	for (const FOSCPosition& Waypoint : Action.Waypoints)
	{
		FOSCEntityState Tmp;
		if (!ResolvePosition(Waypoint, Tmp, true) || !Tmp.bOnRoad)
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("AssignRouteAction '%s': a waypoint is not on the road network; skipped."), *Action.Name);
			continue;
		}
		if (Tmp.RoadId == Route.Last().RoadId)
		{
			continue;
		}
		TArray<FOpenDriveRouteStep> Segment;
		if (!Map->FindRoute(Route.Last().RoadId, Route.Last().bForward, Tmp.RoadId, Segment))
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("AssignRouteAction '%s': no path from road %s to road %s."), *Action.Name, *Route.Last().RoadId, *Tmp.RoadId);
			break;
		}
		for (int32 i = 1; i < Segment.Num(); ++i)
		{
			Route.Add(Segment[i]);
		}
	}
	E.Route = MoveTemp(Route);
	E.RouteIndex = 0;
	E.LookAhead.bValid = false;
}

// ------------------------------------------------------------------------------------------------
// Positions
// ------------------------------------------------------------------------------------------------

bool UOpenScenarioRunner::AttachToRoad(FOSCEntityState& E) const
{
	if (!Map.IsValid())
	{
		return false;
	}
	FOpenDriveRoadPosition Pos;
	if (!Map->FindClosestRoadPosition(E.X, E.Y, 2.0, Pos))
	{
		E.bOnRoad = false;
		return false;
	}
	const FOpenDriveRoad* Road = Map->FindRoad(Pos.RoadId);
	if (!Road)
	{
		return false;
	}
	double Rx, Ry, Rh;
	Map->EvaluateReferenceLine(*Road, Pos.S, Rx, Ry, Rh);

	E.bOnRoad = true;
	E.RoadId = Pos.RoadId;
	E.S = Pos.S;
	E.LaneId = Pos.LaneId;
	E.LaneOffset = Pos.T - Map->GetLaneCenterT(*Road, Pos.S, Pos.LaneId);
	E.bDirForward = FMath::Cos(E.Heading - Rh) >= 0.0;
	E.Z = Map->EvaluatePose(*Road, Pos.S, Pos.T).Z;
	return true;
}

bool UOpenScenarioRunner::ResolvePosition(const FOSCPosition& P, FOSCEntityState& Out, bool bAttachRoad) const
{
	Out.bOnRoad = false;
	Out.bLaneChanging = false;

	auto Orient = [&P](double Base)
	{
		if (!P.bHasOrientation)
		{
			return Base;
		}
		return P.bOrientationRelative ? Base + P.H : P.H;
	};

	switch (P.Type)
	{
	case EOSCPositionType::World:
		Out.X = P.X; Out.Y = P.Y; Out.Z = P.Z;
		Out.Heading = Orient(0.0);
		if (bAttachRoad)
		{
			AttachToRoad(Out);
		}
		return true;

	case EOSCPositionType::Road:
	case EOSCPositionType::Lane:
	case EOSCPositionType::RelativeLane:
	{
		if (!Map.IsValid())
		{
			return false;
		}
		FString RoadId = P.RoadId;
		double S = P.S;
		int32 Lane = P.LaneId;
		if (P.Type == EOSCPositionType::RelativeLane)
		{
			const FOSCEntityState* Ref = FindEntity(P.EntityRef);
			if (!Ref || !Ref->bOnRoad)
			{
				return false;
			}
			RoadId = Ref->RoadId;
			S = Ref->S + P.DS;
			Lane = ShiftLane(Ref->LaneId, P.DLane);
		}
		const FOpenDriveRoad* Road = Map->FindRoad(RoadId);
		if (!Road)
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("Position refers to unknown road '%s'."), *RoadId);
			return false;
		}
		S = FMath::Clamp(S, 0.0, Road->Length);

		double T;
		double Base;
		if (P.Type == EOSCPositionType::Road)
		{
			T = P.T;
			Lane = Map->FindLaneAt(*Road, S, T);
			Out.LaneOffset = T - Map->GetLaneCenterT(*Road, S, Lane);
		}
		else
		{
			Lane = Map->ClampLaneId(*Road, S, Lane);
			T = Map->GetLaneCenterT(*Road, S, Lane) + P.Offset;
			Out.LaneOffset = P.Offset;
		}
		const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, T);
		Base = Pose.Heading + ((P.Type != EOSCPositionType::Road && Lane > 0) ? kPi : 0.0);

		Out.X = Pose.X; Out.Y = Pose.Y; Out.Z = Pose.Z;
		Out.Heading = Orient(Base);
		Out.bOnRoad = true;
		Out.RoadId = RoadId;
		Out.S = S;
		Out.LaneId = Lane;
		Out.bDirForward = FMath::Cos(Out.Heading - Pose.Heading) >= 0.0;
		return true;
	}

	case EOSCPositionType::RelativeWorld:
	case EOSCPositionType::RelativeObject:
	{
		const FOSCEntityState* Ref = FindEntity(P.EntityRef);
		if (!Ref)
		{
			return false;
		}
		if (P.Type == EOSCPositionType::RelativeWorld)
		{
			Out.X = Ref->X + P.DX;
			Out.Y = Ref->Y + P.DY;
		}
		else
		{
			const double C = FMath::Cos(Ref->Heading);
			const double S = FMath::Sin(Ref->Heading);
			Out.X = Ref->X + P.DX * C - P.DY * S;
			Out.Y = Ref->Y + P.DX * S + P.DY * C;
		}
		Out.Z = Ref->Z + P.DZ;
		Out.Heading = P.bHasOrientation ? (P.bOrientationRelative ? Ref->Heading + P.H : P.H) : Ref->Heading;
		if (bAttachRoad)
		{
			AttachToRoad(Out);
		}
		return true;
	}

	case EOSCPositionType::None:
	default:
		return false;
	}
}

// ------------------------------------------------------------------------------------------------
// Motion
// ------------------------------------------------------------------------------------------------

void UOpenScenarioRunner::UpdatePoseFromRoad(FOSCEntityState& E) const
{
	const FOpenDriveRoad* Road = Map.IsValid() ? Map->FindRoad(E.RoadId) : nullptr;
	if (!Road)
	{
		E.bOnRoad = false;
		return;
	}
	const double S = FMath::Clamp(E.S, 0.0, Road->Length);
	double T;
	if (E.bLaneChanging)
	{
		const int32 Src = Map->ClampLaneId(*Road, S, E.LCSourceLane);
		const int32 Tgt = Map->ClampLaneId(*Road, S, E.LCTargetLane);
		T = FMath::Lerp(Map->GetLaneCenterT(*Road, S, Src), Map->GetLaneCenterT(*Road, S, Tgt), E.LCFactor);
	}
	else
	{
		T = Map->GetLaneCenterT(*Road, S, E.LaneId);
	}
	T += E.LaneOffset;

	const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, T);
	E.X = Pose.X;
	E.Y = Pose.Y;
	E.Z = Pose.Z;
	E.Heading = Pose.Heading + (E.bDirForward ? 0.0 : kPi);
}

void UOpenScenarioRunner::UpdateMotion(FOSCEntityState& E, double Dt)
{
	if (E.bTrajectoryControlled)
	{
		return;
	}
	const double Ds = E.Speed * Dt;
	E.TravelledDistance += FMath::Abs(Ds);

	if (E.bOnRoad && Map.IsValid())
	{
		FRoadCursor Before;
		Before.RoadId = E.RoadId;
		Before.S = E.S;
		Before.LaneId = E.LaneId;
		Before.bForward = E.bDirForward;
		AdvanceOnRoad(E, Ds);
		UpdatePoseFromRoad(E);
		if (Ds > 0.0 && E.bOnRoad && UsesSimpleDynamics(E))
		{
			FRoadCursor After;
			After.RoadId = E.RoadId;
			After.S = E.S;
			After.LaneId = E.LaneId;
			After.bForward = E.bDirForward;
			CollectSignalsBetween(Before, After, 0.0, [&](int32 Index, double)
			{
				const FRuntimeSignal& Sig = Signals[Index];
				if (Sig.Kind == EOSCSignalKind::SpeedLimit && SignalSettings.bObeySpeedSigns) { E.SignLimit = Sig.SpeedLimit; }
				else if (Sig.Kind == EOSCSignalKind::EndSpeedLimit && SignalSettings.bObeySpeedSigns) { E.SignLimit = -1.0; }
			});
		}
	}
	else
	{
		E.X += FMath::Cos(E.Heading) * Ds;
		E.Y += FMath::Sin(E.Heading) * Ds;
	}
}

void UOpenScenarioRunner::AdvanceOnRoad(FOSCEntityState& E, double Ds)
{
	E.bAtDeadEnd = false;
	const bool bReversing = Ds < 0.0;
	double Remaining = FMath::Abs(Ds);
	// Direction of travel along the road (differs from the entity's orientation when reversing).
	bool bTravelForward = bReversing ? !E.bDirForward : E.bDirForward;

	TArray<FOpenDriveSuccessor> Successors;
	for (int32 Guard = 0; Guard < 16 && Remaining > 1e-9; ++Guard)
	{
		const FOpenDriveRoad* Road = Map->FindRoad(E.RoadId);
		if (!Road)
		{
			E.bOnRoad = false;
			return;
		}
		const double Room = bTravelForward ? Road->Length - E.S : E.S;
		if (Remaining <= Room)
		{
			E.S += bTravelForward ? Remaining : -Remaining;
			break;
		}
		Remaining -= Room;
		E.S = bTravelForward ? Road->Length : 0.0;

		Map->GetSuccessors(*Road, bTravelForward, Successors);
		const FOpenDriveSuccessor* Chosen = nullptr;
		bool bFollowsRoute = false;
		if (!bReversing && E.Route.IsValidIndex(E.RouteIndex + 1))
		{
			const FOpenDriveRouteStep& Next = E.Route[E.RouteIndex + 1];
			for (const FOpenDriveSuccessor& S : Successors)
			{
				if (S.RoadId == Next.RoadId && S.bForward == Next.bForward)
				{
					Chosen = &S;
					bFollowsRoute = true;
					break;
				}
			}
		}
		if (!Chosen)
		{
			const int32 SourceLane = E.bLaneChanging ? E.LCTargetLane : E.LaneId;
			for (const FOpenDriveSuccessor& S : Successors)
			{
				if (S.LaneMap.Contains(SourceLane))
				{
					Chosen = &S;
					break;
				}
			}
		}
		if (!Chosen && Successors.Num() > 0)
		{
			Chosen = &Successors[0];
		}
		if (!Chosen)
		{
			if (E.TrafficGenerator != INDEX_NONE && E.Def.Kind == EOSCEntityKind::Pedestrian)
			{
				// Walkers turn around at the end of their path instead of stopping.
				E.bDirForward = !E.bDirForward;
				bTravelForward = !bTravelForward;
				E.Route.Reset();
				E.RouteIndex = 0;
				continue;
			}
			// Dead end: the vehicle stops at the end of the road.
			E.Speed = 0.0;
			E.DesiredSpeed = 0.0;
			E.bAtDeadEnd = true;
			break;
		}

		const FOpenDriveRoad* Next = Map->FindRoad(Chosen->RoadId);
		if (!Next)
		{
			E.Speed = 0.0;
			E.DesiredSpeed = 0.0;
			break;
		}

		// Finish any lane change on the old road, then map the lane onto the new road.
		if (E.bLaneChanging)
		{
			E.LaneId = Map->ClampLaneId(*Road, E.S, E.LCTargetLane);
			E.bLaneChanging = false;
		}
		const bool bWithFlow = (bTravelForward == (E.LaneId < 0));
		int32 NewLane;
		if (const int32* Mapped = Chosen->LaneMap.Find(E.LaneId))
		{
			NewLane = *Mapped;
		}
		else
		{
			const int32 Sign = (Chosen->bForward == bWithFlow) ? -1 : 1;
			NewLane = Sign * FMath::Max(1, FMath::Abs(E.LaneId));
		}

		if (bFollowsRoute)
		{
			++E.RouteIndex;
		}
		E.RoadId = Next->Id;
		bTravelForward = Chosen->bForward;
		E.bDirForward = bReversing ? !bTravelForward : bTravelForward;
		E.S = bTravelForward ? 0.0 : Next->Length;
		E.LaneId = Map->ClampLaneId(*Next, E.S, NewLane);
	}

	if (const FOpenDriveRoad* Road = Map->FindRoad(E.RoadId))
	{
		E.S = FMath::Clamp(E.S, 0.0, Road->Length);
		if (!E.bLaneChanging)
		{
			E.LaneId = Map->ClampLaneId(*Road, E.S, E.LaneId);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Simple vehicle dynamics
// ------------------------------------------------------------------------------------------------

bool UOpenScenarioRunner::UsesSimpleDynamics(const FOSCEntityState& E) const
{
	return Dynamics.Mode == EOpenScenarioDynamicsMode::Simple && E.Def.Kind == EOSCEntityKind::Vehicle && !E.bTrajectoryControlled;
}

void UOpenScenarioRunner::SetCommandedSpeed(FOSCEntityState& E, double Speed) const
{
	E.DesiredSpeed = Speed;
	if (bInitPhase || !UsesSimpleDynamics(E))
	{
		E.Speed = Speed;
	}
}

bool UOpenScenarioRunner::AdvanceCursor(FRoadCursor& C, const TArray<FOpenDriveRouteStep>& Route, double Ds) const
{
	double Remaining = Ds;
	TArray<FOpenDriveSuccessor> Successors;
	for (int32 Guard = 0; Guard < 16; ++Guard)
	{
		const FOpenDriveRoad* Road = Map->FindRoad(C.RoadId);
		if (!Road)
		{
			return false;
		}
		const double Room = C.bForward ? Road->Length - C.S : C.S;
		if (Remaining <= Room + 1e-9)
		{
			C.S += C.bForward ? Remaining : -Remaining;
			return true;
		}
		Remaining -= Room;
		C.S = C.bForward ? Road->Length : 0.0;

		Map->GetSuccessors(*Road, C.bForward, Successors);
		const FOpenDriveSuccessor* Chosen = nullptr;
		bool bFollowsRoute = false;
		if (Route.IsValidIndex(C.RouteIndex + 1))
		{
			const FOpenDriveRouteStep& Next = Route[C.RouteIndex + 1];
			for (const FOpenDriveSuccessor& S : Successors)
			{
				if (S.RoadId == Next.RoadId && S.bForward == Next.bForward)
				{
					Chosen = &S;
					bFollowsRoute = true;
					break;
				}
			}
		}
		if (!Chosen)
		{
			for (const FOpenDriveSuccessor& S : Successors)
			{
				if (S.LaneMap.Contains(C.LaneId))
				{
					Chosen = &S;
					break;
				}
			}
		}
		if (!Chosen && Successors.Num() > 0)
		{
			Chosen = &Successors[0];
		}
		const FOpenDriveRoad* NextRoad = Chosen ? Map->FindRoad(Chosen->RoadId) : nullptr;
		if (!NextRoad)
		{
			return false;
		}

		const bool bWithFlow = (C.bForward == (C.LaneId < 0));
		int32 NewLane;
		if (const int32* Mapped = Chosen->LaneMap.Find(C.LaneId))
		{
			NewLane = *Mapped;
		}
		else
		{
			NewLane = ((Chosen->bForward == bWithFlow) ? -1 : 1) * FMath::Max(1, FMath::Abs(C.LaneId));
		}
		if (bFollowsRoute)
		{
			++C.RouteIndex;
		}
		C.RoadId = NextRoad->Id;
		C.bForward = Chosen->bForward;
		C.S = C.bForward ? 0.0 : NextRoad->Length;
		C.LaneId = Map->ClampLaneId(*NextRoad, C.S, NewLane);
	}
	return false;
}

void UOpenScenarioRunner::BuildLookAhead(FOSCEntityState& E, double Distance)
{
	constexpr double Step = LookAheadStep;
	FLookAheadCache& C = E.LookAhead;
	C.Samples.Reset();
	C.Controls.Reset();
	C.bValid = true;
	C.TravelledAtBuild = E.TravelledDistance;
	C.TimeBuilt = SimTime;
	C.CoveredDistance = Distance;

	if (E.bOnRoad && Map.IsValid())
	{
		FRoadCursor Cursor;
		Cursor.RoadId = E.RoadId;
		Cursor.S = E.S;
		Cursor.LaneId = E.bLaneChanging ? E.LCTargetLane : E.LaneId;
		Cursor.bForward = E.bDirForward;
		Cursor.RouteIndex = E.RouteIndex;
		double SignLimit = E.SignLimit;

		// Samples lie on a fixed grid of travelled distance (plus one at the current position), so the
		// look-ahead is stable when it is rebuilt while the vehicle moves.
		double FirstGrid = Step - FMath::Fmod(E.TravelledDistance, Step);
		if (FirstGrid < 0.05)
		{
			FirstGrid += Step;
		}
		bool bFirstSample = true;
		for (double D = 0.0; D <= Distance;)
		{
			const FOpenDriveRoad* Road = Map->FindRoad(Cursor.RoadId);
			if (!Road)
			{
				break;
			}
			const int32 Lane = Map->ClampLaneId(*Road, Cursor.S, Cursor.LaneId);
			const FOpenDrivePose Pose = Map->EvaluatePose(*Road, Cursor.S, Map->GetLaneCenterT(*Road, Cursor.S, Lane));

			double Limit = -1.0;
			if (Dynamics.bRespectSpeedLimits)
			{
				const double RoadLimit = Map->GetSpeedLimit(*Road, Cursor.S, Lane);
				if (RoadLimit >= 0.0)
				{
					Limit = RoadLimit * Dynamics.SpeedLimitFactor;
				}
			}
			if (SignLimit >= 0.0)
			{
				Limit = Limit < 0.0 ? SignLimit : FMath::Min(Limit, SignLimit);
			}
			if (Dynamics.bSlowInCurves)
			{
				const double K = FMath::Abs(Map->GetLaneCurvature(*Road, Cursor.S, Lane));
				if (K > 1e-4)
				{
					const double CurveSpeed = FMath::Sqrt(Dynamics.MaxLateralAcceleration / K);
					Limit = Limit < 0.0 ? CurveSpeed : FMath::Min(Limit, CurveSpeed);
				}
			}

			FLookAheadSample Sample;
			Sample.Dist = D;
			Sample.VLimit = Limit;
			Sample.X = Pose.X;
			Sample.Y = Pose.Y;
			C.Samples.Add(Sample);

			const double NextD = bFirstSample ? FirstGrid : D + Step;
			bFirstSample = false;
			const FRoadCursor Before = Cursor;
			const bool bAdvanced = AdvanceCursor(Cursor, E.Route, NextD - D);
			if (bAdvanced)
			{
				CollectSignalsBetween(Before, Cursor, D, [&](int32 Index, double Dist)
				{
					const FRuntimeSignal& Sig = Signals[Index];
					if (Sig.Kind == EOSCSignalKind::SpeedLimit && SignalSettings.bObeySpeedSigns) { SignLimit = Sig.SpeedLimit; }
					else if (Sig.Kind == EOSCSignalKind::EndSpeedLimit && SignalSettings.bObeySpeedSigns) { SignLimit = -1.0; }
					else if (Sig.Kind == EOSCSignalKind::TrafficLight || Sig.Kind == EOSCSignalKind::Stop || Sig.Kind == EOSCSignalKind::Yield)
					{
						FLookAheadControl Control;
						Control.Dist = Dist;
						Control.Signal = Index;
						C.Controls.Add(Control);
					}
				});
			}
			if (!bAdvanced)
			{
				// Dead end: plan to stand at the end of the road.
				if (const FOpenDriveRoad* Last = Map->FindRoad(Cursor.RoadId))
				{
					const FOpenDrivePose End = Map->EvaluatePose(*Last, Cursor.S, Map->GetLaneCenterT(*Last, Cursor.S, Lane));
					FLookAheadSample Stop;
					Stop.Dist = D + 0.5 * Step;
					Stop.VLimit = 0.0;
					Stop.X = End.X;
					Stop.Y = End.Y;
					C.Samples.Add(Stop);
				}
				break;
			}
			D = NextD;
		}
	}
	else
	{
		for (double D = 0.0; D <= Distance; D += Step)
		{
			FLookAheadSample Sample;
			Sample.Dist = D;
			Sample.X = E.X + FMath::Cos(E.Heading) * D;
			Sample.Y = E.Y + FMath::Sin(E.Heading) * D;
			C.Samples.Add(Sample);
		}
	}
}

bool UOpenScenarioRunner::FindLeader(const FOSCEntityState& E, double LookDistance, double& OutGap, double& OutLeaderSpeed, FString& OutName) const
{
	const FLookAheadCache& C = E.LookAhead;
	if (C.Samples.Num() < 2)
	{
		return false;
	}
	const double Along = E.TravelledDistance - C.TravelledAtBuild;
	const double FrontSelf = E.Def.CenterX + 0.5 * E.Def.Length;

	bool bFound = false;
	OutGap = TNumericLimits<double>::Max();
	for (const FOSCEntityState& O : Entities)
	{
		if (&O == &E || !O.bActive || O.Def.Kind == EOSCEntityKind::External)
		{
			continue;
		}
		// Cheap reject before projecting onto the path.
		if (FMath::Square(O.X - E.X) + FMath::Square(O.Y - E.Y) > FMath::Square(LookDistance + O.Def.Length + E.Def.Length + 5.0))
		{
			continue;
		}
		const double Ch = FMath::Cos(O.Heading);
		const double Sh = FMath::Sin(O.Heading);
		const double Ox = O.X + Ch * O.Def.CenterX - Sh * O.Def.CenterY;
		const double Oy = O.Y + Sh * O.Def.CenterX + Ch * O.Def.CenterY;

		// Closest point of the planned path to the other actor's centre.
		double BestLat = TNumericLimits<double>::Max();
		double BestAlong = 0.0;
		double DirX = 1.0, DirY = 0.0;
		for (int32 i = 0; i + 1 < C.Samples.Num(); ++i)
		{
			const FLookAheadSample& A = C.Samples[i];
			const FLookAheadSample& B = C.Samples[i + 1];
			const double Dx = B.X - A.X;
			const double Dy = B.Y - A.Y;
			const double Len2 = Dx * Dx + Dy * Dy;
			if (Len2 < 1e-9)
			{
				continue;
			}
			const double U = FMath::Clamp(((Ox - A.X) * Dx + (Oy - A.Y) * Dy) / Len2, 0.0, 1.0);
			const double Lat = FMath::Sqrt(FMath::Square(Ox - (A.X + U * Dx)) + FMath::Square(Oy - (A.Y + U * Dy)));
			if (Lat < BestLat)
			{
				BestLat = Lat;
				const double Len = FMath::Sqrt(Len2);
				BestAlong = A.Dist + U * Len;
				DirX = Dx / Len;
				DirY = Dy / Len;
			}
		}
		if (BestLat == TNumericLimits<double>::Max())
		{
			continue;
		}
		const double Rel = BestAlong - Along;
		if (Rel <= 0.0 || Rel > LookDistance + O.Def.Length)
		{
			continue;
		}

		const double Angle = O.Heading - FMath::Atan2(DirY, DirX);
		const double CosD = FMath::Cos(Angle);
		const double SinD = FMath::Sin(Angle);
		const double LatExtent = 0.5 * O.Def.Width * FMath::Abs(CosD) + 0.5 * O.Def.Length * FMath::Abs(SinD);
		const double LongExtent = 0.5 * O.Def.Length * FMath::Abs(CosD) + 0.5 * O.Def.Width * FMath::Abs(SinD);
		if (BestLat > 0.5 * E.Def.Width + LatExtent + 0.3)
		{
			continue;
		}
		const double Gap = FMath::Max(0.01, Rel - LongExtent - FrontSelf);
		if (Gap < OutGap)
		{
			bFound = true;
			OutGap = Gap;
			OutLeaderSpeed = O.Speed * CosD;
			OutName = O.Name;
		}
	}
	return bFound;
}

void UOpenScenarioRunner::UpdateDynamics(FOSCEntityState& E, double Dt)
{
	if (!UsesSimpleDynamics(E) || E.Speed < -1e-6 || E.DesiredSpeed < 0.0)
	{
		// Kinematic mode, pedestrians/objects, trajectories and reversing: speed is exactly what is commanded.
		E.DesiredSpeed = E.Speed;
		E.bSpeedConstrained = false;
		E.LeaderGap = -1.0;
		E.LeaderName.Reset();
		return;
	}

	const double V = E.Speed;
	const double AMax = FMath::Max(0.1, FMath::Min(E.Def.MaxAcceleration, Dynamics.MaxAcceleration));
	const double BMax = FMath::Max(0.1, FMath::Min(E.Def.MaxDeceleration, Dynamics.MaxDeceleration));
	const double BComfort = FMath::Min(Dynamics.ComfortDeceleration, BMax);
	const double VDesired = E.Def.MaxSpeed > 0.0 ? FMath::Min(E.DesiredSpeed, E.Def.MaxSpeed) : E.DesiredSpeed;

	const double Look = FMath::Clamp(V * V / (2.0 * BComfort) + V * Dynamics.TimeHeadway + 30.0, 30.0, Dynamics.MaxLookAhead);
	FLookAheadCache& Cache = E.LookAhead;
	double Along = E.TravelledDistance - Cache.TravelledAtBuild;
	const double MaxAge = E.bLaneChanging ? 0.2 : 0.5;
	if (!Cache.bValid || Along > 5.0 || SimTime - Cache.TimeBuilt > MaxAge || Cache.CoveredDistance - Along < Look)
	{
		BuildLookAhead(E, Look + 20.0);
		Along = 0.0;
	}

	// Fastest speed from which the vehicle can still brake to every limit ahead.
	double VEnvelope = TNumericLimits<double>::Max();
	for (const FLookAheadSample& Sample : Cache.Samples)
	{
		// Samples are spaced LookAheadStep apart; a limit is assumed to start one step before its sample so the
		// discretisation never lets the vehicle overshoot it.
		if (Sample.VLimit < 0.0 || Sample.Dist < Along - LookAheadStep)
		{
			continue;
		}
		const double D = FMath::Max(0.0, Sample.Dist - Along - LookAheadStep);
		VEnvelope = FMath::Min(VEnvelope, FMath::Sqrt(Sample.VLimit * Sample.VLimit + 2.0 * BComfort * D));
	}

	// Signals ahead: red lights, stop signs and give-way signs can force a stop at their line.
	bool bDwelling = false;
	const double FrontSelf = E.Def.CenterX + 0.5 * E.Def.Length;
	for (const FLookAheadControl& Control : Cache.Controls)
	{
		const FRuntimeSignal& Sig = Signals[Control.Signal];
		if (E.HandledSignals.Contains(Control.Signal))
		{
			continue;
		}
		const double ToLine = Control.Dist - Along - FrontSelf - SignalSettings.StopLineMargin;
		if (ToLine < -1.0)
		{
			// The front bumper is past the line: nothing left to obey.
			if (Sig.Kind != EOSCSignalKind::TrafficLight)
			{
				E.HandledSignals.Add(Control.Signal);
			}
			continue;
		}
		bool bMustStop = false;
		switch (Sig.Kind)
		{
		case EOSCSignalKind::TrafficLight:
			if (SignalSettings.bObeyTrafficLights)
			{
				if (Sig.State == EOpenScenarioSignalState::Red)
				{
					bMustStop = true;
				}
				else if (Sig.State == EOpenScenarioSignalState::Yellow)
				{
					// Stop if that is possible with comfortable braking, otherwise drive through.
					bMustStop = ToLine > 0.0 && V * V / (2.0 * BComfort) <= ToLine + 0.5;
				}
			}
			break;
		case EOSCSignalKind::Stop:
			if (SignalSettings.bObeyStopSigns)
			{
				bMustStop = true;
				if (ToLine <= 0.5 && V < 0.2)
				{
					bDwelling = true;
					E.StopDwell += Dt;
					if (E.StopDwell >= SignalSettings.StopDwellTime && !JunctionOccupied(Sig, &E))
					{
						E.HandledSignals.Add(Control.Signal);
						E.StopDwell = 0.0;
						bMustStop = false;
					}
				}
			}
			break;
		case EOSCSignalKind::Yield:
			if (SignalSettings.bObeyYieldSigns)
			{
				if (JunctionOccupied(Sig, &E))
				{
					bMustStop = true;
				}
				else
				{
					const double D = FMath::Max(0.0, ToLine);
					VEnvelope = FMath::Min(VEnvelope, FMath::Sqrt(FMath::Square(SignalSettings.YieldApproachSpeed) + 2.0 * BComfort * D));
				}
			}
			break;
		default:
			break;
		}
		if (bMustStop)
		{
			VEnvelope = FMath::Min(VEnvelope, FMath::Sqrt(2.0 * BComfort * FMath::Max(0.0, ToLine)));
		}
	}
	if (!bDwelling)
	{
		E.StopDwell = 0.0;
	}

	const double VTarget = FMath::Min(VDesired, VEnvelope);
	double A = FMath::Clamp((VTarget - V) / Dt, -BMax, AMax);
	if (Dynamics.MaxJerk > 0.0 && A < 0.0)
	{
		// The envelope moves in steps when the look-ahead is rebuilt; let the braking build up smoothly.
		// (Releasing the throttle is immediate, otherwise the vehicle would overshoot its target speed.)
		A = FMath::Max(A, FMath::Min(E.CmdAccel, 0.0) - Dynamics.MaxJerk * Dt);
	}
	bool bConstrained = VEnvelope < VDesired - 0.3;

	double Gap = -1.0;
	double LeaderSpeed = 0.0;
	FString Leader;
	if (Dynamics.bBrakeForObstacles && FindLeader(E, Look, Gap, LeaderSpeed, Leader))
	{
		const double SStar = Dynamics.MinGap + FMath::Max(0.0, V * Dynamics.TimeHeadway + V * (V - LeaderSpeed) / (2.0 * FMath::Sqrt(AMax * BComfort)));
		double AFollow = Gap > 0.05 ? AMax * (1.0 - FMath::Square(SStar / Gap)) : -BMax;
		AFollow = FMath::Max(AFollow, -BMax);
		if (AFollow < A)
		{
			A = AFollow;
			bConstrained = true;
		}
		E.LeaderGap = Gap;
		E.LeaderName = Leader;
	}
	else
	{
		E.LeaderGap = -1.0;
		E.LeaderName.Reset();
	}

	E.CmdAccel = A;
	double VNew = FMath::Max(0.0, V + A * Dt);
	if (VTarget <= 0.01 && VNew < 0.05)
	{
		VNew = 0.0;
		E.CmdAccel = 0.0;
	}
	E.Speed = VNew;
	E.bSpeedConstrained = bConstrained;
}

// ------------------------------------------------------------------------------------------------
// Traffic generators (TrafficSwarmAction / TrafficSourceAction / TrafficSinkAction)
// ------------------------------------------------------------------------------------------------

namespace
{
	/** Default bounding box and kind for a TrafficDefinition category. */
	void MakeTrafficEntityDef(const FString& Category, FOSCEntity& D)
	{
		D.Category = Category;
		struct FSize { const TCHAR* Name; double L, W, H; };
		static const FSize Sizes[] = {
			{ TEXT("car"), 4.5, 1.8, 1.5 }, { TEXT("van"), 5.5, 2.0, 2.2 }, { TEXT("truck"), 8.0, 2.5, 3.2 },
			{ TEXT("trailer"), 12.0, 2.5, 3.5 }, { TEXT("semitrailer"), 14.0, 2.5, 3.5 }, { TEXT("bus"), 12.0, 2.55, 3.2 },
			{ TEXT("motorbike"), 2.2, 0.8, 1.5 }, { TEXT("bicycle"), 1.8, 0.6, 1.7 }, { TEXT("train"), 20.0, 2.8, 3.5 }, { TEXT("tram"), 20.0, 2.5, 3.5 } };

		if (Category.Equals(TEXT("pedestrian"), ESearchCase::IgnoreCase))
		{
			D.Kind = EOSCEntityKind::Pedestrian;
			D.Length = 0.6; D.Width = 0.6; D.Height = 1.8;
			D.CenterX = 0.0; D.CenterY = 0.0; D.CenterZ = 0.9;
			D.MaxSpeed = 3.0;
			return;
		}
		D.Kind = EOSCEntityKind::Vehicle;
		D.Length = 4.5; D.Width = 1.8; D.Height = 1.5;
		for (const FSize& S : Sizes)
		{
			if (Category.Equals(S.Name, ESearchCase::IgnoreCase))
			{
				D.Length = S.L; D.Width = S.W; D.Height = S.H;
				break;
			}
		}
		D.CenterX = 0.3 * D.Length;
		D.CenterY = 0.0;
		D.CenterZ = 0.5 * D.Height;
		D.MaxSpeed = 50.0;
	}

	int64 CellKey(int32 Cx, int32 Cy)
	{
		return (static_cast<int64>(Cx) << 32) ^ static_cast<int64>(static_cast<uint32>(Cy));
	}

	constexpr double GridCell = 100.0;
	constexpr double SegmentLength = 20.0;
}

int32 UOpenScenarioRunner::GetTrafficCount() const
{
	int32 Count = 0;
	for (const FOSCEntityState& E : Entities)
	{
		Count += (E.bActive && E.TrafficGenerator != INDEX_NONE) ? 1 : 0;
	}
	return Count;
}

bool UOpenScenarioRunner::HasActiveGenerators() const
{
	for (const FTrafficGenerator& G : Generators)
	{
		if (G.bActive)
		{
			return true;
		}
	}
	return false;
}

void UOpenScenarioRunner::StartTrafficAction(const FOSCAction& Action)
{
	if (Action.Traffic.Kind == EOSCTrafficKind::Stop)
	{
		for (FTrafficGenerator& G : Generators)
		{
			if (G.bActive && G.Def.TrafficName == Action.Traffic.TrafficName)
			{
				G.bActive = false;
			}
		}
		return;
	}
	if (!Map.IsValid())
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("Traffic action '%s' needs a road network; it is ignored."), *Action.Traffic.TrafficName);
		return;
	}
	FTrafficGenerator G;
	G.Id = NextGeneratorId++;
	G.Def = Action.Traffic;
	Generators.Add(MoveTemp(G));
}

void UOpenScenarioRunner::BuildTrafficIndex()
{
	bTrafficIndexBuilt = true;
	LaneSegments.Reset();
	LaneGrid.Reset();
	if (!Map.IsValid())
	{
		return;
	}
	for (const FOpenDriveRoad& Road : Map->GetRoads())
	{
		if (Road.IsJunctionRoad())
		{
			continue; // actors are spawned on regular roads and drive through junctions
		}
		for (const FOpenDriveLaneSection& Section : Road.LaneSections)
		{
			for (const FOpenDriveLane& Lane : Section.Lanes)
			{
				const bool bVehicle = Lane.IsDriving();
				const bool bWalk = Lane.Type.Equals(TEXT("sidewalk"), ESearchCase::IgnoreCase) || Lane.Type.Equals(TEXT("walking"), ESearchCase::IgnoreCase);
				if (!bVehicle && !bWalk)
				{
					continue;
				}
				for (double S0 = Section.S; S0 < Section.EndS - 0.5; S0 += SegmentLength)
				{
					FLaneSegment Seg;
					Seg.RoadId = Road.Id;
					Seg.LaneId = Lane.Id;
					Seg.S0 = S0;
					Seg.S1 = FMath::Min(Section.EndS, S0 + SegmentLength);
					const double Mid = 0.5 * (Seg.S0 + Seg.S1);
					const FOpenDrivePose Pose = Map->EvaluatePose(Road, Mid, Map->GetLaneCenterT(Road, Mid, Lane.Id));
					Seg.X = Pose.X;
					Seg.Y = Pose.Y;
					// Walkers and vehicles share the grid; the lane type is looked up again when sampling.
					const int32 Index = LaneSegments.Add(Seg);
					LaneGrid.FindOrAdd(CellKey(FMath::FloorToInt(Seg.X / GridCell), FMath::FloorToInt(Seg.Y / GridCell))).Add(Index);
				}
			}
		}
	}
	UE_LOG(LogOpenScenario, Log, TEXT("Traffic lane index: %d segments."), LaneSegments.Num());
}

void UOpenScenarioRunner::CollectSegments(double X, double Y, double Radius, bool bPedestrian, TArray<int32>& Out) const
{
	Out.Reset();
	const int32 X0 = FMath::FloorToInt((X - Radius) / GridCell), X1 = FMath::FloorToInt((X + Radius) / GridCell);
	const int32 Y0 = FMath::FloorToInt((Y - Radius) / GridCell), Y1 = FMath::FloorToInt((Y + Radius) / GridCell);
	for (int32 Cx = X0; Cx <= X1; ++Cx)
	{
		for (int32 Cy = Y0; Cy <= Y1; ++Cy)
		{
			const TArray<int32>* Cell = LaneGrid.Find(CellKey(Cx, Cy));
			if (!Cell)
			{
				continue;
			}
			for (const int32 Index : *Cell)
			{
				const FLaneSegment& Seg = LaneSegments[Index];
				if (FMath::Square(Seg.X - X) + FMath::Square(Seg.Y - Y) > FMath::Square(Radius))
				{
					continue;
				}
				const FOpenDriveRoad* Road = Map->FindRoad(Seg.RoadId);
				const FOpenDriveLaneSection* Section = Road ? Map->FindLaneSection(*Road, Seg.S0) : nullptr;
				const FOpenDriveLane* Lane = Section ? Section->FindLane(Seg.LaneId) : nullptr;
				if (!Lane)
				{
					continue;
				}
				const bool bIsWalk = !Lane->IsDriving();
				if (bIsWalk == bPedestrian)
				{
					Out.Add(Index);
				}
			}
		}
	}
}

FString UOpenScenarioRunner::PickCategory(const FOSCTraffic& Def)
{
	double Total = 0.0;
	for (const FOSCTrafficCategory& C : Def.Distribution)
	{
		Total += FMath::Max(0.0, C.Weight);
	}
	if (Total <= 0.0)
	{
		return TEXT("car");
	}
	double Pick = Random.FRand() * Total;
	for (const FOSCTrafficCategory& C : Def.Distribution)
	{
		Pick -= FMath::Max(0.0, C.Weight);
		if (Pick <= 0.0)
		{
			return C.Category;
		}
	}
	return Def.Distribution.Last().Category;
}

bool UOpenScenarioRunner::IsSpotClear(double X, double Y, double Radius) const
{
	for (const FOSCEntityState& E : Entities)
	{
		if (E.bActive && FMath::Square(E.X - X) + FMath::Square(E.Y - Y) < FMath::Square(Radius))
		{
			return false;
		}
	}
	return true;
}

int32 UOpenScenarioRunner::PickLaneAt(const FOpenDriveRoad& Road, double S, bool bPedestrian)
{
	const FOpenDriveLaneSection* Section = Map->FindLaneSection(Road, S);
	if (!Section)
	{
		return 0;
	}
	TArray<int32> Lanes;
	for (const FOpenDriveLane& Lane : Section->Lanes)
	{
		const bool bWalk = Lane.Type.Equals(TEXT("sidewalk"), ESearchCase::IgnoreCase) || Lane.Type.Equals(TEXT("walking"), ESearchCase::IgnoreCase);
		if (bPedestrian ? bWalk : Lane.IsDriving())
		{
			Lanes.Add(Lane.Id);
		}
	}
	return Lanes.Num() > 0 ? Lanes[Random.RandRange(0, Lanes.Num() - 1)] : 0;
}

void UOpenScenarioRunner::ExtendRandomRoute(FOSCEntityState& E, int32 Count)
{
	if (!Map.IsValid() || E.Route.Num() == 0)
	{
		return;
	}
	TArray<FOpenDriveSuccessor> Successors;
	for (int32 i = 0; i < Count; ++i)
	{
		const FOpenDriveRouteStep Last = E.Route.Last();
		const FOpenDriveRoad* Road = Map->FindRoad(Last.RoadId);
		if (!Road)
		{
			return;
		}
		Map->GetSuccessors(*Road, Last.bForward, Successors);
		for (int32 k = Successors.Num() - 1; k >= 0; --k)
		{
			if (Successors[k].RoadId == Last.RoadId)
			{
				Successors.RemoveAt(k);
			}
		}
		if (Successors.Num() == 0)
		{
			return;
		}
		const FOpenDriveSuccessor& Pick = Successors[Random.RandRange(0, Successors.Num() - 1)];
		FOpenDriveRouteStep Step;
		Step.RoadId = Pick.RoadId;
		Step.bForward = Pick.bForward;
		E.Route.Add(Step);
	}
}

int32 UOpenScenarioRunner::SpawnTrafficEntity(FTrafficGenerator& G, const FString& Category, const FString& RoadId, int32 LaneId, double S, bool bTravelForward)
{
	const FOpenDriveRoad* Road = Map->FindRoad(RoadId);
	if (!Road)
	{
		return INDEX_NONE;
	}
	FOSCEntityState E;
	E.Name = FString::Printf(TEXT("%s_%d"), *G.Def.TrafficName, ++TrafficCounter);
	MakeTrafficEntityDef(Category, E.Def);
	E.Def.Name = E.Name;
	E.TrafficGenerator = G.Id;
	E.bOnRoad = true;
	E.RoadId = RoadId;
	E.S = FMath::Clamp(S, 0.0, Road->Length);
	E.LaneId = LaneId;
	E.bDirForward = bTravelForward;
	UpdatePoseFromRoad(E);

	double Speed;
	if (E.Def.Kind == EOSCEntityKind::Pedestrian)
	{
		Speed = TrafficSettings.PedestrianSpeedMin + Random.FRand() * (TrafficSettings.PedestrianSpeedMax - TrafficSettings.PedestrianSpeedMin);
	}
	else
	{
		const double Limit = Map->GetSpeedLimit(*Road, E.S, LaneId);
		Speed = G.Def.Velocity > 0.0 ? G.Def.Velocity : (Limit > 0.0 ? Limit : TrafficSettings.DefaultVehicleSpeed);
		Speed *= 0.9 + 0.15 * Random.FRand();
		if (Dynamics.bRespectSpeedLimits && Limit > 0.0)
		{
			Speed = FMath::Min(Speed, Limit * Dynamics.SpeedLimitFactor);
		}
	}
	E.Speed = Speed;
	E.DesiredSpeed = Speed;

	FOpenDriveRouteStep First;
	First.RoadId = RoadId;
	First.bForward = bTravelForward;
	E.Route.Add(First);
	ExtendRandomRoute(E, 6);

	int32 Slot = INDEX_NONE;
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		if (!Entities[i].bActive)
		{
			Slot = i;
			break;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Slot = Entities.Add(FOSCEntityState());
	}
	Entities[Slot] = MoveTemp(E);
	EntityIndex.Add(Entities[Slot].Name, Slot);
	if (bSpawnActors && World)
	{
		SpawnEntityActor(Entities[Slot]);
	}
	return Slot;
}

void UOpenScenarioRunner::DespawnEntity(int32 Index)
{
	FOSCEntityState& E = Entities[Index];
	if (!E.bActive)
	{
		return;
	}
	E.bActive = false;
	EntityIndex.Remove(E.Name);
	if (TObjectPtr<AActor>* Actor = EntityActors.Find(E.Name))
	{
		if (Actor->Get())
		{
			Actor->Get()->Destroy();
		}
		EntityActors.Remove(E.Name);
	}
}

void UOpenScenarioRunner::UpdateTraffic(double Dt)
{
	if (Generators.Num() == 0 || !Map.IsValid())
	{
		return;
	}
	if (!bTrafficIndexBuilt)
	{
		BuildTrafficIndex();
	}

	// Housekeeping of all traffic actors: keep their random route long enough, remove stuck ones.
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		FOSCEntityState& E = Entities[i];
		if (!E.bActive || E.TrafficGenerator == INDEX_NONE)
		{
			continue;
		}
		if (E.RouteIndex > 8)
		{
			const int32 Drop = E.RouteIndex - 2;
			E.Route.RemoveAt(0, Drop);
			E.RouteIndex -= Drop;
			E.LookAhead.bValid = false;
		}
		if (E.Route.Num() - E.RouteIndex < 4)
		{
			ExtendRandomRoute(E, 6);
		}
		E.DeadEndTime = E.bAtDeadEnd ? E.DeadEndTime + Dt : 0.0;
		E.StationaryTime = FMath::Abs(E.Speed) < 0.1 ? E.StationaryTime + Dt : 0.0;
		if ((TrafficSettings.bDespawnAtDeadEnds && E.DeadEndTime > 3.0)
			|| (TrafficSettings.StuckDespawnSeconds > 0.0 && E.StationaryTime > TrafficSettings.StuckDespawnSeconds))
		{
			DespawnEntity(i);
		}
	}

	for (int32 g = 0; g < Generators.Num(); ++g)
	{
		FTrafficGenerator& G = Generators[g];
		if (!G.bActive)
		{
			continue;
		}
		switch (G.Def.Kind)
		{
		case EOSCTrafficKind::Swarm: UpdateSwarm(G, Dt); break;
		case EOSCTrafficKind::Source: UpdateSource(G, Dt); break;
		case EOSCTrafficKind::Sink: UpdateSink(G, Dt); break;
		default: break;
		}
	}
}

void UOpenScenarioRunner::UpdateSwarm(FTrafficGenerator& G, double Dt)
{
	const FOSCEntityState* Center = FindEntity(G.Def.CentralObject);
	if (!Center)
	{
		return;
	}
	const double Ch = FMath::Cos(Center->Heading);
	const double Sh = FMath::Sin(Center->Heading);
	const double Cx = Center->X + Ch * G.Def.Offset;
	const double Cy = Center->Y + Sh * G.Def.Offset;
	const double A = FMath::Max(1.0, G.Def.SemiMajorAxis);
	const double B = FMath::Max(1.0, G.Def.SemiMinorAxis);
	const double CenterX = Center->X;
	const double CenterY = Center->Y;
	auto Inside = [=](double X, double Y, double Scale)
	{
		const double Dx = X - Cx;
		const double Dy = Y - Cy;
		const double U = Dx * Ch + Dy * Sh;
		const double V = -Dx * Sh + Dy * Ch;
		return FMath::Square(U / (A * Scale)) + FMath::Square(V / (B * Scale)) <= 1.0;
	};

	// Remove actors that left the ellipse (with some hysteresis) and count the rest.
	int32 Count = 0;
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		FOSCEntityState& E = Entities[i];
		if (!E.bActive || E.TrafficGenerator != G.Id)
		{
			continue;
		}
		if (!Inside(E.X, E.Y, 1.15))
		{
			DespawnEntity(i);
		}
		else
		{
			++Count;
		}
	}
	const int32 Need = G.Def.NumberOfVehicles - Count;
	if (Need <= 0)
	{
		G.Accumulator = 0.0;
		return;
	}

	int32 Budget;
	const bool bBurst = G.bFirstUpdate;
	if (G.bFirstUpdate)
	{
		G.bFirstUpdate = false;
		Budget = Need;
	}
	else
	{
		G.Accumulator = FMath::Min(G.Accumulator + TrafficSettings.MaxSpawnsPerSecond * Dt, 2.0 + TrafficSettings.MaxSpawnsPerSecond);
		Budget = FMath::Min(Need, FMath::FloorToInt(G.Accumulator));
	}

	const double Reach = FMath::Max(A, B) + FMath::Abs(G.Def.Offset);
	TArray<int32> VehicleSegments, WalkSegments;
	bool bCollected = false;
	for (int32 n = 0; n < Budget; ++n)
	{
		if (!bCollected)
		{
			CollectSegments(Cx, Cy, Reach, false, VehicleSegments);
			CollectSegments(Cx, Cy, Reach, true, WalkSegments);
			bCollected = true;
		}
		const FString Category = PickCategory(G.Def);
		const bool bPedestrian = Category.Equals(TEXT("pedestrian"), ESearchCase::IgnoreCase);
		const TArray<int32>& Candidates = bPedestrian ? WalkSegments : VehicleSegments;
		if (Candidates.Num() == 0)
		{
			continue;
		}
		const double Clearance = bPedestrian ? 2.0 : TrafficSettings.SpawnClearance;
		bool bSpawned = false;
		for (int32 Attempt = 0; Attempt < 16 && !bSpawned; ++Attempt)
		{
			const FLaneSegment& Seg = LaneSegments[Candidates[Random.RandRange(0, Candidates.Num() - 1)]];
			const double S = Seg.S0 + Random.FRand() * (Seg.S1 - Seg.S0);
			const FOpenDriveRoad* Road = Map->FindRoad(Seg.RoadId);
			if (!Road)
			{
				continue;
			}
			const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, Map->GetLaneCenterT(*Road, S, Seg.LaneId));
			if (!Inside(Pose.X, Pose.Y, 1.0) || FMath::Square(Pose.X - CenterX) + FMath::Square(Pose.Y - CenterY) < FMath::Square(G.Def.InnerRadius)
				|| !IsSpotClear(Pose.X, Pose.Y, Clearance))
			{
				continue;
			}
			const bool bForward = bPedestrian ? Random.FRand() < 0.5f : Seg.LaneId < 0;
			bSpawned = SpawnTrafficEntity(G, Category, Seg.RoadId, Seg.LaneId, S, bForward) != INDEX_NONE;
		}
		if (!bBurst)
		{
			G.Accumulator -= 1.0;
		}
	}
}

void UOpenScenarioRunner::UpdateSource(FTrafficGenerator& G, double Dt)
{
	G.Accumulator = FMath::Min(G.Accumulator + G.Def.Rate * Dt, 2.0);
	if (G.Accumulator < 1.0)
	{
		return;
	}
	FOSCEntityState Spot;
	if (!ResolvePosition(G.Def.Position, Spot, true) || !Spot.bOnRoad)
	{
		return;
	}
	const FOpenDriveRoad* Road = Map->FindRoad(Spot.RoadId);
	if (!Road)
	{
		return;
	}
	const FString Category = PickCategory(G.Def);
	const bool bPedestrian = Category.Equals(TEXT("pedestrian"), ESearchCase::IgnoreCase);
	int32 Lane = Spot.LaneId;
	const FOpenDriveLaneSection* Section = Map->FindLaneSection(*Road, Spot.S);
	const FOpenDriveLane* Chosen = Section ? Section->FindLane(Lane) : nullptr;
	const bool bLaneFits = Chosen && (bPedestrian ? !Chosen->IsDriving() : Chosen->IsDriving());
	if (G.Def.Position.Type != EOSCPositionType::Lane || !bLaneFits)
	{
		Lane = PickLaneAt(*Road, Spot.S, bPedestrian);
	}
	if (Lane == 0 || !IsSpotClear(Spot.X, Spot.Y, bPedestrian ? 2.0 : TrafficSettings.SpawnClearance))
	{
		return;
	}
	const bool bForward = bPedestrian ? Random.FRand() < 0.5f : Lane < 0;
	if (SpawnTrafficEntity(G, Category, Spot.RoadId, Lane, Spot.S, bForward) != INDEX_NONE)
	{
		G.Accumulator -= 1.0;
	}
}

void UOpenScenarioRunner::UpdateSink(FTrafficGenerator& G, double Dt)
{
	FOSCEntityState Spot;
	if (!ResolvePosition(G.Def.Position, Spot, false))
	{
		return;
	}
	const bool bUnlimited = G.Def.Rate <= 0.0;
	G.Accumulator = FMath::Min(G.Accumulator + G.Def.Rate * Dt, 2.0);
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		const FOSCEntityState& E = Entities[i];
		if (!E.bActive || E.TrafficGenerator == INDEX_NONE)
		{
			continue;
		}
		if (FMath::Square(E.X - Spot.X) + FMath::Square(E.Y - Spot.Y) > FMath::Square(G.Def.Radius))
		{
			continue;
		}
		if (!bUnlimited && G.Accumulator < 1.0)
		{
			break;
		}
		DespawnEntity(i);
		G.Accumulator -= 1.0;
	}
}

// ---------------------------------------------------------------------------------------------
// Traffic signals and signs
// ---------------------------------------------------------------------------------------------

EOpenScenarioSignalState UOpenScenarioRunner::ParseSignalState(const FString& Text)
{
	const FString T = Text.ToLower();
	if (T.Contains(TEXT("red")) || T == TEXT("stop")) { return EOpenScenarioSignalState::Red; }
	if (T.Contains(TEXT("yellow")) || T.Contains(TEXT("amber"))) { return EOpenScenarioSignalState::Yellow; }
	if (T.Contains(TEXT("green")) || T == TEXT("go")) { return EOpenScenarioSignalState::Green; }
	return EOpenScenarioSignalState::Off;
}

bool UOpenScenarioRunner::GetSignalState(const FString& SignalId, EOpenScenarioSignalState& OutState) const
{
	if (const int32* Index = SignalById.Find(SignalId))
	{
		OutState = Signals[*Index].State;
		return true;
	}
	return false;
}

bool UOpenScenarioRunner::SetSignalStateById(const FString& SignalId, EOpenScenarioSignalState State)
{
	const int32* Index = SignalById.Find(SignalId);
	if (!Index)
	{
		return false;
	}
	Signals[*Index].bManual = true;
	SetSignalState(*Index, State);
	return true;
}

void UOpenScenarioRunner::SetSignalState(int32 Index, EOpenScenarioSignalState State)
{
	FRuntimeSignal& Sig = Signals[Index];
	if (Sig.State == State)
	{
		return;
	}
	Sig.State = State;
	OnSignalChanged.Broadcast(Sig.Id, State);
}

void UOpenScenarioRunner::BuildSignalTable()
{
	Signals.Reset();
	RoadSignals.Reset();
	SignalById.Reset();
	SignalGroups.Reset();
	SignalClusters.Reset();
	SignalControllers.Reset();
	if (!Map.IsValid())
	{
		return;
	}

	auto Starts = [](const FString& Value, std::initializer_list<const TCHAR*> Prefixes)
	{
		for (const TCHAR* P : Prefixes)
		{
			if (Value.Equals(P, ESearchCase::IgnoreCase)) { return true; }
		}
		return false;
	};

	for (const FOpenDriveRoad& Road : Map->GetRoads())
	{
		for (const FOpenDriveSignal& Src : Road.Signals)
		{
			FRuntimeSignal Sig;
			Sig.Id = Src.Id;
			Sig.RoadId = Road.Id;
			Sig.S = Src.S;
			Sig.T = Src.T;
			Sig.Orientation = Src.Orientation;

			const bool bStop = Starts(Src.Type, { TEXT("206"), TEXT("stop"), TEXT("R1-1") });
			const bool bYield = Starts(Src.Type, { TEXT("205"), TEXT("yield"), TEXT("giveway"), TEXT("R1-2") });
			const bool bLimit = Starts(Src.Type, { TEXT("274"), TEXT("R2-1") });
			const bool bEnd = Starts(Src.Type, { TEXT("278"), TEXT("280"), TEXT("282") });
			bool bLight = false;
			for (const FString& T : SignalSettings.TrafficLightTypes)
			{
				bLight |= Src.Type == T;
			}
			if (bStop) { Sig.Kind = EOSCSignalKind::Stop; }
			else if (bYield) { Sig.Kind = EOSCSignalKind::Yield; }
			else if (bLimit && Src.Value > 0.0)
			{
				Sig.Kind = EOSCSignalKind::SpeedLimit;
				const FString Unit = Src.Unit.ToLower();
				Sig.SpeedLimit = Unit == TEXT("mph") ? Src.Value * 0.44704 : (Unit == TEXT("m/s") ? Src.Value : Src.Value / 3.6);
			}
			else if (bEnd) { Sig.Kind = EOSCSignalKind::EndSpeedLimit; }
			else if (bLight || (Src.bDynamic && Src.Type != TEXT("1000002") && Src.Type != TEXT("1000003")))
			{
				Sig.Kind = EOSCSignalKind::TrafficLight;
			}

			// Which junction does the signal regulate?
			if (Road.IsJunctionRoad())
			{
				Sig.JunctionId = Road.JunctionId;
			}
			else
			{
				const bool bSucc = Road.SuccessorType == EOpenDriveElementType::Junction;
				const bool bPred = Road.PredecessorType == EOpenDriveElementType::Junction;
				if (Src.Orientation == EOpenDriveSignalOrientation::Plus && bSucc) { Sig.JunctionId = Road.SuccessorId; }
				else if (Src.Orientation == EOpenDriveSignalOrientation::Minus && bPred) { Sig.JunctionId = Road.PredecessorId; }
				else if (Src.Orientation == EOpenDriveSignalOrientation::None)
				{
					const bool bNearEnd = Src.S > 0.5 * Road.Length;
					if (bNearEnd && bSucc) { Sig.JunctionId = Road.SuccessorId; }
					else if (!bNearEnd && bPred) { Sig.JunctionId = Road.PredecessorId; }
				}
			}

			const FOpenDrivePose Pose = Map->EvaluatePose(Road, Src.S, Src.T);
			Sig.X = Pose.X;
			Sig.Y = Pose.Y;
			Sig.Z = Pose.Z + Src.ZOffset;

			const int32 Index = Signals.Add(Sig);
			RoadSignals.FindOrAdd(Road.Id).Add(Index);
			if (!Src.Id.IsEmpty())
			{
				SignalById.Add(Src.Id, Index);
			}
		}
	}

	BuildSignalGroups();

	// Scenario-defined controllers take over the signals they list.
	for (const FOSCSignalController& Def : Scenario.SignalControllers)
	{
		FSignalControllerRuntime Ctrl;
		Ctrl.Def = &Def;
		Ctrl.Elapsed = -FMath::Max(0.0, Def.Delay);
		for (const FOSCSignalPhase& Phase : Def.Phases)
		{
			for (const FOSCSignalStateEntry& Entry : Phase.States)
			{
				if (const int32* Index = SignalById.Find(Entry.SignalId))
				{
					Signals[*Index].bManual = true;
				}
			}
		}
		SignalControllers.Add(Ctrl);
	}
	for (FSignalControllerRuntime& Ctrl : SignalControllers)
	{
		ApplyControllerPhase(Ctrl);
	}
}

void UOpenScenarioRunner::BuildSignalGroups()
{
	TArray<double> Heading;
	Heading.SetNumZeroed(Signals.Num());
	for (int32 i = 0; i < Signals.Num(); ++i)
	{
		const FOpenDriveRoad* Road = Map->FindRoad(Signals[i].RoadId);
		if (Road)
		{
			double X, Y, H;
			Map->EvaluateReferenceLine(*Road, Signals[i].S, X, Y, H);
			Heading[i] = H + (Signals[i].Orientation == EOpenDriveSignalOrientation::Minus ? kPi : 0.0);
		}
	}

	TMap<FString, int32> ControllerGroupOf; // signal id -> group
	TArray<int32> GroupHeadingSignal;

	for (const FOpenDriveController& Ctrl : Map->GetControllers())
	{
		int32 Group = INDEX_NONE;
		for (const FOpenDriveControllerEntry& Entry : Ctrl.Controls)
		{
			const int32* Index = SignalById.Find(Entry.SignalId);
			if (!Index || Signals[*Index].Kind != EOSCSignalKind::TrafficLight || Signals[*Index].Group != INDEX_NONE)
			{
				continue;
			}
			if (Group == INDEX_NONE)
			{
				Group = SignalGroups.AddDefaulted();
				GroupHeadingSignal.Add(*Index);
			}
			SignalGroups[Group].Signals.Add(*Index);
			Signals[*Index].Group = Group;
		}
	}

	for (int32 i = 0; i < Signals.Num(); ++i)
	{
		FRuntimeSignal& Sig = Signals[i];
		if (Sig.Kind != EOSCSignalKind::TrafficLight || Sig.Group != INDEX_NONE)
		{
			continue;
		}
		int32 Group = INDEX_NONE;
		if (!Sig.JunctionId.IsEmpty())
		{
			for (int32 g = 0; g < SignalGroups.Num() && Group == INDEX_NONE; ++g)
			{
				const FRuntimeSignal& Other = Signals[GroupHeadingSignal[g]];
				if (Other.JunctionId == Sig.JunctionId && FMath::Abs(FMath::Sin(Heading[i] - Heading[GroupHeadingSignal[g]])) < 0.5)
				{
					Group = g;
				}
			}
		}
		if (Group == INDEX_NONE)
		{
			Group = SignalGroups.AddDefaulted();
			GroupHeadingSignal.Add(i);
		}
		SignalGroups[Group].Signals.Add(i);
		Sig.Group = Group;
	}

	// Groups that regulate the same junction alternate; everything else cycles on its own.
	TMap<FString, int32> ClusterOfJunction;
	for (int32 g = 0; g < SignalGroups.Num(); ++g)
	{
		const FString& Junction = Signals[GroupHeadingSignal[g]].JunctionId;
		int32 Cluster = INDEX_NONE;
		if (!Junction.IsEmpty())
		{
			if (const int32* Found = ClusterOfJunction.Find(Junction))
			{
				Cluster = *Found;
			}
		}
		if (Cluster == INDEX_NONE)
		{
			Cluster = SignalClusters.AddDefaulted();
			if (!Junction.IsEmpty())
			{
				ClusterOfJunction.Add(Junction, Cluster);
			}
		}
		SignalGroups[g].Cluster = Cluster;
		SignalGroups[g].IndexInCluster = SignalClusters[Cluster].Groups.Num();
		SignalClusters[Cluster].Groups.Add(g);
	}
}

void UOpenScenarioRunner::ApplyControllerPhase(FSignalControllerRuntime& Ctrl)
{
	if (!Ctrl.Def || !Ctrl.Def->Phases.IsValidIndex(Ctrl.Phase))
	{
		return;
	}
	for (const FOSCSignalStateEntry& Entry : Ctrl.Def->Phases[Ctrl.Phase].States)
	{
		if (const int32* Index = SignalById.Find(Entry.SignalId))
		{
			SetSignalState(*Index, ParseSignalState(Entry.State));
		}
	}
}

void UOpenScenarioRunner::UpdateSignals(double Dt)
{
	if (Signals.Num() == 0)
	{
		return;
	}

	for (FSignalControllerRuntime& Ctrl : SignalControllers)
	{
		if (!Ctrl.Def || Ctrl.Def->Phases.Num() == 0)
		{
			continue;
		}
		Ctrl.Elapsed += Dt;
		for (int32 Guard = 0; Guard < 16; ++Guard)
		{
			const double Duration = Ctrl.Def->Phases[Ctrl.Phase].Duration;
			if (Duration <= 0.0 || Ctrl.Elapsed < Duration)
			{
				break;
			}
			Ctrl.Elapsed -= Duration;
			Ctrl.Phase = (Ctrl.Phase + 1) % Ctrl.Def->Phases.Num();
			ApplyControllerPhase(Ctrl);
		}
	}

	if (!SignalSettings.bAutoCycleTrafficLights)
	{
		return;
	}
	const double G = FMath::Max(0.1, SignalSettings.GreenTime);
	const double Y = FMath::Max(0.0, SignalSettings.YellowTime);
	const double AllRed = FMath::Max(0.0, SignalSettings.AllRedTime);
	for (const FSignalCluster& Cluster : SignalClusters)
	{
		const int32 K = Cluster.Groups.Num();
		if (K == 0)
		{
			continue;
		}
		int32 Active = INDEX_NONE;
		EOpenScenarioSignalState ActiveState = EOpenScenarioSignalState::Red;
		if (K == 1)
		{
			const double Period = G + Y + 0.5 * G;
			const double T = FMath::Fmod(SimTime, Period);
			Active = 0;
			ActiveState = T < G ? EOpenScenarioSignalState::Green : (T < G + Y ? EOpenScenarioSignalState::Yellow : EOpenScenarioSignalState::Red);
		}
		else
		{
			const double Slot = G + Y + AllRed;
			const double T = FMath::Fmod(SimTime, Slot * K);
			Active = FMath::Min(K - 1, static_cast<int32>(T / Slot));
			const double Within = T - Active * Slot;
			ActiveState = Within < G ? EOpenScenarioSignalState::Green : (Within < G + Y ? EOpenScenarioSignalState::Yellow : EOpenScenarioSignalState::Red);
		}
		for (int32 i = 0; i < K; ++i)
		{
			const EOpenScenarioSignalState State = i == Active ? ActiveState : EOpenScenarioSignalState::Red;
			for (int32 SigIndex : SignalGroups[Cluster.Groups[i]].Signals)
			{
				if (!Signals[SigIndex].bManual)
				{
					SetSignalState(SigIndex, State);
				}
			}
		}
	}
}

void UOpenScenarioRunner::StartSignalAction(const FOSCAction& Action)
{
	if (Action.Type == EOSCActionType::TrafficSignalState)
	{
		if (!SetSignalStateById(Action.SignalId, ParseSignalState(Action.SignalState)))
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("TrafficSignalStateAction: unknown signal '%s'."), *Action.SignalId);
		}
		return;
	}
	for (FSignalControllerRuntime& Ctrl : SignalControllers)
	{
		if (Ctrl.Def && Ctrl.Def->Name == Action.ControllerRef)
		{
			for (int32 i = 0; i < Ctrl.Def->Phases.Num(); ++i)
			{
				if (Ctrl.Def->Phases[i].Name == Action.ControllerPhase)
				{
					Ctrl.Phase = i;
					Ctrl.Elapsed = 0.0;
					ApplyControllerPhase(Ctrl);
					return;
				}
			}
			UE_LOG(LogOpenScenario, Warning, TEXT("TrafficSignalControllerAction: controller '%s' has no phase '%s'."), *Action.ControllerRef, *Action.ControllerPhase);
			return;
		}
	}
	UE_LOG(LogOpenScenario, Warning, TEXT("TrafficSignalControllerAction: unknown controller '%s'."), *Action.ControllerRef);
}

bool UOpenScenarioRunner::JunctionOccupied(const FRuntimeSignal& Signal, const FOSCEntityState* Self) const
{
	if (Signal.JunctionId.IsEmpty() || !Map.IsValid())
	{
		return false;
	}
	for (const FOSCEntityState& Other : Entities)
	{
		if (!Other.bActive || &Other == Self || !Other.bOnRoad)
		{
			continue;
		}
		const FOpenDriveRoad* Road = Map->FindRoad(Other.RoadId);
		if (Road && Road->JunctionId == Signal.JunctionId)
		{
			return true;
		}
	}
	return false;
}

void UOpenScenarioRunner::CollectSignalsBetween(const FRoadCursor& Before, const FRoadCursor& After, double BaseDist, TFunctionRef<void(int32, double)> Callback) const
{
	if (!Map.IsValid() || Signals.Num() == 0)
	{
		return;
	}
	// Signals in [S0, S1) along the direction of travel; Dist is measured from S0.
	auto Scan = [&](const FString& RoadId, bool bForward, double S0, double S1, double Base)
	{
		const TArray<int32>* List = RoadSignals.Find(RoadId);
		if (!List)
		{
			return;
		}
		for (int32 Index : *List)
		{
			const FRuntimeSignal& Sig = Signals[Index];
			const bool bApplies = bForward ? Sig.Orientation != EOpenDriveSignalOrientation::Minus : Sig.Orientation != EOpenDriveSignalOrientation::Plus;
			if (!bApplies)
			{
				continue;
			}
			const bool bInside = bForward ? (Sig.S >= S0 - 1e-9 && Sig.S < S1) : (Sig.S <= S0 + 1e-9 && Sig.S > S1);
			if (bInside)
			{
				Callback(Index, Base + FMath::Abs(Sig.S - S0));
			}
		}
	};

	if (Before.RoadId == After.RoadId)
	{
		Scan(Before.RoadId, Before.bForward, Before.S, After.S, BaseDist);
		return;
	}
	const FOpenDriveRoad* Old = Map->FindRoad(Before.RoadId);
	if (!Old)
	{
		return;
	}
	const double End = Before.bForward ? Old->Length + 1e-6 : -1e-6;
	Scan(Before.RoadId, Before.bForward, Before.S, End, BaseDist);
	const double OldRemaining = Before.bForward ? Old->Length - Before.S : Before.S;
	const FOpenDriveRoad* Next = Map->FindRoad(After.RoadId);
	if (Next)
	{
		Scan(After.RoadId, After.bForward, After.bForward ? 0.0 : Next->Length, After.S, BaseDist + OldRemaining);
	}
}
