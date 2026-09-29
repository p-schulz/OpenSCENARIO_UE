#include "OpenScenarioEditorModule.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Authoring/OpenScenarioMapVisualizer.h"
#include "Authoring/SOpenScenarioStoryboardTab.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "OpenScenarioEditorModule"

const FName FOpenScenarioEditorModule::StoryboardTabId(TEXT("OpenScenarioStoryboard"));

FOpenScenarioEditorModule& FOpenScenarioEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FOpenScenarioEditorModule>("OpenScenarioEditor");
}

void FOpenScenarioEditorModule::StartupModule()
{
	Context = MakeShared<FOpenScenarioEditorContext>();
	Visualizer = MakeShared<FOpenScenarioMapVisualizer>();

	// Redraw the level viewports whenever the visualisation inputs change.
	Context->OnVisualizationChanged.AddLambda([]()
	{
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(false);
		}
	});

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(StoryboardTabId, FOnSpawnTab::CreateRaw(this, &FOpenScenarioEditorModule::SpawnStoryboardTab))
		.SetDisplayName(LOCTEXT("StoryboardTabTitle", "OpenSCENARIO Storyboard"))
		.SetTooltipText(LOCTEXT("StoryboardTabTip", "Overview and editor of the active OpenSCENARIO storyboard"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
}

void FOpenScenarioEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StoryboardTabId);
	}
	Visualizer.Reset();
	Context.Reset();
}

TSharedRef<SDockTab> FOpenScenarioEditorModule::SpawnStoryboardTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("StoryboardTabLabel", "OpenSCENARIO Storyboard"))
		[
			SNew(SOpenScenarioStoryboardTab, *Context)
		];
}

void FOpenScenarioEditorModule::OpenStoryboardTab() const
{
	FGlobalTabmanager::Get()->TryInvokeTab(StoryboardTabId);
}

IMPLEMENT_MODULE(FOpenScenarioEditorModule, OpenScenarioEditor)

#undef LOCTEXT_NAMESPACE
