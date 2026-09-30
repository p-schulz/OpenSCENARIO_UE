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

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void CreateToolkit() override;
	virtual void ModeTick(float DeltaTime) override;
};
