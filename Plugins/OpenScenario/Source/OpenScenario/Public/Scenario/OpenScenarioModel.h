#pragma once

#include "CoreMinimal.h"
#include "OpenScenarioModel.generated.h"

/**
 * Representation of the supported subset of an ASAM OpenSCENARIO 1.x file.
 *
 * The types are reflected (USTRUCT/UENUM) so the editor can show and edit them with the standard struct
 * details view. The parser fills them, FOpenScenarioWriter serialises them, and the runner copies the
 * scenario and mutates the (non-reflected) per-element `Runtime` members.
 */

// ------------------------------------------------------------------------------------------------
// Runtime bookkeeping (not reflected)
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

struct FOSCConditionRuntime
{
	bool bPrev = false;
	double TrueSince = -1.0;
	double PendingAt = -1.0;
};

// ------------------------------------------------------------------------------------------------
// Entities
// ------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EOSCEntityKind : uint8
{
	Vehicle,
	Pedestrian,
	MiscObject,
	External
};

USTRUCT(BlueprintType)
struct FOSCEntity
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
	EOSCEntityKind Kind = EOSCEntityKind::Vehicle;

	/** Vehicle / pedestrian / misc-object category, e.g. "car", "truck", "pedestrian". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
	FString Category;

	/** Catalog reference (if the entity was defined through a catalog). Informational. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entity")
	FString CatalogName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Entity")
	FString CatalogEntry;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity")
	FString Model3d;

	/** Bounding box in metres, centre relative to the entity reference point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double Length = 4.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double Width = 1.8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double Height = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double CenterX = 1.4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double CenterY = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bounding Box")
	double CenterZ = 0.75;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	double MaxSpeed = 70.0;

	/** m/s^2. The simulation additionally caps this with the dynamics settings of the OpenScenario Actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	double MaxAcceleration = 3.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	double MaxDeceleration = 8.0;
};

// ------------------------------------------------------------------------------------------------
// Positions
// ------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
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

USTRUCT(BlueprintType)
struct FOSCPosition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position")
	EOSCPositionType Type = EOSCPositionType::None;

	// World (metres)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::World", EditConditionHides))
	double X = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::World", EditConditionHides))
	double Y = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::World", EditConditionHides))
	double Z = 0.0;

	// Orientation (radians)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "Type != EOSCPositionType::None", EditConditionHides))
	bool bHasOrientation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "bHasOrientation && Type != EOSCPositionType::None && Type != EOSCPositionType::World", EditConditionHides))
	bool bOrientationRelative = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "bHasOrientation && Type != EOSCPositionType::None", EditConditionHides))
	double H = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "bHasOrientation && Type != EOSCPositionType::None", EditConditionHides))
	double P = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "bHasOrientation && Type != EOSCPositionType::None", EditConditionHides))
	double R = 0.0;

	// Road / lane
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::Road || Type == EOSCPositionType::Lane", EditConditionHides))
	FString RoadId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::Road || Type == EOSCPositionType::Lane", EditConditionHides))
	double S = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::Road", EditConditionHides))
	double T = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::Lane", EditConditionHides))
	int32 LaneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::Lane || Type == EOSCPositionType::RelativeLane", EditConditionHides))
	double Offset = 0.0;

	// Relative
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeWorld || Type == EOSCPositionType::RelativeObject || Type == EOSCPositionType::RelativeLane", EditConditionHides))
	FString EntityRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeWorld || Type == EOSCPositionType::RelativeObject", EditConditionHides))
	double DX = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeWorld || Type == EOSCPositionType::RelativeObject", EditConditionHides))
	double DY = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeWorld || Type == EOSCPositionType::RelativeObject", EditConditionHides))
	double DZ = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeLane", EditConditionHides))
	int32 DLane = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Position", meta = (EditCondition = "Type == EOSCPositionType::RelativeLane", EditConditionHides))
	double DS = 0.0;

	bool IsSet() const { return Type != EOSCPositionType::None; }
};

// ------------------------------------------------------------------------------------------------
// Actions
// ------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EOSCShape : uint8
{
	Step,
	Linear,
	Cubic,
	Sinusoidal
};

UENUM(BlueprintType)
enum class EOSCDimension : uint8
{
	Time,
	Distance,
	Rate
};

USTRUCT(BlueprintType)
struct FOSCDynamics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	EOSCShape Shape = EOSCShape::Step;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	EOSCDimension Dimension = EOSCDimension::Time;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	double Value = 0.0;
};

UENUM(BlueprintType)
enum class EOSCActionType : uint8
{
	Unsupported,
	Teleport,
	Speed,
	LaneChange,
	AssignRoute,
	FollowTrajectory,
	/** Global TrafficAction: swarm, source, sink or stop. */
	Traffic,
	/** Global TrafficSignalStateAction: sets one OpenDRIVE signal's state. */
	TrafficSignalState,
	/** Global TrafficSignalControllerAction: sets the phase of a signal controller defined in the scenario. */
	TrafficSignalController
};

