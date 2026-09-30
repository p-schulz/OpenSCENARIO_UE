#pragma once

#include "CoreMinimal.h"
#include "OpenScenarioVersion.h"
#include "UObject/Object.h"
#include "Scenario/OpenScenarioModel.h"
#include "OpenScenarioAsset.generated.h"

class AActor;
class UAssetImportData;
class UOpenDriveAsset;
class FOpenDriveMap;

/**
 * An ASAM OpenSCENARIO (.xosc) scenario. The XML is stored verbatim in the asset (and can be edited
 * in the details panel), parsed into FOSCScenario on load, and simulated by AOpenScenarioActor.
 */
UCLASS(BlueprintType)
class OPENSCENARIO_API UOpenScenarioAsset : public UObject
{
	GENERATED_BODY()

public:
	/** The OpenSCENARIO XML document. Edit and press Reparse (or leave the field) to update. */
	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO", meta = (MultiLine = "true"))
	FString SourceXml;

	/** Original .xosc path (used to resolve relative OpenDRIVE and catalog paths). */
	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO")
	FString SourceFilename;

	/**
	 * Road network used for routing and lane/road positions. Filled automatically on import from the
	 * scenario's RoadNetwork/LogicFile; assign manually to override.
	 */
	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO")
	TObjectPtr<UOpenDriveAsset> RoadNetwork;

	/** Values replacing the defaults of top-level ParameterDeclarations (name -> value). */
	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO")
	TMap<FString, FString> ParameterOverrides;

	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO|Info")
	FString ScenarioDescription;

	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO|Info")
	FString Author;

	/** LogicFile path as written in the scenario. */
	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO|Info")
	FString RoadNetworkFile;

	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO|Info")
	TArray<FString> EntityNames;

	/** Actor class spawned for entities of the given kind (overridable per entity below). */
	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO|Actor Mapping")
	TSoftClassPtr<AActor> VehicleActorClass;

	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO|Actor Mapping")
	TSoftClassPtr<AActor> PedestrianActorClass;

	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO|Actor Mapping")
	TSoftClassPtr<AActor> MiscObjectActorClass;

	/** Actor class per scenario entity name (e.g. "Ego"). Takes precedence over the per-kind classes. */
	UPROPERTY(EditAnywhere, Category = "OpenSCENARIO|Actor Mapping")
	TMap<FString, TSoftClassPtr<AActor>> EntityActorClasses;

	/** Parser warnings and errors from the last parse. */
	UPROPERTY(VisibleAnywhere, Category = "OpenSCENARIO|Info")
	TArray<FString> ParseMessages;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = "ImportSettings")
	TObjectPtr<UAssetImportData> AssetImportData;
#endif

	bool SetSource(const FString& Xml, const FString& Filename);

	UFUNCTION(CallInEditor, Category = "OpenSCENARIO")
	void Reparse();

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	bool IsScenarioValid() const { return bParsedOk; }

	UFUNCTION(BlueprintPure, Category = "OpenSCENARIO")
	TArray<FString> GetEntityNames() const { return EntityNames; }

	const FOSCScenario& GetScenario() const { return Scenario; }

	/** Serialises the model back to XML (see FOpenScenarioWriter for what is preserved) and re-parses it. */
	bool ApplyScenarioModel(const FOSCScenario& Model);

	/** Loaded class mapped to the entity by name, or null. */
	UClass* FindEntityActorClass(const FString& EntityName) const;
	/** Loaded class mapped to the entity kind, or null. */
	UClass* FindKindActorClass(EOSCEntityKind Kind) const;

	/** Broadcast after every (re)parse, e.g. so editor tools can refresh. */
	FSimpleMulticastDelegate OnReparsed;

	/** Directory used to resolve relative paths (directory of the original .xosc, else the project dir). */
	FString GetBaseDirectory() const;

	/** Locates and reads the OpenDRIVE file referenced by the scenario from disk. */
	bool ReadReferencedRoadNetworkFile(FString& OutXml, FString& OutResolvedPath) const;

	/** Imported road network if available, otherwise the referenced file loaded from disk (cached). */
	TSharedPtr<const FOpenDriveMap> ResolveRoadNetwork();

	static FString MakeTemplateXml(const FString& Name);

	//~ UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
#if OSC_UE_AT_LEAST(5, 4)
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
#else
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	FOSCScenario Scenario;
	bool bParsedOk = false;
	TSharedPtr<FOpenDriveMap> FallbackMap;
};
