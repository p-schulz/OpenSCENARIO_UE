#include "Mode/OpenScenarioModeToolkit.h"
#include "Mode/SOpenScenarioModePanel.h"
#include "OpenScenarioEditorModule.h"

#define LOCTEXT_NAMESPACE "OpenScenarioModeToolkit"

#if OSC_UE_AT_LEAST(5, 1)
void FOpenScenarioModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
#else
void FOpenScenarioModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	FModeToolkit::Init(InitToolkitHost);
#endif
	Panel = SNew(SOpenScenarioModePanel, FOpenScenarioEditorModule::Get().GetContext());
}

FText FOpenScenarioModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "OpenSCENARIO");
}

#undef LOCTEXT_NAMESPACE
