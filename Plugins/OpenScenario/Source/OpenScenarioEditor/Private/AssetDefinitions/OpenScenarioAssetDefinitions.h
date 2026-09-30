#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "OpenScenarioAssetDefinitions.generated.h"

/** Content browser definition for OpenSCENARIO scenario assets. */
UCLASS()
class UAssetDefinition_OpenScenario : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
};

// OpenDRIVE road network assets are defined by the OpenDrive plugin (UAssetDefinition_OpenDrive), which
// this plugin now depends on instead of carrying its own copy.
