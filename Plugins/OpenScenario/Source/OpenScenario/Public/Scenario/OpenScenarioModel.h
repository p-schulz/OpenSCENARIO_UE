#pragma once

#include "CoreMinimal.h"

/**
 * Plain C++ representation of the supported subset of an ASAM OpenSCENARIO 1.x file.
 * The parser fills it, the runner copies it and mutates the per-element `Runtime` members.
 */

// ------------------------------------------------------------------------------------------------
// Runtime bookkeeping
// ------------------------------------------------------------------------------------------------

enum class EOSCElementState : uint8
{
	Standby,
	Running,
	Complete
};

struct FOSCRuntime
{
	EOSCElementState State = EOSCElementState::Standby;
	/** Simulation tick indices at which the respective transition last happened. */
	int64 StartTick = -1000;
	int64 EndTick = -1000;
	int64 StopTick = -1000;
	int64 SkipTick = -1000;
	int32 Executions = 0;
	/** Indices into the runner's action instance list (events only). */
	TArray<int32> Instances;

	void Reset()
	{
		*this = FOSCRuntime();
	}
};

// ------------------------------------------------------------------------------------------------
// Entities
// ------------------------------------------------------------------------------------------------

enum class EOSCEntityKind : uint8
{
	Vehicle,
	Pedestrian,
	MiscObject,
	External
};

struct FOSCEntity
{
	FString Name;
	EOSCEntityKind Kind = EOSCEntityKind::Vehicle;
	/** Vehicle / pedestrian / misc-object category, e.g. "car", "truck", "pedestrian". */
	FString Category;
	/** Catalog reference (if the entity was defined through a catalog). */
	FString CatalogName;
	FString CatalogEntry;
	FString Model3d;

	/** Bounding box in metres, centre relative to the entity reference point. */
	double Length = 4.5;
	double Width = 1.8;
	double Height = 1.5;
	double CenterX = 1.4;
	double CenterY = 0.0;
	double CenterZ = 0.75;
	double MaxSpeed = 70.0;
};

// ------------------------------------------------------------------------------------------------
// Positions
// ------------------------------------------------------------------------------------------------

enum class EOSCPositionType : uint8
{
	None,
	World,
	Road,
	Lane,
	RelativeWorld,
	RelativeObject,
	RelativeLane
};

struct FOSCPosition
{
	EOSCPositionType Type = EOSCPositionType::None;

	// World / orientation (metres, radians)
	double X = 0.0, Y = 0.0, Z = 0.0;
	double H = 0.0, P = 0.0, R = 0.0;
	bool bHasOrientation = false;
	bool bOrientationRelative = false;

	// Road / lane
	FString RoadId;
	double S = 0.0;
	double T = 0.0;
	int32 LaneId = 0;
	double Offset = 0.0;

	// Relative
	FString EntityRef;
	double DX = 0.0, DY = 0.0, DZ = 0.0;
	int32 DLane = 0;
	double DS = 0.0;

	bool IsSet() const { return Type != EOSCPositionType::None; }
};

// ------------------------------------------------------------------------------------------------
// Actions
// ------------------------------------------------------------------------------------------------

enum class EOSCShape : uint8
{
	Step,
	Linear,
	Cubic,
	Sinusoidal
};

enum class EOSCDimension : uint8
{
	Time,
	Distance,
	Rate
};

struct FOSCDynamics
{
	EOSCShape Shape = EOSCShape::Step;
	EOSCDimension Dimension = EOSCDimension::Time;
	double Value = 0.0;
};

enum class EOSCActionType : uint8
{
	Unsupported,
	Teleport,
	Speed,
	LaneChange,
	AssignRoute,
	FollowTrajectory
};

struct FOSCTrajectoryVertex
{
	bool bHasTime = false;
	double Time = 0.0;
	FOSCPosition Position;
};

struct FOSCAction
{
	FString Name;
	EOSCActionType Type = EOSCActionType::Unsupported;
	/** For unsupported actions: the tag that was not understood (for diagnostics). */
	FString UnsupportedTag;

	// Teleport
	FOSCPosition Position;

	// Speed / lane change dynamics
	FOSCDynamics Dynamics;

	// Speed
	double SpeedValue = 0.0;
	bool bSpeedRelative = false;
	bool bSpeedFactor = false;
	FString RefEntity;

	// Lane change
	bool bLaneRelative = false;
	int32 LaneValue = 0;
	double LaneOffset = 0.0;

