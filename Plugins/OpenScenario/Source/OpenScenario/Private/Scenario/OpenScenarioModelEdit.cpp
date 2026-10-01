#include "Scenario/OpenScenarioModelEdit.h"
#include "OpenDrive/OpenDriveMap.h"

namespace
{
	// --- Safe index access ---------------------------------------------------------------------
	template <class T>
	T* At(TArray<T>& A, int32 I) { return A.IsValidIndex(I) ? &A[I] : nullptr; }

	FOSCStory* GetStory(FOSCScenario& S, const TArray<int32>& P) { return P.Num() > 0 ? At(S.Stories, P[0]) : nullptr; }
	FOSCAct* GetAct(FOSCScenario& S, const TArray<int32>& P)
	{
		FOSCStory* St = GetStory(S, P);
		return (St && P.Num() > 1) ? At(St->Acts, P[1]) : nullptr;
	}
	FOSCManeuverGroup* GetMG(FOSCScenario& S, const TArray<int32>& P)
	{
		FOSCAct* A = GetAct(S, P);
		return (A && P.Num() > 2) ? At(A->ManeuverGroups, P[2]) : nullptr;
	}
	FOSCManeuver* GetManeuver(FOSCScenario& S, const TArray<int32>& P)
	{
		FOSCManeuverGroup* M = GetMG(S, P);
		return (M && P.Num() > 3) ? At(M->Maneuvers, P[3]) : nullptr;
	}
	FOSCEvent* GetEvent(FOSCScenario& S, const TArray<int32>& P)
	{
		FOSCManeuver* M = GetManeuver(S, P);
		return (M && P.Num() > 4) ? At(M->Events, P[4]) : nullptr;
	}
	FOSCAction* GetEventAction(FOSCScenario& S, const TArray<int32>& P)
	{
		FOSCEvent* E = GetEvent(S, P);
		return (E && P.Num() > 5) ? At(E->Actions, P[5]) : nullptr;
	}

	TArray<int32> Prefix(const TArray<int32>& P, int32 N)
	{
		TArray<int32> R;
		for (int32 i = 0; i < N && i < P.Num(); ++i) { R.Add(P[i]); }
		return R;
	}

	FOSCTrigger* GetTriggerByOwner(FOSCScenario& S, EOSCTriggerOwner Owner, const TArray<int32>& OwnerPath)
	{
		switch (Owner)
		{
		case EOSCTriggerOwner::ActStart: { FOSCAct* A = GetAct(S, OwnerPath); return A ? &A->StartTrigger : nullptr; }
		case EOSCTriggerOwner::ActStop: { FOSCAct* A = GetAct(S, OwnerPath); return A ? &A->StopTrigger : nullptr; }
		case EOSCTriggerOwner::EventStart: { FOSCEvent* E = GetEvent(S, OwnerPath); return E ? &E->StartTrigger : nullptr; }
		case EOSCTriggerOwner::StoryboardStop: return &S.StopTrigger;
		default: return nullptr;
		}
	}

	FOSCTrigger* GetTriggerOfNode(FOSCScenario& S, const FOSCNodeRef& N)
	{
		switch (N.Type)
		{
		case EOSCNodeType::Trigger: return GetTriggerByOwner(S, N.Owner, N.Path);
		case EOSCNodeType::ConditionGroup: return GetTriggerByOwner(S, N.Owner, Prefix(N.Path, N.Path.Num() - 1));
		case EOSCNodeType::Condition: return GetTriggerByOwner(S, N.Owner, Prefix(N.Path, N.Path.Num() - 2));
		default: return nullptr;
		}
	}

	// --- Defaults for new elements -------------------------------------------------------------
	FOSCCondition DefaultCondition(const TCHAR* Name, double Time)
	{
		FOSCCondition C;
		C.Name = Name;
		C.Type = EOSCConditionType::SimulationTime;
		C.Rule = EOSCRule::GreaterThan;
		C.Value = Time;
		C.Edge = EOSCEdge::Rising;
		return C;
	}

	FOSCTrigger DefaultTrigger(const TCHAR* CondName, double Time, EOSCRule Rule = EOSCRule::GreaterThan)
	{
		FOSCTrigger T;
		T.bPresent = true;
		FOSCConditionGroup G;
		FOSCCondition C = DefaultCondition(CondName, Time);
		C.Rule = Rule;
		G.Conditions.Add(C);
		T.Groups.Add(G);
		return T;
	}

	FString Numbered(const TCHAR* Base, int32 Count)
	{
		return FString::Printf(TEXT("%s %d"), Base, Count + 1);
	}

	FOSCAction DefaultTeleport()
	{
		FOSCAction A;
		A.Name = TEXT("Teleport");
		A.Type = EOSCActionType::Teleport;
		A.Position.Type = EOSCPositionType::World;
		A.Position.bHasOrientation = true;
		return A;
	}

	FOSCAction DefaultSpeed(const FString& Name)
	{
		FOSCAction A;
		A.Name = Name;
		A.Type = EOSCActionType::Speed;
		A.Dynamics.Shape = EOSCShape::Step;
		A.SpeedValue = 0.0;
		return A;
	}

