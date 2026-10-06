#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

OPENDRIVE_API DECLARE_LOG_CATEGORY_EXTERN(LogOpenDrive, Log, All);

class FOpenDriveModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
