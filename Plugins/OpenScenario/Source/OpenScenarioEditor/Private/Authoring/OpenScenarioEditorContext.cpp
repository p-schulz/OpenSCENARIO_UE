#include "Authoring/OpenScenarioEditorContext.h"
#include "Authoring/OpenScenarioEditorSettings.h"
#include "Factories/OpenScenarioFactories.h"
#include "OpenDrive/OpenDriveMap.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Scenario/OpenScenarioWriter.h"
#include "Simulation/OpenScenarioActor.h"
#include "Simulation/OpenScenarioRunner.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "Selection.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "OpenScenarioEditorContext"

void UOpenScenarioEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++Revision;
	OnChanged.Broadcast();
}

FOpenScenarioEditorContext::FOpenScenarioEditorContext()
{
	Settings.Reset(NewObject<UOpenScenarioEditorSettings>(GetTransientPackage(), NAME_None, RF_Transient));
	Settings->OnChanged.AddLambda([this]() { OnVisualizationChanged.Broadcast(); });
}

FOpenScenarioEditorContext::~FOpenScenarioEditorContext()
{
	UnbindAsset();
}

// ------------------------------------------------------------------------------------------------
// Asset & model
// ------------------------------------------------------------------------------------------------

void FOpenScenarioEditorContext::UnbindAsset()
{
	if (UOpenScenarioAsset* Old = Asset.Get())
	{
		Old->OnReparsed.Remove(ReparsedHandle);
	}
	ReparsedHandle.Reset();
}

void FOpenScenarioEditorContext::SetAsset(UOpenScenarioAsset* NewAsset)
{
	if (Asset.Get() == NewAsset)
	{
		return;
	}
	UnbindAsset();
	Asset = NewAsset;
	Selection = FOSCNodeRef();
	if (NewAsset)
	{
		ReparsedHandle = NewAsset->OnReparsed.AddRaw(this, &FOpenScenarioEditorContext::HandleAssetReparsed);
	}
	ReloadWorking();
	OnAssetChanged.Broadcast();
	OnSelectionChanged.Broadcast();
	OnVisualizationChanged.Broadcast();
}

void FOpenScenarioEditorContext::ReloadWorking()
{
	const UOpenScenarioAsset* A = Asset.Get();
	Working = (A && A->IsScenarioValid()) ? A->GetScenario() : FOSCScenario();
	bDirty = false;
	++ModelRevision;
}

void FOpenScenarioEditorContext::HandleAssetReparsed()
{
	if (bApplying)
	{
		return;
	}
	// The asset text changed from outside (details panel, undo, reimport): follow it unless the user has
	// unapplied edits, which stay until they Apply or Revert.
	if (!bDirty)
	{
		ReloadWorking();
		Selection = FOSCNodeRef();
		OnStructureChanged.Broadcast();
		OnSelectionChanged.Broadcast();
	}
	OnVisualizationChanged.Broadcast();
}

void FOpenScenarioEditorContext::NotifyStructureChanged()
{
	bDirty = true;
	++ModelRevision;
	OnStructureChanged.Broadcast();
	OnVisualizationChanged.Broadcast();
}

void FOpenScenarioEditorContext::NotifyValueChanged()
{
	bDirty = true;
	++ModelRevision;
	OnValueChanged.Broadcast();
	OnVisualizationChanged.Broadcast();
}

bool FOpenScenarioEditorContext::ApplyWouldLoseInformation() const
{
	const UOpenScenarioAsset* A = Asset.Get();
	if (!A)
	{
		return false;
	}
	return A->ParseMessages.Num() > 0 || A->SourceXml.Contains(TEXT("$")) || A->SourceXml.Contains(TEXT("<!--")) || A->SourceXml.Contains(TEXT("CatalogReference"));
}

