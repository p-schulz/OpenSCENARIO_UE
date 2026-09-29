#pragma once

#include "CoreMinimal.h"

class FOpenScenarioEditorContext;
class FPrimitiveDrawInterface;
class FSceneView;
class FCanvas;
class FViewport;
class FEditorViewportClient;

/**
 * Draws the active scenario's OpenDRIVE network and its entities' start poses into the level editor
 * viewports. Geometry is cached and rebuilt when the map, the settings, the scenario model or the
 * origin change.
 */
class FOpenScenarioMapVisualizer
{
public:
	void Render(FOpenScenarioEditorContext& Context, const FSceneView* View, FPrimitiveDrawInterface* PDI);
	void DrawLabels(FOpenScenarioEditorContext& Context, FEditorViewportClient* ViewportClient, const FSceneView* View, FCanvas* Canvas);

private:
	struct FLine
	{
		FVector A;
		FVector B;
		FColor Color;
		float Thickness;
	};

	struct FLabel
	{
		FVector Position;
		FString Text;
		FColor Color;
	};

	void Rebuild(FOpenScenarioEditorContext& Context);
	void AddArrow(const FVector& From, const FVector& To, const FColor& Color, float Thickness);
	void AddBox(const FTransform& Pose, const FVector& CenterCm, const FVector& HalfExtentCm, const FColor& Color, float Thickness);

	TArray<FLine> Lines;
	TArray<FLabel> Labels;

	const void* CachedMap = nullptr;
	uint32 CachedSettingsRevision = MAX_uint32;
	uint32 CachedModelRevision = MAX_uint32;
	FTransform CachedOrigin;
	bool bHasCache = false;
};
