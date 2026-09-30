#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FOpenScenarioEditorContext;
class UWorld;

/**
 * Draws the active scenario's OpenDRIVE network and its entities' start poses into the editor world as
 * persistent debug lines and strings. The geometry is rebuilt and redrawn only when the map, the settings,
 * the scenario model or the origin change.
 */
class FOpenScenarioMapVisualizer
{
public:
	/** Call regularly (e.g. every mode tick); redraws when something changed. */
	void Update(FOpenScenarioEditorContext& Context);
	/** Removes the drawing from the editor world. */
	void Clear();
	/** Forces a redraw on the next Update. */
	void Invalidate() { bHasCache = false; }

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
	TWeakObjectPtr<UWorld> DrawnWorld;
};