bool FOpenScenarioEditorContext::Apply(bool bConfirmLossy)
{
	UOpenScenarioAsset* A = Asset.Get();
	if (!A)
	{
		return false;
	}
	if (bConfirmLossy && ApplyWouldLoseInformation())
	{
		const FText Message = LOCTEXT("LossyApply",
			"Applying regenerates the scenario XML from the editor's model.\n\n"
			"Comments, unsupported elements (see the asset's Parse Messages), catalog references and $parameter references "
			"(values are written resolved) are not preserved.\n\nApply anyway?");
		if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes)
		{
			return false;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("ApplyEdits", "Apply OpenSCENARIO edits"));
	A->Modify();
	bApplying = true;
	const bool bOk = A->ApplyScenarioModel(Working);
	bApplying = false;
	A->MarkPackageDirty();

	ReloadWorking();
	OnStructureChanged.Broadcast();
	OnVisualizationChanged.Broadcast();
	return bOk;
}

void FOpenScenarioEditorContext::Revert()
{
	ReloadWorking();
	Selection = FOSCNodeRef();
	OnStructureChanged.Broadcast();
	OnSelectionChanged.Broadcast();
	OnVisualizationChanged.Broadcast();
}

const TArray<FString>& FOpenScenarioEditorContext::GetIssues()
{
	if (IssuesRevision != ModelRevision)
	{
		FOSCModelEdit::Validate(Working, Issues);
		IssuesRevision = ModelRevision;
	}
	return Issues;
}

void FOpenScenarioEditorContext::SetSelection(const FOSCNodeRef& Node)
{
	Selection = Node;
	OnSelectionChanged.Broadcast();
}

// ------------------------------------------------------------------------------------------------
// Import / create / export
// ------------------------------------------------------------------------------------------------

UObject* FOpenScenarioEditorContext::ImportFile(const FString& FilePath)
{
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = Settings->ImportDestination;
	Task->bAutomated = true;
	Task->bReplaceExisting = true;
	Task->bSave = false;

	FAssetToolsModule::GetModule().Get().ImportAssetTasks({ Task });
	for (UObject* Imported : Task->GetObjects())
	{
		if (Imported)
		{
			if (UOpenScenarioAsset* Scenario = Cast<UOpenScenarioAsset>(Imported))
			{
				SetAsset(Scenario);
			}
			return Imported;
		}
	}
	return nullptr;
}

UOpenScenarioAsset* FOpenScenarioEditorContext::CreateNewScenario()
{
	IAssetTools& Tools = FAssetToolsModule::GetModule().Get();
	FString PackageName, AssetName;
	Tools.CreateUniqueAssetName(Settings->ImportDestination / TEXT("NewScenario"), FString(), PackageName, AssetName);

	UObject* Created = Tools.CreateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), UOpenScenarioAsset::StaticClass(), NewObject<UOpenScenarioNewFactory>());
	UOpenScenarioAsset* Scenario = Cast<UOpenScenarioAsset>(Created);
	if (Scenario)
	{
		SetAsset(Scenario);
	}
	return Scenario;
}

bool FOpenScenarioEditorContext::ExportScenarioFile()
{
	UOpenScenarioAsset* A = Asset.Get();
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!A || !Platform)
	{
		return false;
	}
	TArray<FString> Files;
	const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	if (!Platform->SaveFileDialog(Parent, TEXT("Export OpenSCENARIO"), FPaths::ProjectDir(), A->GetName() + TEXT(".xosc"), TEXT("OpenSCENARIO (*.xosc)|*.xosc"), EFileDialogFlags::None, Files) || Files.Num() == 0)
	{
		return false;
	}
	// Unapplied edits are exported as they are; otherwise the asset's own text (keeps comments etc.).
	const FString Xml = bDirty ? FOpenScenarioWriter::Write(Working) : A->SourceXml;
	return FFileHelper::SaveStringToFile(Xml, *Files[0], FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

AActor* FOpenScenarioEditorContext::PlaceScenarioActor()
{
	UOpenScenarioAsset* A = Asset.Get();
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!A || !World)
	{
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("PlaceActor", "Place OpenSCENARIO Actor"));
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transactional;
	AOpenScenarioActor* Actor = World->SpawnActor<AOpenScenarioActor>(AOpenScenarioActor::StaticClass(), FTransform::Identity, Params);
	if (Actor)
	{
		Actor->Scenario = A;
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(Actor, true, true);
		OnVisualizationChanged.Broadcast();
	}
	return Actor;
}

