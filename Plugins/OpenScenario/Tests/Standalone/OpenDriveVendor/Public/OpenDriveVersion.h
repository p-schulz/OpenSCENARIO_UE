#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"

/**
 * Engine version switches for APIs that changed between releases.
 * Usage: #if ODR_UE_AT_LEAST(5, 4) ... new API ... #else ... old API ... #endif
 */
#define ODR_UE_AT_LEAST(Major, Minor) (!UE_VERSION_OLDER_THAN(Major, Minor, 0))
