#include "Mode/SOpenScenarioModePanel.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Authoring/OpenScenarioEditorSettings.h"
#include "OpenDrive/OpenDriveAsset.h"
#include "OpenScenarioEditorModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "AssetRegistry/AssetData.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "IDesktopPlatform.h"
#include "IDetailsView.h"
#include "Misc/Paths.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "SClassPropertyEntryBox.h"
#include "Selection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OpenScenarioModePanel"

void SOpenScenarioModePanel::Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext)
{
	Context = &InContext;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.bShowOptions = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	SettingsView = PropertyModule.CreateDetailView(Args);
	SettingsView->SetObject(Context->GetSettings());

	auto MakeButton = [](const FText& Label, const FText& Tooltip, TFunction<FReply()> OnClick, TFunction<bool()> IsEnabled)
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClick]() { return OnClick(); })
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); });
	};
	FOpenScenarioEditorContext* Ctx = Context;
	auto HasAsset = [Ctx]() { return Ctx->GetAsset() != nullptr; };
	auto Always = []() { return true; };

	SAssignNew(MappingBox, SVerticalBox);

	ChildSlot
	[
		SNew(SScrollBox)

		+ SScrollBox::Slot().Padding(4.f)
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("ScenarioSection", "Scenario"))
			.InitiallyCollapsed(false)
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UOpenScenarioAsset::StaticClass())
					.ObjectPath(this, &SOpenScenarioModePanel::GetAssetPath)
					.OnObjectChanged(this, &SOpenScenarioModePanel::OnAssetPicked)
					.DisplayThumbnail(false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
				[
					SNew(SWrapBox).UseAllottedSize(true)
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("New", "New"), LOCTEXT("NewTip", "Create a new scenario asset from a template"),
							[Ctx]() { Ctx->CreateNewScenario(); return FReply::Handled(); }, Always)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("ImportXosc", "Import XOSC..."), LOCTEXT("ImportXoscTip", "Import an OpenSCENARIO file (and the OpenDRIVE file it references)"),
							[this]() { return OnImport(false); }, Always)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("ImportXodr", "Import XODR..."), LOCTEXT("ImportXodrTip", "Import an OpenDRIVE road network"),
							[this]() { return OnImport(true); }, Always)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("Export", "Export XOSC..."), LOCTEXT("ExportTip", "Save the scenario as an .xosc file"),
							[Ctx]() { Ctx->ExportScenarioFile(); return FReply::Handled(); }, HasAsset)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("Storyboard", "Storyboard"), LOCTEXT("StoryboardTip", "Open the storyboard editor tab"),
							[]() { FOpenScenarioEditorModule::Get().OpenStoryboardTab(); return FReply::Handled(); }, Always)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("PlaceActor", "Place Actor"), LOCTEXT("PlaceActorTip", "Place an OpenScenario Actor for this scenario in the level"),
							[Ctx]() { Ctx->PlaceScenarioActor(); return FReply::Handled(); }, HasAsset)
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("Apply", "Apply"), LOCTEXT("ApplyTip", "Write unapplied storyboard edits to the asset"),
							[Ctx]() { Ctx->Apply(); return FReply::Handled(); }, [Ctx]() { return Ctx->GetAsset() && Ctx->IsDirty(); })
					]
					+ SWrapBox::Slot().Padding(0.f, 0.f, 4.f, 4.f)
					[
						MakeButton(LOCTEXT("Reparse", "Reparse"), LOCTEXT("ReparseTip", "Re-read the scenario XML stored in the asset"),
							[Ctx]() { if (UOpenScenarioAsset* A = Ctx->GetAsset()) { A->Reparse(); } return FReply::Handled(); }, HasAsset)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(2.f)
				[
					SNew(STextBlock).Text(this, &SOpenScenarioModePanel::GetInfoText).AutoWrapText(true)
				]
			]
		]

		+ SScrollBox::Slot().Padding(4.f)
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("MappingSection", "Actor Classes"))
			.InitiallyCollapsed(false)
			.BodyContent()
			[
				MappingBox.ToSharedRef()
			]
		]

		+ SScrollBox::Slot().Padding(4.f)
		[
			SNew(SExpandableArea)
			.AreaTitle(LOCTEXT("VizSection", "OpenDRIVE Visualization and Import"))
			.InitiallyCollapsed(false)
			.BodyContent()
			[
				SettingsView.ToSharedRef()
			]
		]
	];

	AssetHandle = Context->OnAssetChanged.AddRaw(this, &SOpenScenarioModePanel::RebuildMappingRows);
	StructureHandle = Context->OnStructureChanged.AddRaw(this, &SOpenScenarioModePanel::RebuildMappingRows);
	MappingHandle = Context->OnMappingChanged.AddRaw(this, &SOpenScenarioModePanel::RebuildMappingRows);
	RebuildMappingRows();
}

