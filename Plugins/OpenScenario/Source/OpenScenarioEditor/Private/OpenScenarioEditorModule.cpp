#include "Modules/ModuleManager.h"

// Asset factories and asset definitions register themselves through UCLASS reflection,
// so the module needs no startup work.
IMPLEMENT_MODULE(FDefaultModuleImpl, OpenScenarioEditor)