	// Route
	TArray<FOSCPosition> Waypoints;

	// Trajectory
	TArray<FOSCTrajectoryVertex> Vertices;
	bool bTimeReference = false;
	bool bTimeAbsolute = true;
	double TimeOffset = 0.0;
	double TimeScale = 1.0;

	FOSCRuntime Runtime;
};

// ------------------------------------------------------------------------------------------------
// Triggers
// ------------------------------------------------------------------------------------------------

enum class EOSCRule : uint8
{
	EqualTo,
	GreaterThan,
	LessThan,
	GreaterOrEqual,
	LessOrEqual,
	NotEqualTo
};

enum class EOSCEdge : uint8
{
	None,
	Rising,
	Falling,
	RisingOrFalling
};

enum class EOSCConditionType : uint8
{
	Unsupported,
	// By value
	SimulationTime,
	StoryboardElementState,
	Parameter,
	// By entity
	Speed,
	RelativeSpeed,
	Acceleration,
	TraveledDistance,
	ReachPosition,
	Distance,
	RelativeDistance,
	TimeHeadway,
	Collision,
	StandStill
};

struct FOSCConditionRuntime
{
	bool bPrev = false;
	double TrueSince = -1.0;
	double PendingAt = -1.0;
};

struct FOSCCondition
{
	FString Name;
	EOSCConditionType Type = EOSCConditionType::Unsupported;
	FString UnsupportedTag;
	EOSCEdge Edge = EOSCEdge::None;
	double Delay = 0.0;
	EOSCRule Rule = EOSCRule::GreaterThan;
	double Value = 0.0;

	// By entity
	TArray<FString> TriggeringEntities;
	bool bAllTriggeringEntities = false;
	FString EntityRef;
	FOSCPosition Position;
	double Tolerance = 1.0;
	FString RelativeDistanceType;
	bool bFreespace = false;

	// By value
	FString ElementType;
	FString ElementRef;
	FString ElementState;
	FString ParameterRef;
	FString StringValue;

	FOSCConditionRuntime Runtime;
};

struct FOSCConditionGroup
{
	/** All conditions must hold. */
	TArray<FOSCCondition> Conditions;
};

struct FOSCTrigger
{
	bool bPresent = false;
	/** Any group may hold. */
	TArray<FOSCConditionGroup> Groups;
};

// ------------------------------------------------------------------------------------------------
// Storyboard
// ------------------------------------------------------------------------------------------------

enum class EOSCPriority : uint8
{
	Parallel,
	Override,
	Skip
};

struct FOSCEvent
{
	FString Name;
	EOSCPriority Priority = EOSCPriority::Parallel;
	int32 MaxExecutions = 1;
	TArray<FOSCAction> Actions;
	FOSCTrigger StartTrigger;
	FOSCRuntime Runtime;
};

struct FOSCManeuver
{
	FString Name;
	TArray<FOSCEvent> Events;
	FOSCRuntime Runtime;
};

struct FOSCManeuverGroup
{
	FString Name;
	int32 MaxExecutions = 1;
	TArray<FString> Actors;
	bool bSelectTriggeringEntities = false;
	TArray<FOSCManeuver> Maneuvers;
	FOSCRuntime Runtime;
};

struct FOSCAct
{
	FString Name;
	TArray<FOSCManeuverGroup> ManeuverGroups;
	FOSCTrigger StartTrigger;
	FOSCTrigger StopTrigger;
	FOSCRuntime Runtime;
};

struct FOSCStory
{
	FString Name;
	TArray<FOSCAct> Acts;
	FOSCRuntime Runtime;
};

struct FOSCInitActions
{
	FString EntityRef;
	TArray<FOSCAction> Actions;
};

struct FOSCScenario
{
	// File header
	FString Description;
	FString Author;
	FString Date;

	/** Path of the referenced OpenDRIVE file as written in the RoadNetwork/LogicFile element. */
	FString RoadNetworkFile;

	TMap<FString, FString> Parameters;
	TArray<FOSCEntity> Entities;
	TArray<FOSCInitActions> InitActions;
	TArray<FOSCStory> Stories;
	FOSCTrigger StopTrigger;

	const FOSCEntity* FindEntity(const FString& Name) const
	{
		for (const FOSCEntity& E : Entities)
		{
			if (E.Name == Name)
			{
				return &E;
			}
		}
		return nullptr;
	}
};
