#pragma once

#include "CoreMinimal.h"
#include "Scenario/OpenScenarioModel.h"

class FOpenDriveMap;

enum class EOSCNodeType : uint8
{
	None,
	Storyboard,
	Entities,
	Entity,
	Init,
	InitGroup,
	InitAction,
	Story,
	Act,
	ManeuverGroup,
	Maneuver,
	Event,
	Action,
	Trigger,
	ConditionGroup,
	Condition
};

/** Which trigger a Trigger/ConditionGroup/Condition node belongs to. */
enum class EOSCTriggerOwner : uint8
{
	None,
	ActStart,
	ActStop,
	EventStart,
	StoryboardStop
};

/**
 * Address of an element inside an FOSCScenario, used by the editor's storyboard tree.
 *
 * Path holds array indices: Entity [e]; InitGroup [g]; InitAction [g,a]; Story [s]; Act [s,a];
 * ManeuverGroup [s,a,m]; Maneuver [s,a,m,n]; Event [s,a,m,n,e]; Action [s,a,m,n,e,x].
 * Trigger nodes carry the path of their owner (Act [s,a], Event [s,a,m,n,e], storyboard []),
 * ConditionGroup appends [group], Condition appends [group, condition].
 */
struct OPENSCENARIO_API FOSCNodeRef
{
	EOSCNodeType Type = EOSCNodeType::None;
	EOSCTriggerOwner Owner = EOSCTriggerOwner::None;
	TArray<int32> Path;

	FOSCNodeRef() {}
	FOSCNodeRef(EOSCNodeType InType, std::initializer_list<int32> InPath = {}, EOSCTriggerOwner InOwner = EOSCTriggerOwner::None)
		: Type(InType), Owner(InOwner)
	{
		for (int32 I : InPath) { Path.Add(I); }
	}

	bool IsValid() const { return Type != EOSCNodeType::None; }
	FString Key() const;
	bool operator==(const FOSCNodeRef& O) const { return Key() == O.Key(); }
};

/** Engine-independent structural editing of an FOSCScenario, addressed by FOSCNodeRef. */
class OPENSCENARIO_API FOSCModelEdit
{
public:
	/** Pointer to the struct behind the node (type given by node type), or null if the address is invalid. */
	static void* GetData(FOSCScenario& Scenario, const FOSCNodeRef& Node);

	static void GetChildren(const FOSCScenario& Scenario, const FOSCNodeRef& Node, TArray<FOSCNodeRef>& OutChildren);
	static FString Describe(const FOSCScenario& Scenario, const FOSCNodeRef& Node);
	static FString TypeName(EOSCNodeType Type);

	static void GetAddableChildren(const FOSCNodeRef& Parent, TArray<EOSCNodeType>& OutTypes);
	static bool AddChild(FOSCScenario& Scenario, const FOSCNodeRef& Parent, EOSCNodeType ChildType, FOSCNodeRef& OutNew);
	static bool CanRemove(const FOSCNodeRef& Node);
	static bool Remove(FOSCScenario& Scenario, const FOSCNodeRef& Node);
	static bool Duplicate(FOSCScenario& Scenario, const FOSCNodeRef& Node, FOSCNodeRef& OutNew);
	/** Moves the node within its parent array by Delta (-1 = up, +1 = down). */
	static bool Move(FOSCScenario& Scenario, const FOSCNodeRef& Node, int32 Delta, FOSCNodeRef& OutNew);
	static bool GetParent(const FOSCNodeRef& Node, FOSCNodeRef& OutParent);

	/** Reports dangling references (unknown entities, unknown storyboard elements, empty names). */
	static void Validate(const FOSCScenario& Scenario, TArray<FString>& OutIssues);

	/**
	 * Resolves World/Road/Lane positions without a running simulation (e.g. for editor previews).
	 * Road/Lane positions need a map. Returns false for other position types.
	 */
	static bool ResolveStaticPose(const FOpenDriveMap* Map, const FOSCPosition& Position, double& X, double& Y, double& Z, double& Heading);

	static FString DescribeAction(const FOSCAction& Action);
	static FString DescribeCondition(const FOSCCondition& Condition);
	static FString DescribePosition(const FOSCPosition& Position);
};
