#include "OpenDrive/OpenDriveAsset.h"
#include "OpenScenarioCoordinates.h"
#include "OpenScenarioModule.h"
#if OSC_UE_AT_LEAST(5, 4)
#include "UObject/AssetRegistryTagsContext.h"
#endif
#include "UObject/UnrealType.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

bool UOpenDriveAsset::SetSource(const FString& Xml, const FString& Filename)
{
	SourceXml = Xml;
	SourceFilename = Filename;
	Reparse();
	return Map.IsValid();
}

void UOpenDriveAsset::Reparse()
{
	Map.Reset();
	RoadCount = 0;
	JunctionCount = 0;
	TotalRoadLength = 0.0;
	MapName.Reset();

	TSharedPtr<FOpenDriveMap> NewMap = MakeShared<FOpenDriveMap>();
	FString Error;
	if (NewMap->LoadFromString(SourceXml, Error))
	{
		Map = NewMap;
		MapName = Map->GetName();
		RoadCount = Map->GetRoads().Num();
		JunctionCount = Map->GetJunctions().Num();
		TotalRoadLength = Map->GetTotalLength();
		ParseStatus = TEXT("OK");
	}
	else
	{
		ParseStatus = FString::Printf(TEXT("Error: %s"), *Error);
		UE_LOG(LogOpenScenario, Warning, TEXT("OpenDRIVE asset '%s': %s"), *GetPathName(), *ParseStatus);
	}
}

TArray<FString> UOpenDriveAsset::GetRoadIds() const
{
	TArray<FString> Ids;
	if (Map.IsValid())
	{
		for (const FOpenDriveRoad& R : Map->GetRoads())
		{
			Ids.Add(R.Id);
		}
	}
	return Ids;
}

double UOpenDriveAsset::GetRoadLength(const FString& RoadId) const
{
	const FOpenDriveRoad* Road = Map.IsValid() ? Map->FindRoad(RoadId) : nullptr;
	return Road ? Road->Length : 0.0;
}

bool UOpenDriveAsset::GetRoadTransform(const FString& RoadId, double S, double T, FTransform& OutTransform) const
{
	const FOpenDriveRoad* Road = Map.IsValid() ? Map->FindRoad(RoadId) : nullptr;
	if (!Road)
	{
		return false;
	}
	const FOpenDrivePose Pose = Map->EvaluatePose(*Road, S, T);
	OutTransform = FTransform(
		FRotator(0.0, OpenScenarioCoords::HeadingToYawDegrees(Pose.Heading), 0.0),
		OpenScenarioCoords::ToUnrealLocation(Pose.X, Pose.Y, Pose.Z));
	return true;
}

void UOpenDriveAsset::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

void UOpenDriveAsset::PostLoad()
{
	Super::PostLoad();
	if (!SourceXml.IsEmpty())
	{
		Reparse();
	}
}

#if OSC_UE_AT_LEAST(5, 4)
void UOpenDriveAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		Context.AddTag(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}
#endif
	Super::GetAssetRegistryTags(Context);
}
#else
void UOpenDriveAsset::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}
#endif
	Super::GetAssetRegistryTags(OutTags);
}
#endif

#if WITH_EDITOR
void UOpenDriveAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UOpenDriveAsset, SourceXml))
	{
		Reparse();
	}
}
#endif
