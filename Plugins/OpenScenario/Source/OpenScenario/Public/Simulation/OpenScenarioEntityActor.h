#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Scenario/OpenScenarioModel.h"
#include "OpenScenarioEntityActor.generated.h"

class UStaticMeshComponent;

/**
 * Default actor spawned for scenario entities: a box matching the entity's bounding box.
 * Subclass it (or supply any other AActor class) to represent entities with real vehicle,
 * pedestrian or prop actors. The actor origin is the OpenSCENARIO entity reference point.
 */
UCLASS(BlueprintType, Blueprintable)
class OPENSCENARIO_API AOpenScenarioEntityActor : public AActor
{
	GENERATED_BODY()

public:
	AOpenScenarioEntityActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenSCENARIO")
	FString EntityName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenSCENARIO")
	FString EntityCategory;

	/** Bounding box dimensions in metres (length, width, height). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenSCENARIO")
	FVector BoundingBoxSize = FVector(4.5, 1.8, 1.5);

	void ConfigureFromEntity(const FOSCEntity& Entity);

	/** Called after the entity definition has been applied; use it to pick meshes or colours. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OpenSCENARIO")
	void OnEntityConfigured(const FString& InEntityName, const FString& InCategory, FVector InBoundingBoxSize);

protected:
	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO")
	TObjectPtr<UStaticMeshComponent> BodyMesh;
};
