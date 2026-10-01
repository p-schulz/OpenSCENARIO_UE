#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/RandomStream.h"
#include "GameFramework/Actor.h"
#include "OpenDrive/OpenDriveMap.h"
#include "Scenario/OpenScenarioModel.h"
#include "Simulation/OpenScenarioDynamics.h"
#include "OpenScenarioRunner.generated.h"

class UOpenScenarioAsset;

/** One point of the path a vehicle is about to drive along (used for braking and traffic checks). */
struct FLookAheadSample
{
	/** Distance from the start of the path (m). */
	double Dist = 0.0;
	/** Fastest allowed speed at this point (speed limit, curve); negative = unlimited. */
	double VLimit = -1.0;
	double X = 0.0;
	double Y = 0.0;
};

struct FLookAheadCache
{
	bool bValid = false;
	double TravelledAtBuild = 0.0;
	double TimeBuilt = 0.0;
	double CoveredDistance = 0.0;
	TArray<FLookAheadSample> Samples;
};

/** Kinematic state of one scenario entity. Units: metres, radians, seconds (OpenSCENARIO frame). */
struct OPENSCENARIO_API FOSCEntityState
{
	FString Name;
	FOSCEntity Def;

	double X = 0.0, Y = 0.0, Z = 0.0, Heading = 0.0;
	double Speed = 0.0;
	/** Speed the scenario asks for. Equals Speed in kinematic mode; in simple dynamics Speed follows it. */
	double DesiredSpeed = 0.0;
	double Accel = 0.0;
	/** Last acceleration command of the simple dynamics (for jerk limiting). */
	double CmdAccel = 0.0;
	double TravelledDistance = 0.0;

	// Road binding (valid if bOnRoad)
	bool bOnRoad = false;
	/** True if the entity travels in the direction of increasing s. */
	bool bDirForward = true;
	FString RoadId;
	double S = 0.0;
	int32 LaneId = 0;
	/** Lateral offset from the lane centre in metres (positive = left). */
	double LaneOffset = 0.0;

	// Lane change in progress
	bool bLaneChanging = false;
	int32 LCSourceLane = 0;
	int32 LCTargetLane = 0;
	double LCFactor = 0.0;

	// Route (road-level), see AssignRouteAction
	TArray<FOpenDriveRouteStep> Route;
	int32 RouteIndex = 0;

	/** False for traffic actors that have been removed; their slot is reused. */
	bool bActive = true;
	/** Id of the traffic generator that spawned this entity, INDEX_NONE for scripted entities. */
	int32 TrafficGenerator = INDEX_NONE;
	/** Reached the end of the road network and cannot continue. */
	bool bAtDeadEnd = false;
	double DeadEndTime = 0.0;
	/** Seconds the entity has been (almost) standing still. */
	double StationaryTime = 0.0;

	// Simple dynamics (diagnostics)
	/** True while limits, curves or traffic keep the vehicle below its desired speed. */
	bool bSpeedConstrained = false;
	/** Bumper-to-bumper distance to the actor in front, negative if none. */
	double LeaderGap = -1.0;
	FString LeaderName;
	FLookAheadCache LookAhead;

	/** True while a trajectory action drives the pose directly. */
	bool bTrajectoryControlled = false;
	/** Set on teleports so that the actor is moved without interpolation/sweeping. */
	bool bTeleported = false;
};

/** State of one running action (one action instance per acting entity). */
struct FOSCActionInstance
{
	FOSCAction* Def = nullptr;
	int32 EntityIndex = INDEX_NONE;
	bool bDone = false;
	double StartTime = 0.0;
	double Elapsed = 0.0;
	double Duration = 0.0;
	double V0 = 0.0;
	double VTarget = 0.0;
	double LaneDistance = 0.0;
	double ProgressDistance = 0.0;
	TArray<FVector> Path;
	TArray<double> PathTimes;
	double PathU = 0.0;
	bool bPathTimed = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOpenScenarioEntitySpawnedNative, const FString& /*EntityName*/, AActor* /*Actor*/);
DECLARE_MULTICAST_DELEGATE(FOpenScenarioFinishedNative);

/**
 * Executes an OpenSCENARIO storyboard. Owns the entity actors, evaluates triggers, runs actions and
 * integrates a kinematic motion model that follows the OpenDRIVE road network (lane keeping,
 * lane changes, road-level routing through junctions).
 */
