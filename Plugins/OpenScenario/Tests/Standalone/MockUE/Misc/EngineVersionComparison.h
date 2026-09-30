#pragma once
#include "CoreMinimal.h"
// Mock engine version: 5.6
#define ENGINE_MAJOR_VERSION 5
#define ENGINE_MINOR_VERSION 6
#define ENGINE_PATCH_VERSION 0
#define UE_VERSION_OLDER_THAN(Major, Minor, Patch) ((ENGINE_MAJOR_VERSION < (Major)) || (ENGINE_MAJOR_VERSION == (Major) && (ENGINE_MINOR_VERSION < (Minor))) || (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION == (Minor) && ENGINE_PATCH_VERSION < (Patch)))