	// Per-type initialisation of a freshly added element.
	void InitDefault(FOSCEntity& E, const FOSCScenario& S)
	{
		FString Name;
		for (int32 i = S.Entities.Num(); ; ++i)
		{
			Name = FString::Printf(TEXT("Vehicle%d"), i);
			if (!S.FindEntity(Name)) { break; }
		}
		E.Name = Name;
		E.Category = TEXT("car");
	}
	void InitDefault(FOSCInitActions& G, const FOSCScenario& S)
	{
		for (const FOSCEntity& E : S.Entities)
		{
			bool bUsed = false;
			for (const FOSCInitActions& Other : S.InitActions) { bUsed |= (Other.EntityRef == E.Name); }
			if (!bUsed) { G.EntityRef = E.Name; break; }
		}
		if (G.EntityRef.IsEmpty() && S.Entities.Num() > 0) { G.EntityRef = S.Entities[0].Name; }
		G.Actions.Add(DefaultTeleport());
	}
	void InitDefault(FOSCStory& V, const FOSCScenario& S) { V.Name = Numbered(TEXT("Story"), S.Stories.Num()); }
	void InitDefault(FOSCAct& V, const FOSCScenario&) { V.Name = TEXT("Act"); V.StartTrigger = DefaultTrigger(TEXT("Start"), 0.0, EOSCRule::GreaterOrEqual); }
	void InitDefault(FOSCManeuverGroup& V, const FOSCScenario& S) { V.Name = TEXT("ManeuverGroup"); if (S.Entities.Num() > 0) { V.Actors.Add(S.Entities[0].Name); } }
	void InitDefault(FOSCManeuver& V, const FOSCScenario&) { V.Name = TEXT("Maneuver"); }
	void InitDefault(FOSCEvent& V, const FOSCScenario&) { V.Name = TEXT("Event"); V.StartTrigger = DefaultTrigger(TEXT("EventStart"), 0.0); V.Actions.Add(DefaultSpeed(TEXT("Action"))); }
	void InitDefault(FOSCAction& V, const FOSCScenario&) { V = DefaultSpeed(TEXT("Action")); }
	void InitDefault(FOSCConditionGroup& V, const FOSCScenario&) { V.Conditions.Add(DefaultCondition(TEXT("Condition"), 0.0)); }
	void InitDefault(FOSCCondition& V, const FOSCScenario&) { V = DefaultCondition(TEXT("Condition"), 0.0); }

	template <class T> void NameCopy(T&) {}
	void NameCopy(FOSCStory& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCAct& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCManeuverGroup& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCManeuver& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCEvent& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCAction& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCCondition& V) { V.Name += TEXT(" Copy"); }
	void NameCopy(FOSCEntity& V) { V.Name += TEXT("Copy"); }

	/** Type-erased access to one of the model's arrays. */
	struct FArrayAccessor
	{
		TFunction<int32()> Num;
		TFunction<int32()> AddNew;
		TFunction<void(int32)> RemoveAt;
		TFunction<void(int32)> DuplicateAt;
		TFunction<void(int32, int32)> Swap;
		bool IsValid() const { return (bool)Num; }
	};

	template <class T>
	FArrayAccessor MakeAccessor(TArray<T>& A, FOSCScenario& S)
	{
		FArrayAccessor R;
		R.Num = [&A]() { return A.Num(); };
		R.AddNew = [&A, &S]() { const int32 I = A.AddDefaulted(); InitDefault(A[I], S); return I; };
		R.RemoveAt = [&A](int32 I) { A.RemoveAt(I); };
		R.DuplicateAt = [&A](int32 I) { T Copy = A[I]; NameCopy(Copy); A.Insert(Copy, I + 1); };
		R.Swap = [&A](int32 I, int32 J) { A.Swap(I, J); };
		return R;
	}

	/** The array that contains children of `ChildType` below `Parent`. */
	FArrayAccessor GetArray(FOSCScenario& S, const FOSCNodeRef& Parent, EOSCNodeType ChildType)
	{
		const TArray<int32>& P = Parent.Path;
		switch (ChildType)
		{
		case EOSCNodeType::Entity: return MakeAccessor(S.Entities, S);
		case EOSCNodeType::InitGroup: return MakeAccessor(S.InitActions, S);
		case EOSCNodeType::InitAction: if (FOSCInitActions* G = P.Num() > 0 ? At(S.InitActions, P[0]) : nullptr) { return MakeAccessor(G->Actions, S); } break;
		case EOSCNodeType::Story: return MakeAccessor(S.Stories, S);
		case EOSCNodeType::Act: if (FOSCStory* St = GetStory(S, P)) { return MakeAccessor(St->Acts, S); } break;
		case EOSCNodeType::ManeuverGroup: if (FOSCAct* A = GetAct(S, P)) { return MakeAccessor(A->ManeuverGroups, S); } break;
		case EOSCNodeType::Maneuver: if (FOSCManeuverGroup* M = GetMG(S, P)) { return MakeAccessor(M->Maneuvers, S); } break;
		case EOSCNodeType::Event: if (FOSCManeuver* M = GetManeuver(S, P)) { return MakeAccessor(M->Events, S); } break;
		case EOSCNodeType::Action: if (FOSCEvent* E = GetEvent(S, P)) { return MakeAccessor(E->Actions, S); } break;
		case EOSCNodeType::ConditionGroup: if (FOSCTrigger* T = GetTriggerOfNode(S, Parent)) { return MakeAccessor(T->Groups, S); } break;
		case EOSCNodeType::Condition:
			if (FOSCTrigger* T = GetTriggerOfNode(S, Parent))
			{
				if (P.Num() > 0 && T->Groups.IsValidIndex(P.Last())) { return MakeAccessor(T->Groups[P.Last()].Conditions, S); }
			}
			break;
		default: break;
		}
		return FArrayAccessor();
	}

