#include "Mode/OpenScenarioEdMode.h"
#include "Mode/OpenScenarioModeToolkit.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Authoring/OpenScenarioMapVisualizer.h"
#include "OpenScenarioEditorModule.h"
#include "Editor.h"
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

void UOpenScenarioEdMode::Enter()
{
	Super::Enter();
	FOpenScenarioEditorModule::Get().GetVisualizer().Invalidate();
}

void UOpenScenarioEdMode::Exit()
{
	FOpenScenarioEditorModule::Get().GetVisualizer().Clear();
	Super::Exit();
}

void UOpenScenarioEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FOpenScenarioModeToolkit>();
}

void UOpenScenarioEdMode::ModeTick(float DeltaTime)
{
	Super::ModeTick(DeltaTime);
	FOpenScenarioEditorModule& Module = FOpenScenarioEditorModule::Get();
	Module.GetVisualizer().Update(Module.GetContext());
}

#undef LOCTEXT_NAMESPACE