// ------------------------------------------------------------------------------------------------
// Playback
// ------------------------------------------------------------------------------------------------

AOpenScenarioActor* FOpenScenarioEditorContext::FindPlaybackActor() const
{
	UOpenScenarioAsset* A = Asset.Get();
	if (!A || !GEditor)
	{
		return nullptr;
	}
	auto Search = [A](UWorld* World) -> AOpenScenarioActor*
	{
		if (World)
		{
			for (TActorIterator<AOpenScenarioActor> It(World); It; ++It)
			{
				if (It->Scenario == A)
				{
					return *It;
				}
			}
		}
		return nullptr;
	};
	// A running Play-In-Editor session takes precedence over the editor world.
	if (AOpenScenarioActor* InPie = Search(GEditor->PlayWorld))
	{
		return InPie;
	}
	return Search(GEditor->GetEditorWorldContext().World());
}

void FOpenScenarioEditorContext::RefreshPlaybackCache() const
{
	if (PlaybackCacheFrame == GFrameCounter)
	{
		return;
	}
	PlaybackCacheFrame = GFrameCounter;
	const AOpenScenarioActor* Actor = FindPlaybackActor();
	bCachedHasActor = Actor != nullptr;
	CachedPlaybackState = Actor ? Actor->GetPlaybackState() : EOpenScenarioPlaybackState::Stopped;
	CachedPlaybackTime = Actor ? Actor->GetSimulationTime() : 0.0;
	CachedTimeScale = Actor ? Actor->TimeScale : 1.f;
}

EOpenScenarioPlaybackState FOpenScenarioEditorContext::GetPlaybackState() const { RefreshPlaybackCache(); return CachedPlaybackState; }
double FOpenScenarioEditorContext::GetPlaybackTime() const { RefreshPlaybackCache(); return CachedPlaybackTime; }
bool FOpenScenarioEditorContext::HasPlaybackActor() const { RefreshPlaybackCache(); return bCachedHasActor; }
float FOpenScenarioEditorContext::GetPlaybackTimeScale() const { RefreshPlaybackCache(); return CachedTimeScale; }

void FOpenScenarioEditorContext::SetPlaybackTimeScale(float Scale)
{
	if (AOpenScenarioActor* Actor = FindPlaybackActor())
	{
		Actor->SetTimeScale(Scale);
	}
	PlaybackCacheFrame = MAX_uint64;
}

bool FOpenScenarioEditorContext::PrepareForPlayback()
{
	// The simulation reads the asset, so unapplied storyboard edits have to be written first.
	return !bDirty || Apply(true);
}

void FOpenScenarioEditorContext::PlaybackPlay()
{
	if (!Asset.IsValid() || !PrepareForPlayback())
	{
		return;
	}
	AOpenScenarioActor* Actor = FindPlaybackActor();
	if (!Actor)
	{
		Actor = Cast<AOpenScenarioActor>(PlaceScenarioActor());
	}
	if (Actor)
	{
		Actor->PlayScenario();
	}
	PlaybackCacheFrame = MAX_uint64;
}

void FOpenScenarioEditorContext::PlaybackPause()
{
	if (AOpenScenarioActor* Actor = FindPlaybackActor())
	{
		Actor->PauseScenario();
	}
	PlaybackCacheFrame = MAX_uint64;
}

void FOpenScenarioEditorContext::PlaybackStep()
{
	if (!Asset.IsValid() || !PrepareForPlayback())
	{
		return;
	}
	AOpenScenarioActor* Actor = FindPlaybackActor();
	if (!Actor)
	{
		Actor = Cast<AOpenScenarioActor>(PlaceScenarioActor());
	}
	if (Actor)
	{
		Actor->StepScenario();
	}
	PlaybackCacheFrame = MAX_uint64;
}