UCLASS()
class OPENSCENARIO_API UOpenScenarioRunner : public UObject
{
	GENERATED_BODY()

public:
	// --- Configuration (set before Initialize) ---------------------------------------------
	/** World transform of the OpenSCENARIO origin. */
	FTransform Origin = FTransform::Identity;
	bool bSpawnActors = true;
	/** For scenarios without a StopTrigger: end once all stories are complete and no action is running. */
	bool bStopWhenStoryboardComplete = true;

	UPROPERTY()
	TSubclassOf<AActor> DefaultVehicleClass;
	UPROPERTY()
	TSubclassOf<AActor> DefaultPedestrianClass;
	UPROPERTY()
	TSubclassOf<AActor> DefaultMiscObjectClass;
	UPROPERTY()
	TMap<FString, TSubclassOf<AActor>> EntityClassOverrides;

	/** Vehicle/driver model parameters. */
	FOpenScenarioDynamicsSettings Dynamics;
	FOpenScenarioTrafficSettings TrafficSettings;

	// --- Lifecycle ---------------------------------------------------------------------------
	bool Initialize(UWorld* InWorld, UOpenScenarioAsset* InAsset);
	/** Advances the simulation. */
	void Step(double DeltaTime);
	void Stop(bool bDestroyActors);

	bool IsRunning() const { return bRunning; }
	bool IsFinished() const { return bFinished; }
	double GetSimulationTime() const { return SimTime; }

	AActor* GetEntityActor(const FString& EntityName) const;
	const FOSCEntityState* GetEntityState(const FString& EntityName) const;
	const TArray<FOSCEntityState>& GetEntities() const { return Entities; }
	/** Number of currently active actors spawned by traffic generators. */
	int32 GetTrafficCount() const;
	TSharedPtr<const FOpenDriveMap> GetRoadNetwork() const { return Map; }

	FOpenScenarioEntitySpawnedNative OnEntitySpawned;
	FOpenScenarioFinishedNative OnFinished;

private:
	// Setup
	void SpawnEntityActor(FOSCEntityState& E);
	void BuildElementIndex();
	void RunInitActions();

	// Storyboard state machine
	void UpdateStoryboard();
	void UpdateAct(FOSCAct& Act);
	void UpdateManeuverGroup(FOSCManeuverGroup& MG);
	void UpdateEvent(FOSCManeuverGroup& MG, FOSCManeuver& Man, FOSCEvent& Ev);
	void BeginManeuverGroup(FOSCManeuverGroup& MG, bool bFirst);
	void StartEvent(FOSCManeuverGroup& MG, FOSCEvent& Ev);
	void StopEvent(FOSCEvent& Ev);
	void StopAct(FOSCAct& Act);
	void StartElement(FOSCRuntime& R);
	void EndElement(FOSCRuntime& R);
	void StopElement(FOSCRuntime& R);
	bool AllInstancesDone(const FOSCEvent& Ev) const;

	// Triggers
	bool EvaluateTrigger(FOSCTrigger& Trigger, bool bEmptyResult);
	bool EvaluateCondition(FOSCCondition& Cond);
	bool EvaluateRaw(const FOSCCondition& Cond);
	bool EvaluateEntityCondition(const FOSCCondition& Cond, const FOSCEntityState& Entity);
	bool EvaluateElementState(const FOSCCondition& Cond) const;
	static bool Compare(double Lhs, EOSCRule Rule, double Rhs);