	const TCHAR* RuleSymbol(EOSCRule R)
	{
		switch (R)
		{
		case EOSCRule::EqualTo: return TEXT("==");
		case EOSCRule::NotEqualTo: return TEXT("!=");
		case EOSCRule::LessThan: return TEXT("<");
		case EOSCRule::LessOrEqual: return TEXT("<=");
		case EOSCRule::GreaterOrEqual: return TEXT(">=");
		default: return TEXT(">");
		}
	}

	FString Num(double V) { return FString::SanitizeFloat(V); }
}

// ------------------------------------------------------------------------------------------------
// FOSCNodeRef
// ------------------------------------------------------------------------------------------------

FString FOSCNodeRef::Key() const
{
	FString K = FString::Printf(TEXT("%d/%d"), (int32)Type, (int32)Owner);
	for (int32 I : Path)
	{
		K += FString::Printf(TEXT(".%d"), I);
	}
	return K;
}

FString FOSCModelEdit::TypeName(EOSCNodeType Type)
{
	switch (Type)
	{
	case EOSCNodeType::Storyboard: return TEXT("Storyboard");
	case EOSCNodeType::Entities: return TEXT("Entities");
	case EOSCNodeType::Entity: return TEXT("Entity");
	case EOSCNodeType::Init: return TEXT("Init");
	case EOSCNodeType::InitGroup: return TEXT("Init Actions (Entity)");
	case EOSCNodeType::InitAction: return TEXT("Init Action");
	case EOSCNodeType::Story: return TEXT("Story");
	case EOSCNodeType::Act: return TEXT("Act");
	case EOSCNodeType::ManeuverGroup: return TEXT("Maneuver Group");
	case EOSCNodeType::Maneuver: return TEXT("Maneuver");
	case EOSCNodeType::Event: return TEXT("Event");
	case EOSCNodeType::Action: return TEXT("Action");
	case EOSCNodeType::Trigger: return TEXT("Trigger");
	case EOSCNodeType::ConditionGroup: return TEXT("Condition Group");
	case EOSCNodeType::Condition: return TEXT("Condition");
	default: return TEXT("None");
	}
}

// ------------------------------------------------------------------------------------------------
// Data access, tree structure
// ------------------------------------------------------------------------------------------------

void* FOSCModelEdit::GetData(FOSCScenario& S, const FOSCNodeRef& N)
{
	const TArray<int32>& P = N.Path;
	switch (N.Type)
	{
	case EOSCNodeType::Storyboard: return &S;
	case EOSCNodeType::Entity: return P.Num() > 0 ? At(S.Entities, P[0]) : nullptr;
	case EOSCNodeType::InitGroup: return P.Num() > 0 ? At(S.InitActions, P[0]) : nullptr;
	case EOSCNodeType::InitAction:
	{
		FOSCInitActions* G = P.Num() > 0 ? At(S.InitActions, P[0]) : nullptr;
		return (G && P.Num() > 1) ? At(G->Actions, P[1]) : nullptr;
	}
	case EOSCNodeType::Story: return GetStory(S, P);
	case EOSCNodeType::Act: return GetAct(S, P);
	case EOSCNodeType::ManeuverGroup: return GetMG(S, P);
	case EOSCNodeType::Maneuver: return GetManeuver(S, P);
	case EOSCNodeType::Event: return GetEvent(S, P);
	case EOSCNodeType::Action: return GetEventAction(S, P);
	case EOSCNodeType::Trigger: return GetTriggerOfNode(S, N);
	case EOSCNodeType::ConditionGroup:
	{
		FOSCTrigger* T = GetTriggerOfNode(S, N);
		return (T && P.Num() > 0) ? At(T->Groups, P.Last()) : nullptr;
	}
	case EOSCNodeType::Condition:
	{
		FOSCTrigger* T = GetTriggerOfNode(S, N);
		if (!T || P.Num() < 2) { return nullptr; }
		FOSCConditionGroup* G = At(T->Groups, P[P.Num() - 2]);
		return G ? At(G->Conditions, P.Last()) : nullptr;
	}
	default: return nullptr;
	}
}