void FOpenScenarioEditorContext::PlaybackStop()
{
	if (AOpenScenarioActor* Actor = FindPlaybackActor())
	{
		Actor->StopScenario();
	}
	PlaybackCacheFrame = MAX_uint64;
}

void FOpenScenarioEditorContext::PlaybackRestart()
{
	if (!Asset.IsValid() || !PrepareForPlayback())
	{
		return;
	}
	if (AOpenScenarioActor* Actor = FindPlaybackActor())
	{
		Actor->RestartScenario();
	}
	else
	{
		PlaybackPlay();
	}
	PlaybackCacheFrame = MAX_uint64;
}

// ------------------------------------------------------------------------------------------------
// Actor class mapping
// ------------------------------------------------------------------------------------------------

void FOpenScenarioEditorContext::SetEntityActorClass(const FString& EntityName, const UClass* ActorClass)
{
	UOpenScenarioAsset* A = Asset.Get();
	if (!A)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("MapEntityClass", "Map entity actor class"));
	A->Modify();
	if (ActorClass)
	{
		A->EntityActorClasses.Add(EntityName, TSoftClassPtr<AActor>(ActorClass));
	}
	else
	{
		A->EntityActorClasses.Remove(EntityName);
	}
	A->MarkPackageDirty();
	OnMappingChanged.Broadcast();
}

void FOpenScenarioEditorContext::SetKindActorClass(EOSCEntityKind Kind, const UClass* ActorClass)
{
	UOpenScenarioAsset* A = Asset.Get();
	if (!A)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("MapKindClass", "Map default actor class"));
	A->Modify();
	TSoftClassPtr<AActor>& Slot = (Kind == EOSCEntityKind::Pedestrian) ? A->PedestrianActorClass
		: (Kind == EOSCEntityKind::MiscObject || Kind == EOSCEntityKind::External) ? A->MiscObjectActorClass : A->VehicleActorClass;
	Slot = ActorClass ? TSoftClassPtr<AActor>(ActorClass) : TSoftClassPtr<AActor>();
	A->MarkPackageDirty();
	OnMappingChanged.Broadcast();
}

const UClass* FOpenScenarioEditorContext::GetEntityActorClass(const FString& EntityName) const
{
	const UOpenScenarioAsset* A = Asset.Get();
	return A ? A->FindEntityActorClass(EntityName) : nullptr;
}

const UClass* FOpenScenarioEditorContext::GetKindActorClass(EOSCEntityKind Kind) const
{
	const UOpenScenarioAsset* A = Asset.Get();
	return A ? A->FindKindActorClass(Kind) : nullptr;
}

// ------------------------------------------------------------------------------------------------
// Visualisation helpers
// ------------------------------------------------------------------------------------------------

TSharedPtr<const FOpenDriveMap> FOpenScenarioEditorContext::GetMap() const
{
	// Not cached: the road network can be edited in the OpenDRIVE plugin's editor at any time and
	// UOpenScenarioAsset::ResolveRoadNetwork is cheap.
	UOpenScenarioAsset* A = Asset.Get();
	return A ? A->ResolveRoadNetwork() : nullptr;
}

FTransform FOpenScenarioEditorContext::ResolveOrigin() const
{
	if (const AActor* Explicit = Settings->OriginActor.Get())
	{
		FTransform T = Explicit->GetActorTransform();
		T.SetScale3D(FVector::OneVector);
		return T;
	}
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AOpenScenarioActor> It(World); It; ++It)
		{
			if (It->Scenario == Asset.Get())
			{
				FTransform T = It->GetActorTransform();
				T.SetScale3D(FVector::OneVector);
				return T;
			}
		}
	}
	return FTransform::Identity;
}

#undef LOCTEXT_NAMESPACE
