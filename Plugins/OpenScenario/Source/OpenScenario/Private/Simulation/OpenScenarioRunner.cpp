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

	SimTime = 0.0;
	CurrentDt = 0.0;
	Tick = 0;
	bRunning = true;
	bFinished = false;

	RunInitActions();
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
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		Entities[i].Accel = (Entities[i].Speed - PrevSpeed[i]) / Dt;
	}

	UpdateStoryboard();

	for (FOSCEntityState& E : Entities)
	{
		UpdateMotion(E, Dt);
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
	if (bStopWhenStoryboardComplete && !Scenario.StopTrigger.bPresent && Scenario.Stories.Num() > 0)
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
		Inst.V0 = E.Speed;
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
			E.Speed = Inst.VTarget;
			FinishInstance(Inst);
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
	E.Speed = Inst.V0 + (Inst.VTarget - Inst.V0) * ShapeFactor(Inst.Def->Dynamics.Shape, P);
	if (P >= 1.0)
	{
		E.Speed = Inst.VTarget;
		FinishInstance(Inst);
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

	if (bDone)
	{
		E.bTrajectoryControlled = false;
		AttachToRoad(E);
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
		AdvanceOnRoad(E, Ds);
		UpdatePoseFromRoad(E);
	}
	else
	{
		E.X += FMath::Cos(E.Heading) * Ds;
		E.Y += FMath::Sin(E.Heading) * Ds;
	}
}

void UOpenScenarioRunner::AdvanceOnRoad(FOSCEntityState& E, double Ds)
{
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
			// Dead end: the vehicle stops at the end of the road.
			E.Speed = 0.0;
			break;
		}

		const FOpenDriveRoad* Next = Map->FindRoad(Chosen->RoadId);
		if (!Next)
		{
			E.Speed = 0.0;
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
