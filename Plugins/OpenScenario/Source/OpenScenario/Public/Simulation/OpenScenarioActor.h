#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Simulation/OpenScenarioDynamics.h"
#include "OpenScenarioActor.generated.h"

class UOpenScenarioAsset;
class UOpenScenarioRunner;

UENUM(BlueprintType)
enum class EOpenScenarioPlaybackState : uint8
{
	Stopped,
	Playing,
	Paused,
	/** The storyboard ended (stop trigger or all stories complete); entity actors are still in the level. */
	Finished
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOpenScenarioFinishedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOpenScenarioPlaybackStateSignature, EOpenScenarioPlaybackState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOpenScenarioEntitySpawnedSignature, const FString&, EntityName, AActor*, EntityActor);

/**
 * Place this actor in a level to play an OpenSCENARIO asset. The actor's transform is the origin of
 * the scenario's coordinate system (OpenSCENARIO/OpenDRIVE metres are converted to Unreal units).
 */
UCLASS(BlueprintType, Blueprintable)
class OPENSCENARIO_API AOpenScenarioActor : public AActor
{
	GENERATED_BODY()

public:
	AOpenScenarioActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	TObjectPtr<UOpenScenarioAsset> Scenario;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	bool bAutoStart = true;

	/** Spawn an actor per scenario entity. Disable to read entity state only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	bool bSpawnEntityActors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	bool bDestroyEntityActorsOnEnd = false;

	/** For scenarios without a StopTrigger: end when all acts are complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	bool bStopWhenStoryboardComplete = true;

	/** Start in the paused state (use PlayScenario or StepScenario to continue). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO")
	bool bStartPaused = false;

	/** Vehicle/driver model: acceleration limits, speed limits, curves and traffic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Dynamics")
	FOpenScenarioDynamicsSettings Dynamics;

	/** Spawn limits, seed and speeds of TrafficSwarm/Source/Sink actions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Traffic")
	FOpenScenarioTrafficSettings Traffic;

	/** Fixed simulation step in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Simulation", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float FixedTimeStep = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Simulation", meta = (ClampMin = "0.0"))
	float TimeScale = 1.0f;

	/** Upper bound of simulation steps per rendered frame (keeps slow frames from spiralling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Simulation", meta = (ClampMin = "1"))
	int32 MaxStepsPerFrame = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Actors")
	TSubclassOf<AActor> VehicleActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Actors")
	TSubclassOf<AActor> PedestrianActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Actors")
	TSubclassOf<AActor> MiscObjectActorClass;

	/** Per-entity actor class, keyed by the scenario entity name (e.g. "Ego"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Actors")
	TMap<FString, TSubclassOf<AActor>> EntityActorClasses;

	/** Draw reference lines and lane borders of the road network while playing (or via DrawRoadNetwork). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenSCENARIO|Debug")
	bool bDrawRoadNetwork = false;

	UPROPERTY(BlueprintAssignable, Category = "OpenSCENARIO")
	FOpenScenarioFinishedSignature OnScenarioFinished;

	UPROPERTY(BlueprintAssignable, Category = "OpenSCENARIO")
	FOpenScenarioEntitySpawnedSignature OnEntitySpawned;

	UPROPERTY(BlueprintAssignable, Category = "OpenSCENARIO")
	FOpenScenarioPlaybackStateSignature OnPlaybackStateChanged;

	/** Starts the scenario from the beginning (entity actors are respawned). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	bool StartScenario();

	/** Starts the scenario if it is stopped or finished, resumes it if it is paused. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	void PlayScenario();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	void PauseScenario();

	/** Advances a paused scenario by one fixed time step. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	void StepScenario();

	/** Stops the scenario and removes the entity actors. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	void StopScenario();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Playback")
	bool RestartScenario();

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO|Playback")
	EOpenScenarioPlaybackState GetPlaybackState() const { return PlaybackState; }

	UFUNCTION(BlueprintCallable, Category = "OpenSCENARIO|Playback")
	void SetTimeScale(float NewTimeScale);

	/** True while the scenario is playing or paused. */
	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	bool IsScenarioRunning() const;

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	double GetSimulationTime() const;

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	AActor* GetEntityActor(const FString& EntityName) const;

	/** Speed of a scenario entity in m/s (0 if unknown). */
	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	double GetEntitySpeed(const FString& EntityName) const;

	/** Number of currently active actors spawned by traffic generators. */
	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO|Traffic")
	int32 GetActiveTrafficCount() const;

	/** Diagnostics of the vehicle model for one entity. Returns false for unknown entities. */
	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO|Dynamics")
	bool GetEntityDynamics(const FString& EntityName, double& OutSpeed, double& OutDesiredSpeed, double& OutLeaderGap, FString& OutLeaderName) const;

	/** Draws the road network of the scenario's OpenDRIVE file as persistent debug lines. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Debug")
	void DrawRoadNetwork();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO|Debug")
	void ClearRoadNetworkDrawing();

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	UOpenScenarioRunner* GetRunner() const { return Runner; }

	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	/** Lets a scenario play in the editor viewport without Play-In-Editor. */
	virtual bool ShouldTickIfViewportsOnly() const override;

private:
	void HandleFinished();
	void HandleEntitySpawned(const FString& Name, AActor* Actor);

	void SetPlaybackState(EOpenScenarioPlaybackState NewState);

	UPROPERTY(Transient)
	TObjectPtr<UOpenScenarioRunner> Runner;

	EOpenScenarioPlaybackState PlaybackState = EOpenScenarioPlaybackState::Stopped;

	double Accumulator = 0.0;
};