void FOSCModelEdit::GetChildren(const FOSCScenario& Scenario, const FOSCNodeRef& N, TArray<FOSCNodeRef>& Out)
{
	Out.Reset();
	// The const_cast is only used for read access through the shared address helpers.
	FOSCScenario& S = const_cast<FOSCScenario&>(Scenario);
	auto WithIndex = [&N](EOSCNodeType Type, int32 Index, EOSCTriggerOwner Owner = EOSCTriggerOwner::None)
	{
		FOSCNodeRef R;
		R.Type = Type;
		R.Owner = Owner;
		R.Path = N.Path;
		R.Path.Add(Index);
		return R;
	};

	switch (N.Type)
	{
	case EOSCNodeType::Storyboard:
		Out.Add(FOSCNodeRef(EOSCNodeType::Entities));
		Out.Add(FOSCNodeRef(EOSCNodeType::Init));
		for (int32 i = 0; i < S.Stories.Num(); ++i) { Out.Add(FOSCNodeRef(EOSCNodeType::Story, { i })); }
		Out.Add(FOSCNodeRef(EOSCNodeType::Trigger, {}, EOSCTriggerOwner::StoryboardStop));
		break;
	case EOSCNodeType::Entities:
		for (int32 i = 0; i < S.Entities.Num(); ++i) { Out.Add(FOSCNodeRef(EOSCNodeType::Entity, { i })); }
		break;
	case EOSCNodeType::Init:
		for (int32 i = 0; i < S.InitActions.Num(); ++i) { Out.Add(FOSCNodeRef(EOSCNodeType::InitGroup, { i })); }
		break;
	case EOSCNodeType::InitGroup:
		if (FOSCInitActions* G = At(S.InitActions, N.Path.Num() > 0 ? N.Path[0] : -1))
		{
			for (int32 i = 0; i < G->Actions.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::InitAction, i)); }
		}
		break;
	case EOSCNodeType::Story:
		if (FOSCStory* St = GetStory(S, N.Path))
		{
			for (int32 i = 0; i < St->Acts.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::Act, i)); }
		}
		break;
	case EOSCNodeType::Act:
		if (FOSCAct* A = GetAct(S, N.Path))
		{
			Out.Add(FOSCNodeRef(EOSCNodeType::Trigger, {}, EOSCTriggerOwner::ActStart));
			Out.Last().Path = N.Path;
			Out.Add(FOSCNodeRef(EOSCNodeType::Trigger, {}, EOSCTriggerOwner::ActStop));
			Out.Last().Path = N.Path;
			for (int32 i = 0; i < A->ManeuverGroups.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::ManeuverGroup, i)); }
		}
		break;
	case EOSCNodeType::ManeuverGroup:
		if (FOSCManeuverGroup* M = GetMG(S, N.Path))
		{
			for (int32 i = 0; i < M->Maneuvers.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::Maneuver, i)); }
		}
		break;
	case EOSCNodeType::Maneuver:
		if (FOSCManeuver* M = GetManeuver(S, N.Path))
		{
			for (int32 i = 0; i < M->Events.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::Event, i)); }
		}
		break;
	case EOSCNodeType::Event:
		if (FOSCEvent* E = GetEvent(S, N.Path))
		{
			Out.Add(FOSCNodeRef(EOSCNodeType::Trigger, {}, EOSCTriggerOwner::EventStart));
			Out.Last().Path = N.Path;
			for (int32 i = 0; i < E->Actions.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::Action, i)); }
		}
		break;
	case EOSCNodeType::Trigger:
		if (FOSCTrigger* T = GetTriggerOfNode(S, N))
		{
			for (int32 i = 0; i < T->Groups.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::ConditionGroup, i, N.Owner)); }
		}
		break;
	case EOSCNodeType::ConditionGroup:
		if (FOSCTrigger* T = GetTriggerOfNode(S, N))
		{
			if (FOSCConditionGroup* G = N.Path.Num() > 0 ? At(T->Groups, N.Path.Last()) : nullptr)
			{
				for (int32 i = 0; i < G->Conditions.Num(); ++i) { Out.Add(WithIndex(EOSCNodeType::Condition, i, N.Owner)); }
			}
		}
		break;
	default:
		break;
	}
}

bool FOSCModelEdit::GetParent(const FOSCNodeRef& N, FOSCNodeRef& Out)
{
	Out = FOSCNodeRef();
	auto Set = [&Out, &N](EOSCNodeType Type, int32 Keep, EOSCTriggerOwner Owner = EOSCTriggerOwner::None)
	{
		Out.Type = Type;
		Out.Owner = Owner;
		Out.Path = Prefix(N.Path, Keep);
	};
	switch (N.Type)
	{
	case EOSCNodeType::Entities:
	case EOSCNodeType::Init: Set(EOSCNodeType::Storyboard, 0); return true;
	case EOSCNodeType::Entity: Set(EOSCNodeType::Entities, 0); return true;
	case EOSCNodeType::InitGroup: Set(EOSCNodeType::Init, 0); return true;
	case EOSCNodeType::InitAction: Set(EOSCNodeType::InitGroup, 1); return true;
	case EOSCNodeType::Story: Set(EOSCNodeType::Storyboard, 0); return true;
	case EOSCNodeType::Act: Set(EOSCNodeType::Story, 1); return true;
	case EOSCNodeType::ManeuverGroup: Set(EOSCNodeType::Act, 2); return true;
	case EOSCNodeType::Maneuver: Set(EOSCNodeType::ManeuverGroup, 3); return true;
	case EOSCNodeType::Event: Set(EOSCNodeType::Maneuver, 4); return true;
	case EOSCNodeType::Action: Set(EOSCNodeType::Event, 5); return true;
	case EOSCNodeType::Trigger:
		switch (N.Owner)
		{
		case EOSCTriggerOwner::ActStart:
		case EOSCTriggerOwner::ActStop: Set(EOSCNodeType::Act, 2); return true;
		case EOSCTriggerOwner::EventStart: Set(EOSCNodeType::Event, 5); return true;
		case EOSCTriggerOwner::StoryboardStop: Set(EOSCNodeType::Storyboard, 0); return true;
		default: return false;
		}
	case EOSCNodeType::ConditionGroup: Set(EOSCNodeType::Trigger, N.Path.Num() - 1, N.Owner); return true;
	case EOSCNodeType::Condition: Set(EOSCNodeType::ConditionGroup, N.Path.Num() - 1, N.Owner); return true;
	default: return false;
	}
}

