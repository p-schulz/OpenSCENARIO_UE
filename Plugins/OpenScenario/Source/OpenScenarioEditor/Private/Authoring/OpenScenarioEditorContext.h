#pragma once

#include "CoreMinimal.h"
#include "Scenario/OpenScenarioModel.h"
#include "Scenario/OpenScenarioModelEdit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class UOpenScenarioAsset;
class UOpenScenarioEditorSettings;
class FOpenDriveMap;
class AActor;

/**
 * Shared editing state: the active scenario asset, an editable working copy of its model, the tree
 * selection and the visualisation settings. Edits happen on the working copy; Apply writes it back to
 * the asset (undoable), Revert discards it.
 */
class FOpenScenarioEditorContext
{
public:
	FOpenScenarioEditorContext();
	~FOpenScenarioEditorContext();

	// --- Asset & model ---------------------------------------------------------------------
	UOpenScenarioAsset* GetAsset() const { return Asset.Get(); }
	void SetAsset(UOpenScenarioAsset* NewAsset);

	FOSCScenario& GetWorking() { return Working; }
	const FOSCScenario& GetWorking() const { return Working; }

	bool IsDirty() const { return bDirty; }
	uint32 GetModelRevision() const { return ModelRevision; }

	/** Structure changed (nodes added/removed/moved). */
	void NotifyStructureChanged();
	/** Only values changed. */
	void NotifyValueChanged();

	/** Writes the working copy to the asset. Asks first if information would be lost. */
	bool Apply(bool bConfirmLossy = true);
	void Revert();
	/** True if applying would drop unsupported elements, comments or parameter references. */
	bool ApplyWouldLoseInformation() const;

	const TArray<FString>& GetIssues();

	// --- Selection -------------------------------------------------------------------------
	const FOSCNodeRef& GetSelection() const { return Selection; }
	void SetSelection(const FOSCNodeRef& Node);

	// --- Import / create / export ----------------------------------------------------------
	UObject* ImportFile(const FString& FilePath);
	UOpenScenarioAsset* CreateNewScenario();
	bool ExportScenarioFile();
	AActor* PlaceScenarioActor();

	// --- Actor class mapping (stored in the asset) -----------------------------------------
	void SetEntityActorClass(const FString& EntityName, const UClass* ActorClass);
	void SetKindActorClass(EOSCEntityKind Kind, const UClass* ActorClass);
	const UClass* GetEntityActorClass(const FString& EntityName) const;
	const UClass* GetKindActorClass(EOSCEntityKind Kind) const;

	// --- Visualisation ---------------------------------------------------------------------
	UOpenScenarioEditorSettings* GetSettings() const { return Settings.Get(); }
	TSharedPtr<const FOpenDriveMap> GetMap() const;
	FTransform ResolveOrigin() const;

	// --- Events ----------------------------------------------------------------------------
	FSimpleMulticastDelegate OnAssetChanged;
	FSimpleMulticastDelegate OnStructureChanged;
	FSimpleMulticastDelegate OnValueChanged;
	FSimpleMulticastDelegate OnSelectionChanged;
	FSimpleMulticastDelegate OnMappingChanged;
	/** Anything that affects the viewport drawing. */
	FSimpleMulticastDelegate OnVisualizationChanged;

private:
	void HandleAssetReparsed();
	void ReloadWorking();
	void UnbindAsset();

	TWeakObjectPtr<UOpenScenarioAsset> Asset;
	FOSCScenario Working;
	FOSCNodeRef Selection;
	bool bDirty = false;
	bool bApplying = false;
	uint32 ModelRevision = 0;
	TArray<FString> Issues;
	uint32 IssuesRevision = MAX_uint32;
	FDelegateHandle ReparsedHandle;
	mutable TSharedPtr<const FOpenDriveMap> CachedMap;
	mutable TWeakObjectPtr<UOpenScenarioAsset> CachedMapAsset;

	TStrongObjectPtr<UOpenScenarioEditorSettings> Settings;
};