SOpenScenarioModePanel::~SOpenScenarioModePanel()
{
	if (Context)
	{
		Context->OnAssetChanged.Remove(AssetHandle);
		Context->OnStructureChanged.Remove(StructureHandle);
		Context->OnMappingChanged.Remove(MappingHandle);
	}
}

// ------------------------------------------------------------------------------------------------
// Asset selection / import
// ------------------------------------------------------------------------------------------------

FString SOpenScenarioModePanel::GetAssetPath() const
{
	const UOpenScenarioAsset* Asset = Context->GetAsset();
	return Asset ? Asset->GetPathName() : FString();
}

void SOpenScenarioModePanel::OnAssetPicked(const FAssetData& AssetData)
{
	Context->SetAsset(Cast<UOpenScenarioAsset>(AssetData.GetAsset()));
}

FReply SOpenScenarioModePanel::OnImport(bool bRoad)
{
	IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
	if (!Platform)
	{
		return FReply::Handled();
	}
	TArray<FString> Files;
	const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bPicked = Platform->OpenFileDialog(Parent,
		bRoad ? TEXT("Import OpenDRIVE") : TEXT("Import OpenSCENARIO"), FPaths::ProjectDir(), FString(),
		bRoad ? TEXT("OpenDRIVE (*.xodr)|*.xodr") : TEXT("OpenSCENARIO (*.xosc)|*.xosc"), EFileDialogFlags::None, Files);
	if (bPicked)
	{
		for (const FString& File : Files)
		{
			Context->ImportFile(File);
		}
	}
	return FReply::Handled();
}

FText SOpenScenarioModePanel::GetInfoText() const
{
	const UOpenScenarioAsset* Asset = Context->GetAsset();
	if (!Asset)
	{
		return LOCTEXT("NoScenario", "No scenario selected.");
	}
	if (!Asset->IsScenarioValid())
	{
		return FText::Format(LOCTEXT("Invalid", "Scenario XML could not be parsed:\n{0}"),
			FText::FromString(Asset->ParseMessages.Num() > 0 ? Asset->ParseMessages.Last() : FString(TEXT("unknown error"))));
	}

	const FOSCScenario& S = Context->GetWorking();
	int32 Acts = 0, Events = 0, Actions = 0;
	for (const FOSCStory& Story : S.Stories)
	{
		Acts += Story.Acts.Num();
		for (const FOSCAct& Act : Story.Acts)
		{
			for (const FOSCManeuverGroup& MG : Act.ManeuverGroups)
			{
				for (const FOSCManeuver& Man : MG.Maneuvers)
				{
					Events += Man.Events.Num();
					for (const FOSCEvent& Ev : Man.Events)
					{
						Actions += Ev.Actions.Num();
					}
				}
			}
		}
	}

	const TSharedPtr<const FOpenDriveMap> Map = Context->GetMap();
	FString RoadLine;
	if (S.RoadNetworkFile.IsEmpty())
	{
		RoadLine = TEXT("Road network: none");
	}
	else if (Asset->RoadNetwork)
	{
		RoadLine = FString::Printf(TEXT("Road network: %s (%d roads, %.0f m)"), *Asset->RoadNetwork->GetName(), Asset->RoadNetwork->RoadCount, Asset->RoadNetwork->TotalRoadLength);
	}
	else
	{
		RoadLine = FString::Printf(TEXT("Road network: %s (%s)"), *S.RoadNetworkFile, Map.IsValid() ? TEXT("loaded from disk") : TEXT("NOT FOUND"));
	}

	FString Text = FString::Printf(TEXT("%s\nAuthor: %s\n%s\n%d entities, %d stories, %d acts, %d events, %d actions"),
		S.Description.IsEmpty() ? TEXT("(no description)") : *S.Description, *S.Author, *RoadLine, S.Entities.Num(), S.Stories.Num(), Acts, Events, Actions);
	if (Asset->ParseMessages.Num() > 0)
	{
		Text += FString::Printf(TEXT("\n%d parser message(s): %s"), Asset->ParseMessages.Num(), *Asset->ParseMessages[0]);
	}
	if (Context->IsDirty())
	{
		Text += TEXT("\nUnapplied storyboard edits.");
	}
	return FText::FromString(Text);
}

