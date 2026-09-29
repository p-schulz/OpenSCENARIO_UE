#include "AssetDefinitions/AssetDefinitions.h"
#include "OpenDrive/OpenDriveAsset.h"
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

FText UAssetDefinition_OpenDrive::GetAssetDisplayName() const
{
	return LOCTEXT("OpenDriveName", "OpenDRIVE");
}

FLinearColor UAssetDefinition_OpenDrive::GetAssetColor() const
{
	return FLinearColor(0.20f, 0.70f, 0.35f);
}

TSoftClassPtr<UObject> UAssetDefinition_OpenDrive::GetAssetClass() const
{
	return UOpenDriveAsset::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_OpenDrive::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(LOCTEXT("SimulationCategory", "Simulation")) };
	return Categories;
}

#undef LOCTEXT_NAMESPACE
