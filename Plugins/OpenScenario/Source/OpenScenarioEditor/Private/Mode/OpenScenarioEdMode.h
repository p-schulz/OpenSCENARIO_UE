#pragma once

#include "CoreMinimal.h"
#include "Tools/UEdMode.h"
#include "OpenScenarioEdMode.generated.h"

/**
 * "OpenSCENARIO" editor mode: import, read and create scenarios, map entity actor classes, visualise the
 * OpenDRIVE network in the level viewports and open the storyboard editor tab.
 */
UCLASS()
class UOpenScenarioEdMode : public UEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_OpenScenarioId;

	UOpenScenarioEdMode();

	virtual void CreateToolkit() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
};