// ------------------------------------------------------------------------------------------------
// Actor class mapping
// ------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SOpenScenarioModePanel::MakeClassRow(const FText& Label, TFunction<const UClass*()> Getter, TFunction<void(const UClass*)> Setter, const FString& EntityName)
{
	FOpenScenarioEditorContext* Ctx = Context;
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(0.3f).VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock).Text(Label)
		]
		+ SHorizontalBox::Slot().FillWidth(0.7f).Padding(0.f, 1.f)
		[
			SNew(SClassPropertyEntryBox)
			.MetaClass(AActor::StaticClass())
			.AllowNone(true)
			.AllowAbstract(false)
			.SelectedClass_Lambda([Getter]() { return Getter(); })
			.OnSetClass_Lambda([Setter](const UClass* Class) { Setter(Class); })
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("UseSelected", "Use selected"))
			.ToolTipText(LOCTEXT("UseSelectedTip", "Use the class of the actor currently selected in the level"))
			.OnClicked_Lambda([Setter]()
			{
				if (GEditor)
				{
					if (const AActor* Selected = GEditor->GetSelectedActors()->GetTop<AActor>())
					{
						Setter(Selected->GetClass());
					}
				}
				return FReply::Handled();
			})
		];
}

void SOpenScenarioModePanel::RebuildMappingRows()
{
	MappingBox->ClearChildren();
	FOpenScenarioEditorContext* Ctx = Context;
	if (!Ctx->GetAsset())
	{
		MappingBox->AddSlot().AutoHeight().Padding(2.f)[ SNew(STextBlock).Text(LOCTEXT("MapNoAsset", "Select a scenario to map actor classes.")) ];
		return;
	}

	MappingBox->AddSlot().AutoHeight().Padding(2.f)
	[
		SNew(STextBlock).Text(LOCTEXT("KindDefaults", "Defaults per entity kind")).Font(FAppStyle::GetFontStyle("BoldFont"))
	];
	const struct { const TCHAR* Label; EOSCEntityKind Kind; } Kinds[] = {
		{ TEXT("Vehicles"), EOSCEntityKind::Vehicle },
		{ TEXT("Pedestrians"), EOSCEntityKind::Pedestrian },
		{ TEXT("Objects"), EOSCEntityKind::MiscObject } };
	for (const auto& K : Kinds)
	{
		const EOSCEntityKind Kind = K.Kind;
		MappingBox->AddSlot().AutoHeight().Padding(2.f)
		[
			MakeClassRow(FText::FromString(K.Label),
				[Ctx, Kind]() { return Ctx->GetKindActorClass(Kind); },
				[Ctx, Kind](const UClass* C) { Ctx->SetKindActorClass(Kind, C); }, FString())
		];
	}

	MappingBox->AddSlot().AutoHeight().Padding(2.f, 8.f, 2.f, 2.f)
	[
		SNew(STextBlock).Text(LOCTEXT("EntityOverrides", "Per entity (overrides the defaults)")).Font(FAppStyle::GetFontStyle("BoldFont"))
	];
	for (const FOSCEntity& Entity : Ctx->GetWorking().Entities)
	{
		const FString Name = Entity.Name;
		MappingBox->AddSlot().AutoHeight().Padding(2.f)
		[
			MakeClassRow(FText::FromString(Name),
				[Ctx, Name]() { return Ctx->GetEntityActorClass(Name); },
				[Ctx, Name](const UClass* C) { Ctx->SetEntityActorClass(Name, C); }, Name)
		];
	}
	MappingBox->AddSlot().AutoHeight().Padding(2.f, 6.f, 2.f, 2.f)
	[
		SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("MappingHint",
			"Unmapped entities use the box actor. Actors placed with an OpenScenario Actor can override these mappings there. "
			"The actor origin is the OpenSCENARIO reference point of the entity."))
	];
}

#undef LOCTEXT_NAMESPACE