UENUM(BlueprintType)
enum class EOSCTrafficKind : uint8
{
	/** Keeps a number of actors around a central entity. */
	Swarm,
	/** Spawns actors at a position at a given rate. */
	Source,
	/** Removes traffic actors near a position at a given rate. */
	Sink,
	/** Stops the generator with the given traffic name. */
	Stop
};

/** One entry of a TrafficDefinition's VehicleCategoryDistribution. */
USTRUCT(BlueprintType)
struct FOSCTrafficCategory
{
	GENERATED_BODY()

	/**
	 * car, van, truck, trailer, semitrailer, bus, motorbike, bicycle, train, tram, or the extension "pedestrian"
	 * (spawned on sidewalk/walking lanes).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FString Category = TEXT("car");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	double Weight = 1.0;
};

USTRUCT(BlueprintType)
struct FOSCTraffic
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	EOSCTrafficKind Kind = EOSCTrafficKind::Swarm;

	/** Identifies the generator for a Stop action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FString TrafficName = TEXT("Traffic");

	/** Entity the swarm is centred on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	FString CentralObject;

	/** Half-length of the swarm ellipse along the central object's heading (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	double SemiMajorAxis = 150.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	double SemiMinorAxis = 100.0;

	/** Nothing is spawned closer than this to the central object (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	double InnerRadius = 20.0;

	/** Shifts the ellipse centre along the central object's heading (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	double Offset = 0.0;

	/** Number of actors (vehicles and pedestrians together) the swarm maintains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0", EditCondition = "Kind == EOSCTrafficKind::Swarm", EditConditionHides))
	int32 NumberOfVehicles = 10;

	/** Actors per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", EditCondition = "Kind == EOSCTrafficKind::Source || Kind == EOSCTrafficKind::Sink", EditConditionHides))
	double Rate = 0.5;

	/** Source/sink area around the position (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", EditCondition = "Kind == EOSCTrafficKind::Source || Kind == EOSCTrafficKind::Sink", EditConditionHides))
	double Radius = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind == EOSCTrafficKind::Source || Kind == EOSCTrafficKind::Sink", EditConditionHides))
	FOSCPosition Position;

	/** Initial and desired speed (m/s). 0 = use the lane's speed limit. Pedestrians walk at their own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", EditCondition = "Kind != EOSCTrafficKind::Stop && Kind != EOSCTrafficKind::Sink", EditConditionHides))
	double Velocity = 0.0;

	/** Mix of spawned actors. Empty = cars only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Kind != EOSCTrafficKind::Stop", EditConditionHides))
	TArray<FOSCTrafficCategory> Distribution;
};

USTRUCT(BlueprintType)
struct FOSCTrajectoryVertex
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex")
	bool bHasTime = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex", meta = (EditCondition = "bHasTime"))
	double Time = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex")
	FOSCPosition Position;
};

USTRUCT(BlueprintType)
struct FOSCAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	EOSCActionType Type = EOSCActionType::Unsupported;

	/** For unsupported actions: the tag that was not understood (for diagnostics). Such actions are not written back. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Action", meta = (EditCondition = "Type == EOSCActionType::Unsupported", EditConditionHides))
	FString UnsupportedTag;

	// Teleport
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teleport", meta = (EditCondition = "Type == EOSCActionType::Teleport", EditConditionHides))
	FOSCPosition Position;

	// Speed / lane change dynamics
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (EditCondition = "Type == EOSCActionType::Speed || Type == EOSCActionType::LaneChange", EditConditionHides))
	FOSCDynamics Dynamics;

	// Speed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (EditCondition = "Type == EOSCActionType::Speed", EditConditionHides))
	double SpeedValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (EditCondition = "Type == EOSCActionType::Speed", EditConditionHides))
	bool bSpeedRelative = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (EditCondition = "Type == EOSCActionType::Speed && bSpeedRelative", EditConditionHides))
	bool bSpeedFactor = false;

	/** Reference entity of a relative speed target or lane change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "(Type == EOSCActionType::Speed && bSpeedRelative) || (Type == EOSCActionType::LaneChange && bLaneRelative)", EditConditionHides))
	FString RefEntity;

	// Lane change
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane Change", meta = (EditCondition = "Type == EOSCActionType::LaneChange", EditConditionHides))
	bool bLaneRelative = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane Change", meta = (EditCondition = "Type == EOSCActionType::LaneChange", EditConditionHides))
	int32 LaneValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lane Change", meta = (EditCondition = "Type == EOSCActionType::LaneChange", EditConditionHides))
	double LaneOffset = 0.0;

	// Route
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Route", meta = (EditCondition = "Type == EOSCActionType::AssignRoute", EditConditionHides))
	TArray<FOSCPosition> Waypoints;

	// Trajectory
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "Type == EOSCActionType::FollowTrajectory", EditConditionHides))
	TArray<FOSCTrajectoryVertex> Vertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "Type == EOSCActionType::FollowTrajectory", EditConditionHides))
	bool bTimeReference = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "Type == EOSCActionType::FollowTrajectory && bTimeReference", EditConditionHides))
	bool bTimeAbsolute = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "Type == EOSCActionType::FollowTrajectory && bTimeReference", EditConditionHides))
	double TimeOffset = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (EditCondition = "Type == EOSCActionType::FollowTrajectory && bTimeReference", EditConditionHides))
	double TimeScale = 1.0;

	// Traffic signals (global actions)
	/** OpenDRIVE signal id (TrafficSignalState). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCActionType::TrafficSignalState", EditConditionHides))
	FString SignalId;

	/** red, yellow, green or off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCActionType::TrafficSignalState", EditConditionHides))
	FString SignalState = TEXT("red");

	/** Name of a signal controller defined in the scenario's RoadNetwork (TrafficSignalController). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCActionType::TrafficSignalController", EditConditionHides))
	FString ControllerRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCActionType::TrafficSignalController", EditConditionHides))
	FString ControllerPhase;

	// Traffic (global action)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (EditCondition = "Type == EOSCActionType::Traffic", EditConditionHides))
	FOSCTraffic Traffic;

	FOSCRuntime Runtime;
};

// ------------------------------------------------------------------------------------------------
// Triggers
// ------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EOSCRule : uint8
{
	EqualTo,
	GreaterThan,
	LessThan,
	GreaterOrEqual,
	LessOrEqual,
	NotEqualTo
};

UENUM(BlueprintType)
enum class EOSCEdge : uint8
{
	None,
	Rising,
	Falling,
	RisingOrFalling
};

UENUM(BlueprintType)
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
	StandStill,
	/** Compares a signal's current state. */
	TrafficSignal
};

