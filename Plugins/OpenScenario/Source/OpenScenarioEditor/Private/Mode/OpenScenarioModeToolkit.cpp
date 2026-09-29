#include "Mode/OpenScenarioModeToolkit.h"
#include "Mode/SOpenScenarioModePanel.h"
#include "OpenScenarioEditorModule.h"

#define LOCTEXT_NAMESPACE "OpenScenarioModeToolkit"

void FOpenScenarioModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
	Panel = SNew(SOpenScenarioModePanel, FOpenScenarioEditorModule::Get().GetContext());
}

FText FOpenScenarioModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "OpenSCENARIO");
}

#undef LOCTEXT_NAMESPACE