// ------------------------------------------------------------------------------------------------
// Editing
// ------------------------------------------------------------------------------------------------

void FOSCModelEdit::GetAddableChildren(const FOSCNodeRef& Parent, TArray<EOSCNodeType>& Out)
{
	Out.Reset();
	switch (Parent.Type)
	{
	case EOSCNodeType::Storyboard: Out.Add(EOSCNodeType::Story); break;
	case EOSCNodeType::Entities: Out.Add(EOSCNodeType::Entity); break;
	case EOSCNodeType::Init: Out.Add(EOSCNodeType::InitGroup); break;
	case EOSCNodeType::InitGroup: Out.Add(EOSCNodeType::InitAction); break;
	case EOSCNodeType::Story: Out.Add(EOSCNodeType::Act); break;
	case EOSCNodeType::Act: Out.Add(EOSCNodeType::ManeuverGroup); break;
	case EOSCNodeType::ManeuverGroup: Out.Add(EOSCNodeType::Maneuver); break;
	case EOSCNodeType::Maneuver: Out.Add(EOSCNodeType::Event); break;
	case EOSCNodeType::Event: Out.Add(EOSCNodeType::Action); break;
	case EOSCNodeType::Trigger: Out.Add(EOSCNodeType::ConditionGroup); break;
	case EOSCNodeType::ConditionGroup: Out.Add(EOSCNodeType::Condition); break;
	default: break;
	}
}

bool FOSCModelEdit::AddChild(FOSCScenario& S, const FOSCNodeRef& Parent, EOSCNodeType ChildType, FOSCNodeRef& OutNew)
{
	TArray<EOSCNodeType> Allowed;
	GetAddableChildren(Parent, Allowed);
	if (!Allowed.Contains(ChildType))
	{
		return false;
	}
	FArrayAccessor Acc = GetArray(S, Parent, ChildType);
	if (!Acc.IsValid())
	{
		return false;
	}
	if (Parent.Type == EOSCNodeType::Trigger)
	{
		if (FOSCTrigger* T = GetTriggerOfNode(S, Parent)) { T->bPresent = true; }
	}
	const int32 Index = Acc.AddNew();

	OutNew = FOSCNodeRef();
	OutNew.Type = ChildType;
	OutNew.Owner = Parent.Owner;
	// Children below a Trigger/ConditionGroup keep the trigger path; everything else appends to the parent's path.
	OutNew.Path = Parent.Path;
	OutNew.Path.Add(Index);
	return true;
}

bool FOSCModelEdit::CanRemove(const FOSCNodeRef& N)
{
	switch (N.Type)
	{
	case EOSCNodeType::Storyboard:
	case EOSCNodeType::Entities:
	case EOSCNodeType::Init:
	case EOSCNodeType::None:
		return false;
	case EOSCNodeType::Trigger:
		return N.Owner == EOSCTriggerOwner::ActStop || N.Owner == EOSCTriggerOwner::StoryboardStop;
	default:
		return true;
	}
}

bool FOSCModelEdit::Remove(FOSCScenario& S, const FOSCNodeRef& N)
{
	if (!CanRemove(N))
	{
		return false;
	}
	if (N.Type == EOSCNodeType::Trigger)
	{
		FOSCTrigger* T = GetTriggerOfNode(S, N);
		if (!T) { return false; }
		T->Groups.Reset();
		T->bPresent = false;
		return true;
	}
	FOSCNodeRef Parent;
	if (!GetParent(N, Parent) || N.Path.Num() == 0)
	{
		return false;
	}
	FArrayAccessor Acc = GetArray(S, Parent, N.Type);
	const int32 Index = N.Path.Last();
	if (!Acc.IsValid() || Index < 0 || Index >= Acc.Num())
	{
		return false;
	}
	Acc.RemoveAt(Index);
	return true;
}

bool FOSCModelEdit::Duplicate(FOSCScenario& S, const FOSCNodeRef& N, FOSCNodeRef& OutNew)
{
	if (!CanRemove(N) || N.Type == EOSCNodeType::Trigger)
	{
		return false;
	}
	FOSCNodeRef Parent;
	if (!GetParent(N, Parent) || N.Path.Num() == 0)
	{
		return false;
	}
	FArrayAccessor Acc = GetArray(S, Parent, N.Type);
	const int32 Index = N.Path.Last();
	if (!Acc.IsValid() || Index < 0 || Index >= Acc.Num())
	{
		return false;
	}
	Acc.DuplicateAt(Index);
	OutNew = N;
	OutNew.Path.Last() = Index + 1;
	return true;
}

bool FOSCModelEdit::Move(FOSCScenario& S, const FOSCNodeRef& N, int32 Delta, FOSCNodeRef& OutNew)
{
	if (!CanRemove(N) || N.Type == EOSCNodeType::Trigger)
	{
		return false;
	}
	FOSCNodeRef Parent;
	if (!GetParent(N, Parent) || N.Path.Num() == 0)
	{
		return false;
	}
	FArrayAccessor Acc = GetArray(S, Parent, N.Type);
	const int32 Index = N.Path.Last();
	const int32 Target = Index + Delta;
	if (!Acc.IsValid() || Index < 0 || Index >= Acc.Num() || Target < 0 || Target >= Acc.Num())
	{
		return false;
	}
	Acc.Swap(Index, Target);
	OutNew = N;
	OutNew.Path.Last() = Target;
	return true;
}

