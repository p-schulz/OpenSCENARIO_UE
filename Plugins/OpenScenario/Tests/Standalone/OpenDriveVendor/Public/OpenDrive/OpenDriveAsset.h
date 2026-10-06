#pragma once

#include "CoreMinimal.h"
#include "OpenDriveVersion.h"
#include "UObject/Object.h"
#include "OpenDrive/OpenDriveMap.h"
#include "OpenDriveAsset.generated.h"

class UAssetImportData;

/**
 * An ASAM OpenDRIVE (.xodr) road network: imported from a file, created from scratch in the OpenDRIVE
 * editor tool mode, or both. The XML is stored inside the asset so packaged builds do not depend on the
 * original file; it is parsed into an FOpenDriveMap on load. This is the single OpenDRIVE data model
 * shared with other plugins (e.g. OpenScenario_UE) that reference road networks.
 */
UCLASS(BlueprintType)
class OPENDRIVE_API UOpenDriveAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "OpenDRIVE", AdvancedDisplay, meta = (MultiLine = "true"))
	FString SourceXml;

	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE")
	FString SourceFilename;

	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE|Info")
	FString MapName;

	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE|Info")
	int32 RoadCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE|Info")
	int32 JunctionCount = 0;

	/** Sum of all road lengths in metres. */
	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE|Info")
	double TotalRoadLength = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "OpenDRIVE|Info")
	FString ParseStatus;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = "ImportSettings")
	TObjectPtr<UAssetImportData> AssetImportData;
#endif

	/** Sets the XML text and parses it. Returns false (and fills ParseStatus) if parsing fails. */
	bool SetSource(const FString& Xml, const FString& Filename);

	UFUNCTION(CallInEditor, Category = "OpenDRIVE")
	void Reparse();

	TSharedPtr<const FOpenDriveMap> GetMap() const { return Map; }

	UFUNCTION(BlueprintPure, Category = "OpenDRIVE")
	bool IsMapValid() const { return Map.IsValid(); }

	UFUNCTION(BlueprintCallable, Category = "OpenDRIVE")
	TArray<FString> GetRoadIds() const;

	UFUNCTION(BlueprintCallable, Category = "OpenDRIVE")
	double GetRoadLength(const FString& RoadId) const;

	/** Transform (Unreal coordinates, relative to the OpenDRIVE origin) of the point at (s, t) on a road. */
	UFUNCTION(BlueprintCallable, Category = "OpenDRIVE")
	bool GetRoadTransform(const FString& RoadId, double S, double T, FTransform& OutTransform) const;

	/** Writes a new working map (e.g. from the OpenDRIVE editor tool mode), regenerating SourceXml from it. */
	void ApplyMap(const FOpenDriveMap& NewMap);

	/** Serialises the current map to an .xodr file on disk. */
	bool ExportToFile(const FString& Filename) const;

	/** Broadcast after every (re)parse, e.g. so editor tools can refresh. */
	FSimpleMulticastDelegate OnReparsed;

	//~ UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if ODR_UE_AT_LEAST(5, 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
#else
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	TSharedPtr<FOpenDriveMap> Map;
};