USTRUCT(BlueprintType)
struct FOSCCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EOSCConditionType Type = EOSCConditionType::Unsupported;

	/** Tag that was not understood. Such conditions are not written back. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Condition", meta = (EditCondition = "Type == EOSCConditionType::Unsupported", EditConditionHides))
	FString UnsupportedTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EOSCEdge Edge = EOSCEdge::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	double Delay = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compare", meta = (EditCondition = "Type != EOSCConditionType::Unsupported && Type != EOSCConditionType::TrafficSignal && Type != EOSCConditionType::ReachPosition && Type != EOSCConditionType::Collision && Type != EOSCConditionType::StandStill", EditConditionHides))
	EOSCRule Rule = EOSCRule::GreaterThan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Compare", meta = (EditCondition = "Type != EOSCConditionType::Unsupported && Type != EOSCConditionType::TrafficSignal && Type != EOSCConditionType::ReachPosition && Type != EOSCConditionType::Collision && Type != EOSCConditionType::StoryboardElementState", EditConditionHides))
	double Value = 0.0;

	// By entity
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triggering Entities", meta = (EditCondition = "Type != EOSCConditionType::Unsupported && Type != EOSCConditionType::SimulationTime && Type != EOSCConditionType::StoryboardElementState && Type != EOSCConditionType::Parameter && Type != EOSCConditionType::TrafficSignal", EditConditionHides))
	TArray<FString> TriggeringEntities;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Triggering Entities", meta = (EditCondition = "Type != EOSCConditionType::Unsupported && Type != EOSCConditionType::SimulationTime && Type != EOSCConditionType::StoryboardElementState && Type != EOSCConditionType::Parameter && Type != EOSCConditionType::TrafficSignal", EditConditionHides))
	bool bAllTriggeringEntities = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "Type == EOSCConditionType::RelativeSpeed || Type == EOSCConditionType::RelativeDistance || Type == EOSCConditionType::TimeHeadway || Type == EOSCConditionType::Collision", EditConditionHides))
	FString EntityRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "Type == EOSCConditionType::ReachPosition || Type == EOSCConditionType::Distance", EditConditionHides))
	FOSCPosition Position;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "Type == EOSCConditionType::ReachPosition", EditConditionHides))
	double Tolerance = 1.0;

	/** euclidianDistance, cartesianDistance, longitudinal or lateral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "Type == EOSCConditionType::RelativeDistance", EditConditionHides))
	FString RelativeDistanceType = TEXT("euclidianDistance");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference", meta = (EditCondition = "Type == EOSCConditionType::Distance || Type == EOSCConditionType::RelativeDistance || Type == EOSCConditionType::TimeHeadway", EditConditionHides))
	bool bFreespace = false;

	// By value
	/** story, act, maneuverGroup, maneuver, event or action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard Element", meta = (EditCondition = "Type == EOSCConditionType::StoryboardElementState", EditConditionHides))
	FString ElementType = TEXT("act");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard Element", meta = (EditCondition = "Type == EOSCConditionType::StoryboardElementState", EditConditionHides))
	FString ElementRef;

	/** standbyState, runningState, completeState, startTransition, endTransition, stopTransition or skipTransition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard Element", meta = (EditCondition = "Type == EOSCConditionType::StoryboardElementState", EditConditionHides))
	FString ElementState = TEXT("endTransition");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCConditionType::TrafficSignal", EditConditionHides))
	FString SignalId;

	/** red, yellow, green or off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (EditCondition = "Type == EOSCConditionType::TrafficSignal", EditConditionHides))
	FString SignalState = TEXT("green");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter", meta = (EditCondition = "Type == EOSCConditionType::Parameter", EditConditionHides))
	FString ParameterRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter", meta = (EditCondition = "Type == EOSCConditionType::Parameter", EditConditionHides))
	FString StringValue;

	FOSCConditionRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCConditionGroup
{
	GENERATED_BODY()

	/** All conditions must hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	TArray<FOSCCondition> Conditions;
};

USTRUCT(BlueprintType)
struct FOSCTrigger
{
	GENERATED_BODY()

	/** False if the element had no trigger at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	bool bPresent = false;

	/** Any group may hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	TArray<FOSCConditionGroup> Groups;
};

// ------------------------------------------------------------------------------------------------
// Storyboard
// ------------------------------------------------------------------------------------------------

UENUM(BlueprintType)
enum class EOSCPriority : uint8
{
	Parallel,
	Override,
	Skip
};

USTRUCT(BlueprintType)
struct FOSCEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	EOSCPriority Priority = EOSCPriority::Parallel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (ClampMin = "1"))
	int32 MaxExecutions = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	TArray<FOSCAction> Actions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	FOSCTrigger StartTrigger;

	FOSCRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCManeuver
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver")
	TArray<FOSCEvent> Events;

	FOSCRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCManeuverGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver Group")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver Group", meta = (ClampMin = "1"))
	int32 MaxExecutions = 1;

	/** Names of the entities performing the maneuvers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver Group")
	TArray<FString> Actors;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maneuver Group")
	bool bSelectTriggeringEntities = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maneuver Group")
	TArray<FOSCManeuver> Maneuvers;

	FOSCRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCAct
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Act")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Act")
	TArray<FOSCManeuverGroup> ManeuverGroups;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Act")
	FOSCTrigger StartTrigger;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Act")
	FOSCTrigger StopTrigger;

	FOSCRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCStory
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Story")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Story")
	TArray<FOSCAct> Acts;

	FOSCRuntime Runtime;
};

USTRUCT(BlueprintType)
struct FOSCInitActions
{
	GENERATED_BODY()

	/** Entity the actions apply to. Empty = global actions (e.g. traffic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Init")
	FString EntityRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Init")
	TArray<FOSCAction> Actions;
};

USTRUCT(BlueprintType)
struct FOSCParameterDeclaration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FString Name;

	/** string, double, integer, boolean, ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FString Type = TEXT("double");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FString Value;
};

USTRUCT(BlueprintType)
struct FOSCSignalStateEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	FString SignalId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	FString State = TEXT("red");
};

USTRUCT(BlueprintType)
struct FOSCSignalPhase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	FString Name;

	/** Seconds; 0 holds the phase until a controller action changes it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal", meta = (ClampMin = "0.0"))
	double Duration = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	TArray<FOSCSignalStateEntry> States;
};

/** RoadNetwork/TrafficSignals/TrafficSignalController: drives the listed OpenDRIVE signals through its phases. */
USTRUCT(BlueprintType)
struct FOSCSignalController
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	double Delay = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signal")
	TArray<FOSCSignalPhase> Phases;
};

USTRUCT(BlueprintType)
struct FOSCScenario
{
	GENERATED_BODY()

	// File header
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Header")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Header")
	FString Author;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "File Header")
	FString Date;

	/** Path of the referenced OpenDRIVE file as written in the RoadNetwork/LogicFile element. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network")
	FString RoadNetworkFile;

	/** Declared top-level parameters with their file values (parameter overrides are not applied here). */
	/** Signal controllers defined in RoadNetwork/TrafficSignals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road Network")
	TArray<FOSCSignalController> SignalControllers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	TArray<FOSCParameterDeclaration> ParameterDeclarations;

	/** Effective parameter values (declaration + override) used while parsing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Parameters")
	TMap<FString, FString> Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard")
	TArray<FOSCEntity> Entities;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard")
	TArray<FOSCInitActions> InitActions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard")
	TArray<FOSCStory> Stories;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storyboard")
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