// ------------------------------------------------------------------------------------------------
// Descriptions
// ------------------------------------------------------------------------------------------------

FString FOSCModelEdit::DescribePosition(const FOSCPosition& P)
{
	switch (P.Type)
	{
	case EOSCPositionType::World: return FString::Printf(TEXT("world (%s, %s)"), *Num(P.X), *Num(P.Y));
	case EOSCPositionType::Road: return FString::Printf(TEXT("road %s s=%s t=%s"), *P.RoadId, *Num(P.S), *Num(P.T));
	case EOSCPositionType::Lane: return FString::Printf(TEXT("road %s lane %d s=%s"), *P.RoadId, P.LaneId, *Num(P.S));
	case EOSCPositionType::RelativeWorld: return FString::Printf(TEXT("%s + (%s, %s)"), *P.EntityRef, *Num(P.DX), *Num(P.DY));
	case EOSCPositionType::RelativeObject: return FString::Printf(TEXT("%s rel (%s, %s)"), *P.EntityRef, *Num(P.DX), *Num(P.DY));
	case EOSCPositionType::RelativeLane: return FString::Printf(TEXT("%s lane %+d ds=%s"), *P.EntityRef, P.DLane, *Num(P.DS));
	default: return TEXT("(no position)");
	}
}

FString FOSCModelEdit::DescribeAction(const FOSCAction& A)
{
	switch (A.Type)
	{
	case EOSCActionType::Teleport:
		return FString::Printf(TEXT("Teleport to %s"), *DescribePosition(A.Position));
	case EOSCActionType::Speed:
		return FString::Printf(TEXT("Speed %s%s m/s"), A.bSpeedRelative ? (A.bSpeedFactor ? TEXT("x") : TEXT("+")) : TEXT("="), *Num(A.SpeedValue));
	case EOSCActionType::LaneChange:
		return A.bLaneRelative ? FString::Printf(TEXT("Lane change %+d rel. %s"), A.LaneValue, *A.RefEntity) : FString::Printf(TEXT("Lane change to lane %d"), A.LaneValue);
	case EOSCActionType::AssignRoute:
		return FString::Printf(TEXT("Route (%d waypoints)"), A.Waypoints.Num());
	case EOSCActionType::FollowTrajectory:
		return FString::Printf(TEXT("Trajectory (%d vertices)"), A.Vertices.Num());
	case EOSCActionType::Traffic:
		switch (A.Traffic.Kind)
		{
		case EOSCTrafficKind::Swarm: return FString::Printf(TEXT("Traffic swarm: %d around %s"), A.Traffic.NumberOfVehicles, *A.Traffic.CentralObject);
		case EOSCTrafficKind::Source: return FString::Printf(TEXT("Traffic source: %s/s at %s"), *Num(A.Traffic.Rate), *DescribePosition(A.Traffic.Position));
		case EOSCTrafficKind::Sink: return FString::Printf(TEXT("Traffic sink: %s/s at %s"), *Num(A.Traffic.Rate), *DescribePosition(A.Traffic.Position));
		default: return FString::Printf(TEXT("Stop traffic '%s'"), *A.Traffic.TrafficName);
		}
	default:
		return FString::Printf(TEXT("Unsupported <%s>"), *A.UnsupportedTag);
	}
}

