#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpenScenarioActor.generated.h"

class UOpenScenarioAsset;
class UOpenScenarioRunner;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOpenScenarioFinishedSignature);
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

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO")
	bool StartScenario();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "OpenSCENARIO")
	void StopScenario();

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	bool IsScenarioRunning() const;

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	double GetSimulationTime() const;

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	AActor* GetEntityActor(const FString& EntityName) const;

	/** Speed of a scenario entity in m/s (0 if unknown). */
	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	double GetEntitySpeed(const FString& EntityName) const;

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

private:
	void HandleFinished();
	void HandleEntitySpawned(const FString& Name, AActor* Actor);

	UPROPERTY(Transient)
	TObjectPtr<UOpenScenarioRunner> Runner;

	double Accumulator = 0.0;
};
