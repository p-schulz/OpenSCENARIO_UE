#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "EditorReimportHandler.h"
#include "OpenScenarioFactories.generated.h"

/** Imports .xosc files as UOpenScenarioAsset, including the OpenDRIVE file they reference. */
UCLASS()
class UOpenScenarioImportFactory : public UFactory, public FReimportHandler
{
	GENERATED_BODY()

public:
	UOpenScenarioImportFactory();

	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;

	//~ FReimportHandler
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override { return ImportPriority; }
};

/** Creates a new, empty-but-valid scenario from the content browser. */
UCLASS()
class UOpenScenarioNewFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOpenScenarioNewFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
