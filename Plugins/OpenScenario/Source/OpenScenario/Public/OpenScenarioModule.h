#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

OPENSCENARIO_API DECLARE_LOG_CATEGORY_EXTERN(LogOpenScenario, Log, All);

class FOpenScenarioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
