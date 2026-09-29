#pragma once

#include "CoreMinimal.h"
#include "Scenario/OpenScenarioModel.h"

/**
 * Serialises an FOSCScenario back to OpenSCENARIO 1.1 XML.
 *
 * Only what the model represents is written: unsupported actions/conditions, comments, catalog locations
 * and `$parameter` references (values are written resolved) are not preserved.
 */
class OPENSCENARIO_API FOpenScenarioWriter
{
public:
	static FString Write(const FOSCScenario& Scenario);
};
