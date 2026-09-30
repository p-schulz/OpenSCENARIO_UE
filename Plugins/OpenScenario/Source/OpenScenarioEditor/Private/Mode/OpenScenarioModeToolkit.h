#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "OpenScenarioVersion.h"

class SWidget;

/** Toolkit of the OpenSCENARIO editor mode; hosts the mode panel as inline content. */
class FOpenScenarioModeToolkit : public FModeToolkit
{
public:
#if OSC_UE_AT_LEAST(5, 1)
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
#else
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
#endif

	virtual FName GetToolkitFName() const override { return FName("OpenScenarioModeToolkit"); }
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return Panel; }

private:
	TSharedPtr<SWidget> Panel;
};
