#pragma once

#include "CoreMinimal.h"

class FOpenDriveMap;

/** Serialises an FOpenDriveMap back into ASAM OpenDRIVE (.xodr) XML text. */
class OPENDRIVE_API FOpenDriveWriter
{
public:
	static FString Write(const FOpenDriveMap& Map);
};