	// Actions
	int32 CreateInstance(FOSCAction& Action, const FString& EntityName);
	void InitInstance(FOSCActionInstance& Inst);
	void UpdateInstance(FOSCActionInstance& Inst, double Dt);
	void FinishInstance(FOSCActionInstance& Inst);
	void CancelInstance(FOSCActionInstance& Inst);
	void UpdateSpeedInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt);
	void UpdateLaneChangeInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt);
	void UpdateTrajectoryInstance(FOSCActionInstance& Inst, FOSCEntityState& E, double Dt);
	void AssignRoute(const FOSCAction& Action, FOSCEntityState& E);
	void EndLaneChange(FOSCEntityState& E, bool bToTarget);
	/** Sets the speed a SpeedAction asks for (directly, or as desired speed under simple dynamics). */
	void SetCommandedSpeed(FOSCEntityState& E, double Speed) const;
	bool UsesSimpleDynamics(const FOSCEntityState& E) const;

	// Traffic generators
	struct FTrafficGenerator
	{
		int32 Id = 0;
		FOSCTraffic Def;
		bool bActive = true;
		bool bFirstUpdate = true;
		double Accumulator = 0.0;
	};
	struct FLaneSegment
	{
		FString RoadId;
		int32 LaneId = 0;
		double S0 = 0.0;
		double S1 = 0.0;
		double X = 0.0;
		double Y = 0.0;
	};
	void StartTrafficAction(const FOSCAction& Action);
	void UpdateTraffic(double Dt);
	void UpdateSwarm(FTrafficGenerator& G, double Dt);
	void UpdateSource(FTrafficGenerator& G, double Dt);
	void UpdateSink(FTrafficGenerator& G, double Dt);
	void BuildTrafficIndex();
	void CollectSegments(double X, double Y, double Radius, bool bPedestrian, TArray<int32>& Out) const;
	FString PickCategory(const FOSCTraffic& Def);
	int32 SpawnTrafficEntity(FTrafficGenerator& G, const FString& Category, const FString& RoadId, int32 LaneId, double S, bool bTravelForward);
	void DespawnEntity(int32 Index);
	void ExtendRandomRoute(FOSCEntityState& E, int32 Count);
	bool IsSpotClear(double X, double Y, double Radius) const;
	int32 PickLaneAt(const FOpenDriveRoad& Road, double S, bool bPedestrian);
	bool HasActiveGenerators() const;

	// Simple vehicle dynamics
	struct FRoadCursor
	{
		FString RoadId;
		double S = 0.0;
		int32 LaneId = 0;
		bool bForward = true;
		int32 RouteIndex = 0;
	};
	void UpdateDynamics(FOSCEntityState& E, double Dt);
	void BuildLookAhead(FOSCEntityState& E, double Distance);
	bool AdvanceCursor(FRoadCursor& Cursor, const TArray<FOpenDriveRouteStep>& Route, double Ds) const;
	bool FindLeader(const FOSCEntityState& E, double LookDistance, double& OutGap, double& OutLeaderSpeed, FString& OutName) const;

	// Positions & motion
	FOSCEntityState* FindEntity(const FString& Name);
	const FOSCEntityState* FindEntity(const FString& Name) const;
	/** Resolves a position into Out (pose and, for road positions, road binding). */
	bool ResolvePosition(const FOSCPosition& P, FOSCEntityState& Out, bool bAttachRoad) const;
	bool AttachToRoad(FOSCEntityState& E) const;
	void UpdatePoseFromRoad(FOSCEntityState& E) const;
	void UpdateMotion(FOSCEntityState& E, double Dt);
	void AdvanceOnRoad(FOSCEntityState& E, double Ds);
	static bool BoxesOverlap(const FOSCEntityState& A, const FOSCEntityState& B);
	void SyncActors();
	static double ShapeFactor(EOSCShape Shape, double P);

	void Finish();

	UPROPERTY()
	TObjectPtr<UOpenScenarioAsset> Asset;

	UPROPERTY()
	TObjectPtr<UWorld> World;

	UPROPERTY()
	TMap<FString, TObjectPtr<AActor>> EntityActors;

	FOSCScenario Scenario;
	TSharedPtr<const FOpenDriveMap> Map;
	TArray<FOSCEntityState> Entities;
	TMap<FString, int32> EntityIndex;
	TArray<FOSCActionInstance> Instances;
	TArray<FTrafficGenerator> Generators;
	TArray<FLaneSegment> LaneSegments;
	TMap<int64, TArray<int32>> LaneGrid;
	FRandomStream Random;
	bool bTrafficIndexBuilt = false;
	int32 NextGeneratorId = 0;
	int32 TrafficCounter = 0;
	TMap<FString, FOSCRuntime*> ElementIndex;

	bool bInitPhase = false;
	double SimTime = 0.0;
	double CurrentDt = 0.0;
	int64 Tick = 0;
	bool bRunning = false;
	bool bFinished = false;
};
