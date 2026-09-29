#include "Mode/OpenScenarioEdMode.h"
#include "Mode/OpenScenarioModeToolkit.h"
#include "Authoring/OpenScenarioMapVisualizer.h"
#include "OpenScenarioEditorModule.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "OpenScenarioEdMode"

const FEditorModeID UOpenScenarioEdMode::EM_OpenScenarioId = TEXT("EM_OpenScenario");

UOpenScenarioEdMode::UOpenScenarioEdMode()
{
	Info = FEditorModeInfo(
		UOpenScenarioEdMode::EM_OpenScenarioId,
		LOCTEXT("ModeName", "OpenSCENARIO"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.BspMode"),
		true,
		5000);
}

void UOpenScenarioEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FOpenScenarioModeToolkit>();
}

void UOpenScenarioEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);
	FOpenScenarioEditorModule& Module = FOpenScenarioEditorModule::Get();
	Module.GetVisualizer().Render(Module.GetContext(), View, PDI);
}

void UOpenScenarioEdMode::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	Super::DrawHUD(ViewportClient, Viewport, View, Canvas);
	FOpenScenarioEditorModule& Module = FOpenScenarioEditorModule::Get();
	Module.GetVisualizer().DrawLabels(Module.GetContext(), ViewportClient, View, Canvas);
}

#undef LOCTEXT_NAMESPACE
