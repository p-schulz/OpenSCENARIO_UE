#include "Factories/OpenScenarioFactories.h"
#include "OpenDrive/OpenDriveAsset.h"
#include "Scenario/OpenScenarioAsset.h"
#include "OpenScenarioModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
	/**
	 * Creates (or refreshes) a UOpenDriveAsset next to the scenario from the OpenDRIVE file the
	 * scenario references and links it as the scenario's road network.
	 */
	void ImportReferencedRoadNetwork(UOpenScenarioAsset* Scenario)
	{
		const FString& Referenced = Scenario->GetScenario().RoadNetworkFile;
		if (Referenced.IsEmpty())
		{
			return;
		}

		FString OdrXml, OdrPath;
		if (!Scenario->ReadReferencedRoadNetworkFile(OdrXml, OdrPath))
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("Referenced OpenDRIVE file '%s' was not found (searched relative to '%s'). Import it separately and assign it as the scenario's Road Network."),
				*Referenced, *Scenario->GetBaseDirectory());
			return;
		}

		const FString PackagePath = FPackageName::GetLongPackagePath(Scenario->GetOutermost()->GetName());
		const FString RoadName = Scenario->GetName() + TEXT("_RoadNetwork");
		UPackage* Package = CreatePackage(*(PackagePath / RoadName));

		UOpenDriveAsset* Road = FindObject<UOpenDriveAsset>(Package, *RoadName);
		const bool bIsNew = (Road == nullptr);
		if (bIsNew)
		{
			Road = NewObject<UOpenDriveAsset>(Package, FName(*RoadName), RF_Public | RF_Standalone | RF_Transactional);
		}
		if (!Road->SetSource(OdrXml, OdrPath))
		{
			UE_LOG(LogOpenScenario, Warning, TEXT("Referenced OpenDRIVE file '%s' could not be parsed: %s"), *OdrPath, *Road->ParseStatus);
		}
		if (Road->AssetImportData)
		{
			Road->AssetImportData->Update(OdrPath);
		}
		if (bIsNew)
		{
			FAssetRegistryModule::AssetCreated(Road);
		}
		Road->MarkPackageDirty();
		Scenario->RoadNetwork = Road;
	}
}

// ------------------------------------------------------------------------------------------------
// Scenario import
// ------------------------------------------------------------------------------------------------

UOpenScenarioImportFactory::UOpenScenarioImportFactory()
{
	SupportedClass = UOpenScenarioAsset::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("xosc;ASAM OpenSCENARIO"));
}

UObject* UOpenScenarioImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FString Xml;
	if (!FFileHelper::LoadFileToString(Xml, *Filename))
	{
		UE_LOG(LogOpenScenario, Error, TEXT("Could not read '%s'."), *Filename);
		return nullptr;
	}

	UOpenScenarioAsset* Asset = NewObject<UOpenScenarioAsset>(InParent, InClass, InName, Flags);
	if (!Asset->SetSource(Xml, FPaths::ConvertRelativePathToFull(Filename)))
	{
		UE_LOG(LogOpenScenario, Error, TEXT("'%s' is not a valid OpenSCENARIO scenario: %s"), *Filename,
			Asset->ParseMessages.Num() > 0 ? *Asset->ParseMessages.Last() : TEXT("unknown error"));
		return nullptr;
	}
	if (Asset->AssetImportData)
	{
		Asset->AssetImportData->Update(Filename);
	}
	ImportReferencedRoadNetwork(Asset);
	return Asset;
}

bool UOpenScenarioImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UOpenScenarioAsset* Asset = Cast<UOpenScenarioAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return false;
	}
	Asset->AssetImportData->ExtractFilenames(OutFilenames);
	return true;
}

void UOpenScenarioImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UOpenScenarioAsset* Asset = Cast<UOpenScenarioAsset>(Obj);
	if (Asset && Asset->AssetImportData && NewReimportPaths.Num() > 0)
	{
		Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UOpenScenarioImportFactory::Reimport(UObject* Obj)
{
	UOpenScenarioAsset* Asset = Cast<UOpenScenarioAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return EReimportResult::Failed;
	}
	const FString Path = Asset->AssetImportData->GetFirstFilename();
	FString Xml;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Xml, *Path))
	{
		return EReimportResult::Failed;
	}
	if (!Asset->SetSource(Xml, FPaths::ConvertRelativePathToFull(Path)))
	{
		return EReimportResult::Failed;
	}
	Asset->AssetImportData->Update(Path);
	ImportReferencedRoadNetwork(Asset);
	Asset->MarkPackageDirty();
	return EReimportResult::Succeeded;
}

// ------------------------------------------------------------------------------------------------
// New scenario
// ------------------------------------------------------------------------------------------------

UOpenScenarioNewFactory::UOpenScenarioNewFactory()
{
	SupportedClass = UOpenScenarioAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UOpenScenarioNewFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UOpenScenarioAsset* Asset = NewObject<UOpenScenarioAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	Asset->SetSource(UOpenScenarioAsset::MakeTemplateXml(InName.ToString()), FString());
	return Asset;
}

// ------------------------------------------------------------------------------------------------
// OpenDRIVE import
// ------------------------------------------------------------------------------------------------

UOpenDriveImportFactory::UOpenDriveImportFactory()
{
	SupportedClass = UOpenDriveAsset::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("xodr;ASAM OpenDRIVE"));
}

UObject* UOpenDriveImportFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	FString Xml;
	if (!FFileHelper::LoadFileToString(Xml, *Filename))
	{
		UE_LOG(LogOpenScenario, Error, TEXT("Could not read '%s'."), *Filename);
		return nullptr;
	}
	UOpenDriveAsset* Asset = NewObject<UOpenDriveAsset>(InParent, InClass, InName, Flags);
	if (!Asset->SetSource(Xml, FPaths::ConvertRelativePathToFull(Filename)))
	{
		UE_LOG(LogOpenScenario, Error, TEXT("'%s' is not a valid OpenDRIVE file: %s"), *Filename, *Asset->ParseStatus);
		return nullptr;
	}
	if (Asset->AssetImportData)
	{
		Asset->AssetImportData->Update(Filename);
	}
	return Asset;
}

bool UOpenDriveImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UOpenDriveAsset* Asset = Cast<UOpenDriveAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return false;
	}
	Asset->AssetImportData->ExtractFilenames(OutFilenames);
	return true;
}

void UOpenDriveImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UOpenDriveAsset* Asset = Cast<UOpenDriveAsset>(Obj);
	if (Asset && Asset->AssetImportData && NewReimportPaths.Num() > 0)
	{
		Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UOpenDriveImportFactory::Reimport(UObject* Obj)
{
	UOpenDriveAsset* Asset = Cast<UOpenDriveAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return EReimportResult::Failed;
	}
	const FString Path = Asset->AssetImportData->GetFirstFilename();
	FString Xml;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Xml, *Path) || !Asset->SetSource(Xml, FPaths::ConvertRelativePathToFull(Path)))
	{
		return EReimportResult::Failed;
	}
	Asset->AssetImportData->Update(Path);
	Asset->MarkPackageDirty();
	return EReimportResult::Succeeded;
}