FString FOSCModelEdit::DescribeCondition(const FOSCCondition& C)
{
	const FString Who = C.TriggeringEntities.Num() > 0 ? C.TriggeringEntities[0] : FString(TEXT("?"));
	switch (C.Type)
	{
	case EOSCConditionType::SimulationTime: return FString::Printf(TEXT("time %s %s s"), RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::StoryboardElementState: return FString::Printf(TEXT("%s '%s' %s"), *C.ElementType, *C.ElementRef, *C.ElementState);
	case EOSCConditionType::Parameter: return FString::Printf(TEXT("param %s %s %s"), *C.ParameterRef, RuleSymbol(C.Rule), *C.StringValue);
	case EOSCConditionType::Speed: return FString::Printf(TEXT("%s speed %s %s"), *Who, RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::RelativeSpeed: return FString::Printf(TEXT("%s rel. speed to %s %s %s"), *Who, *C.EntityRef, RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::Acceleration: return FString::Printf(TEXT("%s accel %s %s"), *Who, RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::TraveledDistance: return FString::Printf(TEXT("%s travelled > %s m"), *Who, *Num(C.Value));
	case EOSCConditionType::StandStill: return FString::Printf(TEXT("%s stands still"), *Who);
	case EOSCConditionType::ReachPosition: return FString::Printf(TEXT("%s reaches %s (tol %s)"), *Who, *DescribePosition(C.Position), *Num(C.Tolerance));
	case EOSCConditionType::Distance: return FString::Printf(TEXT("%s dist to %s %s %s"), *Who, *DescribePosition(C.Position), RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::RelativeDistance: return FString::Printf(TEXT("%s dist to %s %s %s"), *Who, *C.EntityRef, RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::TimeHeadway: return FString::Printf(TEXT("%s headway to %s %s %s"), *Who, *C.EntityRef, RuleSymbol(C.Rule), *Num(C.Value));
	case EOSCConditionType::Collision: return FString::Printf(TEXT("%s collides with %s"), *Who, *C.EntityRef);
	default: return FString::Printf(TEXT("Unsupported <%s>"), *C.UnsupportedTag);
	}
}

FString FOSCModelEdit::Describe(const FOSCScenario& Scenario, const FOSCNodeRef& N)
{
	FOSCScenario& S = const_cast<FOSCScenario&>(Scenario);
	void* Data = GetData(S, N);
	switch (N.Type)
	{
	case EOSCNodeType::Storyboard:
		return FString::Printf(TEXT("Storyboard: %s"), Scenario.Description.IsEmpty() ? TEXT("(unnamed scenario)") : *Scenario.Description);
	case EOSCNodeType::Entities: return FString::Printf(TEXT("Entities (%d)"), Scenario.Entities.Num());
	case EOSCNodeType::Init: return FString::Printf(TEXT("Init (%d)"), Scenario.InitActions.Num());
	default: break;
	}
	if (!Data && N.Type != EOSCNodeType::Trigger)
	{
		return TEXT("(invalid)");
	}
	switch (N.Type)
	{
	case EOSCNodeType::Entity:
	{
		const FOSCEntity* E = static_cast<FOSCEntity*>(Data);
		const TCHAR* Kind = E->Kind == EOSCEntityKind::Pedestrian ? TEXT("Pedestrian") : E->Kind == EOSCEntityKind::MiscObject ? TEXT("Object") : E->Kind == EOSCEntityKind::External ? TEXT("External") : TEXT("Vehicle");
		return FString::Printf(TEXT("%s: %s"), Kind, *E->Name);
	}
	case EOSCNodeType::InitGroup:
	{
		const FOSCInitActions* G = static_cast<FOSCInitActions*>(Data);
		return FString::Printf(TEXT("%s (%d actions)"), G->EntityRef.IsEmpty() ? TEXT("(global)") : *G->EntityRef, G->Actions.Num());
	}
	case EOSCNodeType::InitAction: return DescribeAction(*static_cast<FOSCAction*>(Data));
	case EOSCNodeType::Story: return FString::Printf(TEXT("Story: %s"), *static_cast<FOSCStory*>(Data)->Name);
	case EOSCNodeType::Act: return FString::Printf(TEXT("Act: %s"), *static_cast<FOSCAct*>(Data)->Name);
	case EOSCNodeType::ManeuverGroup:
	{
		const FOSCManeuverGroup* M = static_cast<FOSCManeuverGroup*>(Data);
		FString Actors;
		for (const FString& A : M->Actors) { Actors += (Actors.IsEmpty() ? TEXT("") : TEXT(", ")) + A; }
		return FString::Printf(TEXT("Maneuver Group: %s [%s]"), *M->Name, *Actors);
	}
	case EOSCNodeType::Maneuver: return FString::Printf(TEXT("Maneuver: %s"), *static_cast<FOSCManeuver*>(Data)->Name);
	case EOSCNodeType::Event: return FString::Printf(TEXT("Event: %s"), *static_cast<FOSCEvent*>(Data)->Name);
	case EOSCNodeType::Action:
	{
		const FOSCAction* A = static_cast<FOSCAction*>(Data);
		return FString::Printf(TEXT("%s: %s"), *A->Name, *DescribeAction(*A));
	}
	case EOSCNodeType::Trigger:
	{
		const FOSCTrigger* T = static_cast<FOSCTrigger*>(Data);
		const TCHAR* Label = N.Owner == EOSCTriggerOwner::ActStop || N.Owner == EOSCTriggerOwner::StoryboardStop ? TEXT("Stop Trigger") : TEXT("Start Trigger");
		return FString::Printf(TEXT("%s%s"), Label, (T && T->Groups.Num() > 0) ? TEXT("") : TEXT(" (none)"));
	}
	case EOSCNodeType::ConditionGroup: return FString::Printf(TEXT("All of (group %d)"), N.Path.Num() > 0 ? N.Path.Last() + 1 : 0);
	case EOSCNodeType::Condition:
	{
		const FOSCCondition* C = static_cast<FOSCCondition*>(Data);
		return FString::Printf(TEXT("%s: %s"), *C->Name, *DescribeCondition(*C));
	}
	default: return TEXT("?");
	}
}

// ------------------------------------------------------------------------------------------------
// Validation, static pose
// ------------------------------------------------------------------------------------------------

void FOSCModelEdit::Validate(const FOSCScenario& S, TArray<FString>& Issues)
{
	Issues.Reset();
	auto CheckEntity = [&](const FString& Name, const FString& Where)
	{
		if (!Name.IsEmpty() && !S.FindEntity(Name))
		{
			Issues.Add(FString::Printf(TEXT("%s refers to unknown entity '%s'."), *Where, *Name));
		}
	};
	auto CheckPosition = [&](const FOSCPosition& P, const FString& Where)
	{
		if (P.Type == EOSCPositionType::RelativeWorld || P.Type == EOSCPositionType::RelativeObject || P.Type == EOSCPositionType::RelativeLane)
		{
			CheckEntity(P.EntityRef, Where);
		}
	};
	auto CheckAction = [&](const FOSCAction& A, const FString& Where)
	{
		if (A.Type == EOSCActionType::Unsupported) { Issues.Add(FString::Printf(TEXT("%s: unsupported action <%s> will not be saved."), *Where, *A.UnsupportedTag)); }
		if ((A.Type == EOSCActionType::Speed && A.bSpeedRelative) || (A.Type == EOSCActionType::LaneChange && A.bLaneRelative)) { CheckEntity(A.RefEntity, Where); }
		CheckPosition(A.Position, Where);
		if (A.Type == EOSCActionType::Traffic)
		{
			if (A.Traffic.Kind == EOSCTrafficKind::Swarm) { CheckEntity(A.Traffic.CentralObject, Where); }
			CheckPosition(A.Traffic.Position, Where);
		}
		for (const FOSCPosition& W : A.Waypoints) { CheckPosition(W, Where); }
		for (const FOSCTrajectoryVertex& V : A.Vertices) { CheckPosition(V.Position, Where); }
	};
	auto CheckTrigger = [&](const FOSCTrigger& T, const FString& Where)
	{
		for (const FOSCConditionGroup& G : T.Groups)
		{
			for (const FOSCCondition& C : G.Conditions)
			{
				const FString W = FString::Printf(TEXT("%s / %s"), *Where, *C.Name);
				if (C.Type == EOSCConditionType::Unsupported) { Issues.Add(FString::Printf(TEXT("%s: unsupported condition <%s> will not be saved."), *W, *C.UnsupportedTag)); }
				for (const FString& E : C.TriggeringEntities) { CheckEntity(E, W); }
				if (C.Type == EOSCConditionType::RelativeSpeed || C.Type == EOSCConditionType::RelativeDistance || C.Type == EOSCConditionType::TimeHeadway || C.Type == EOSCConditionType::Collision) { CheckEntity(C.EntityRef, W); }
				CheckPosition(C.Position, W);
			}
		}
	};

	for (int32 i = 0; i < S.Entities.Num(); ++i)
	{
		if (S.Entities[i].Name.IsEmpty()) { Issues.Add(FString::Printf(TEXT("Entity %d has no name."), i + 1)); }
		for (int32 j = i + 1; j < S.Entities.Num(); ++j)
		{
			if (S.Entities[i].Name == S.Entities[j].Name) { Issues.Add(FString::Printf(TEXT("Duplicate entity name '%s'."), *S.Entities[i].Name)); }
		}
	}
	for (const FOSCInitActions& G : S.InitActions)
	{
		CheckEntity(G.EntityRef, TEXT("Init"));
		for (const FOSCAction& A : G.Actions) { CheckAction(A, FString::Printf(TEXT("Init/%s"), *G.EntityRef)); }
	}
	for (const FOSCStory& St : S.Stories)
	{
		for (const FOSCAct& Act : St.Acts)
		{
			CheckTrigger(Act.StartTrigger, FString::Printf(TEXT("Act %s start"), *Act.Name));
			CheckTrigger(Act.StopTrigger, FString::Printf(TEXT("Act %s stop"), *Act.Name));
			for (const FOSCManeuverGroup& MG : Act.ManeuverGroups)
			{
				for (const FString& A : MG.Actors) { CheckEntity(A, FString::Printf(TEXT("Maneuver group %s"), *MG.Name)); }
				if (MG.Actors.Num() == 0) { Issues.Add(FString::Printf(TEXT("Maneuver group '%s' has no actors."), *MG.Name)); }
				for (const FOSCManeuver& Man : MG.Maneuvers)
				{
					for (const FOSCEvent& Ev : Man.Events)
					{
						const FString W = FString::Printf(TEXT("Event %s"), *Ev.Name);
						CheckTrigger(Ev.StartTrigger, W);
						for (const FOSCAction& A : Ev.Actions) { CheckAction(A, W); }
					}
				}
			}
		}
	}
	CheckTrigger(S.StopTrigger, TEXT("Storyboard stop"));
}

bool FOSCModelEdit::ResolveStaticPose(const FOpenDriveMap* Map, const FOSCPosition& P, double& X, double& Y, double& Z, double& H)
{
	constexpr double Pi = UE_DOUBLE_PI;
	switch (P.Type)
	{
	case EOSCPositionType::World:
		X = P.X; Y = P.Y; Z = P.Z;
		H = P.bHasOrientation ? P.H : 0.0;
		return true;
	case EOSCPositionType::Road:
	case EOSCPositionType::Lane:
	{
		const FOpenDriveRoad* Road = Map ? Map->FindRoad(P.RoadId) : nullptr;
		if (!Road)
		{
			return false;
		}
		const double S = FMath::Clamp(P.S, 0.0, Road->Length);
		double T = P.T;
		double Base;
		if (P.Type == EOSCPositionType::Lane)
		{
			const int32 Lane = Map->ClampLaneId(*Road, S, P.LaneId);
			T = Map->GetLaneCenterT(*Road, S, Lane) + P.Offset;
			const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, T);
			Base = Pose.Heading + (Lane > 0 ? Pi : 0.0);
			X = Pose.X; Y = Pose.Y; Z = Pose.Z;
		}
		else
		{
			const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, T);
			Base = Pose.Heading;
			X = Pose.X; Y = Pose.Y; Z = Pose.Z;
		}
		H = P.bHasOrientation ? (P.bOrientationRelative ? Base + P.H : P.H) : Base;
		return true;
	}
	default:
		return false;
	}
}
