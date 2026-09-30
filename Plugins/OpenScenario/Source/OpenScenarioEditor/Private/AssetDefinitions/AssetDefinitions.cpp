#include "AssetDefinitions/AssetDefinitions.h"
#include "Scenario/OpenScenarioAsset.h"

#define LOCTEXT_NAMESPACE "OpenScenarioAssetDefinitions"

FText UAssetDefinition_OpenScenario::GetAssetDisplayName() const
{
	return LOCTEXT("OpenScenarioName", "OpenSCENARIO");
}

FLinearColor UAssetDefinition_OpenScenario::GetAssetColor() const
{
	return FLinearColor(0.10f, 0.55f, 0.85f);
}

TSoftClassPtr<UObject> UAssetDefinition_OpenScenario::GetAssetClass() const
{
	return UOpenScenarioAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_OpenScenario::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(LOCTEXT("SimulationCategory", "Simulation")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
