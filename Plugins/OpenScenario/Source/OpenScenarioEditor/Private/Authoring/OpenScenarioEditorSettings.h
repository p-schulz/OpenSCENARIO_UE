#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OpenScenarioEditorSettings.generated.h"

class AActor;

/** Options of the OpenSCENARIO editor mode: import location and OpenDRIVE visualisation. */
UCLASS()
class UOpenScenarioEditorSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Content folder used when importing or creating scenarios from the editor mode. */
	UPROPERTY(EditAnywhere, Category = "Import")
	FString ImportDestination = TEXT("/Game/OpenScenario");

	// --- Visualisation ---------------------------------------------------------------------
	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bShowMap = true;

	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawReferenceLines = true;

	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawLaneBorders = true;

	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawLaneCenters = false;

	/** Arrows on driving lanes showing the driving direction. */
	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawDirectionArrows = true;

	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawRoadLabels = true;

	/** Connecting roads inside junctions get their own colour. */
	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bHighlightJunctionRoads = true;

	/** Bounding boxes and names of entities at their Init teleport pose (World/Road/Lane positions). */
	UPROPERTY(EditAnywhere, Category = "Visualization")
	bool bDrawEntityStarts = true;

	/** Sampling distance along roads in metres. Larger values draw faster and coarser. */
	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (ClampMin = "0.25", ClampMax = "50.0"))
	float SampleStep = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float LineThickness = 2.0f;

	/** Lifts the lines above the ground (cm). */
	UPROPERTY(EditAnywhere, Category = "Visualization", meta = (ClampMin = "0.0"))
	float ZOffsetCm = 10.0f;

	/**
	 * Actor whose transform is the scenario origin. If empty, the first OpenScenario Actor in the level
	 * that uses the active scenario is used, otherwise the world origin.
	 */
	UPROPERTY(EditAnywhere, Category = "Visualization")
	TSoftObjectPtr<AActor> OriginActor;

	/** Bumped on every change so cached geometry can be rebuilt. */
	uint32 Revision = 0;

	FSimpleMulticastDelegate OnChanged;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
};
