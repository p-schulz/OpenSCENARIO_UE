#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FOpenScenarioEditorContext;
class FOpenScenarioMapVisualizer;
class SDockTab;
class FSpawnTabArgs;

/** Editor module: asset factories/definitions, storyboard tab and the OpenSCENARIO editor mode. */
class FOpenScenarioEditorModule : public IModuleInterface
{
public:
	static const FName StoryboardTabId;

	static FOpenScenarioEditorModule& Get();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** State shared by the editor mode panel, the storyboard tab and the viewport visualisation. */
	FOpenScenarioEditorContext& GetContext() const { return *Context; }
	FOpenScenarioMapVisualizer& GetVisualizer() const { return *Visualizer; }

	void OpenStoryboardTab() const;

private:
	TSharedRef<SDockTab> SpawnStoryboardTab(const FSpawnTabArgs& Args);

	TSharedPtr<FOpenScenarioEditorContext> Context;
	TSharedPtr<FOpenScenarioMapVisualizer> Visualizer;
};
