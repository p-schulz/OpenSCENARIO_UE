#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OpenDrive/OpenDriveMap.h"
#include "OpenDriveAsset.generated.h"

class UAssetImportData;

/**
 * An imported ASAM OpenDRIVE (.xodr) road network. The XML is stored inside the asset so packaged
 * builds do not depend on the original file; it is parsed into an FOpenDriveMap on load.
 */
UCLASS(BlueprintType)
class OPENSCENARIO_API UOpenDriveAsset : public UObject
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

	//~ UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	TSharedPtr<FOpenDriveMap> Map;
};
