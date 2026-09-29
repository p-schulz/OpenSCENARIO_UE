#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FOpenScenarioEditorContext;

/**
 * Play / Pause / Step / Stop / Restart controls with time readout and time scale.
 * They drive the OpenScenario Actor of the active scenario, in a Play-In-Editor session or directly in the
 * editor viewport (a placed actor simulates without PIE).
 */
class SOpenScenarioTransportBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenScenarioTransportBar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext);

private:
	FText GetStatusText() const;

	FOpenScenarioEditorContext* Context = nullptr;
};
