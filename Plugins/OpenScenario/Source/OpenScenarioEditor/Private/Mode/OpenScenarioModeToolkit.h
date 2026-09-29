#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class SWidget;

/** Toolkit of the OpenSCENARIO editor mode; hosts the mode panel as inline content. */
class FOpenScenarioModeToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;

	virtual FName GetToolkitFName() const override { return FName("OpenScenarioModeToolkit"); }
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return Panel; }

private:
	TSharedPtr<SWidget> Panel;
};
