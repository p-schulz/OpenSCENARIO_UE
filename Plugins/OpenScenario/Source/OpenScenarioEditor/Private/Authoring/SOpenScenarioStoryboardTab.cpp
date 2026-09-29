#include "Authoring/SOpenScenarioStoryboardTab.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "UObject/StructOnScope.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "OpenScenarioStoryboardTab"

namespace
{
	FLinearColor NodeColor(EOSCNodeType Type)
	{
		switch (Type)
		{
		case EOSCNodeType::Storyboard: return FLinearColor(0.9f, 0.9f, 0.9f);
		case EOSCNodeType::Entities:
		case EOSCNodeType::Entity: return FLinearColor(1.0f, 0.35f, 0.35f);
		case EOSCNodeType::Init:
		case EOSCNodeType::InitGroup:
		case EOSCNodeType::InitAction: return FLinearColor(0.7f, 0.5f, 0.9f);
		case EOSCNodeType::Story: return FLinearColor(0.2f, 0.6f, 1.0f);
		case EOSCNodeType::Act: return FLinearColor(0.2f, 0.8f, 0.9f);
		case EOSCNodeType::ManeuverGroup:
		case EOSCNodeType::Maneuver: return FLinearColor(0.3f, 0.85f, 0.5f);
		case EOSCNodeType::Event: return FLinearColor(0.95f, 0.8f, 0.2f);
		case EOSCNodeType::Action: return FLinearColor(1.0f, 0.55f, 0.15f);
		case EOSCNodeType::Trigger:
		case EOSCNodeType::ConditionGroup:
		case EOSCNodeType::Condition: return FLinearColor(0.9f, 0.4f, 0.75f);
		default: return FLinearColor::Gray;
		}
	}

	UScriptStruct* StructForNode(EOSCNodeType Type)
	{
		switch (Type)
		{
		case EOSCNodeType::Storyboard: return FOSCScenario::StaticStruct();
		case EOSCNodeType::Entity: return FOSCEntity::StaticStruct();
		case EOSCNodeType::InitGroup: return FOSCInitActions::StaticStruct();
		case EOSCNodeType::InitAction:
		case EOSCNodeType::Action: return FOSCAction::StaticStruct();
		case EOSCNodeType::Story: return FOSCStory::StaticStruct();
		case EOSCNodeType::Act: return FOSCAct::StaticStruct();
		case EOSCNodeType::ManeuverGroup: return FOSCManeuverGroup::StaticStruct();
		case EOSCNodeType::Maneuver: return FOSCManeuver::StaticStruct();
		case EOSCNodeType::Event: return FOSCEvent::StaticStruct();
		case EOSCNodeType::Trigger: return FOSCTrigger::StaticStruct();
		case EOSCNodeType::ConditionGroup: return FOSCConditionGroup::StaticStruct();
		case EOSCNodeType::Condition: return FOSCCondition::StaticStruct();
		default: return nullptr;
		}
	}
}

void SOpenScenarioStoryboardTab::Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext)
{
	Context = &InContext;

	// Details view for the selected element's struct.
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	FStructureDetailsViewArgs StructArgs;
	Details = PropertyModule.CreateStructureDetailView(DetailsArgs, StructArgs, nullptr);
	// Children of the selected node are edited through the tree, not through nested array editors.
	Details->GetDetailsView()->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& Property)
	{
		static const TSet<FName> Hidden = {
			"Entities", "InitActions", "Stories", "StopTrigger", "StartTrigger", "Acts", "ManeuverGroups",
			"Maneuvers", "Events", "Actions", "Groups", "Conditions" };
		return !Hidden.Contains(Property.Property.GetFName());
	}));
	Details->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SOpenScenarioStoryboardTab::OnDetailsPropertyChanged);

	Tree = SNew(STreeView<FNodePtr>)
		.TreeItemsSource(&RootItems)
		.OnGenerateRow(this, &SOpenScenarioStoryboardTab::OnGenerateRow)
		.OnGetChildren(this, &SOpenScenarioStoryboardTab::OnGetChildren)
		.OnSelectionChanged(this, &SOpenScenarioStoryboardTab::OnTreeSelectionChanged)
		.OnContextMenuOpening(this, &SOpenScenarioStoryboardTab::OnContextMenuOpening)
		.SelectionMode(ESelectionMode::Single);

	auto MakeButton = [](const FText& Label, const FText& Tooltip, TFunction<FReply()> OnClick, TFunction<bool()> IsEnabled)
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClick]() { return OnClick(); })
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); });
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock).Text(this, &SOpenScenarioStoryboardTab::GetHeaderText)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SComboButton)
				.ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Add", "Add")) ]
				.ToolTipText(LOCTEXT("AddTip", "Add a child to the selected element"))
				.OnGetMenuContent(this, &SOpenScenarioStoryboardTab::BuildAddMenu)
				.IsEnabled_Lambda([this]() { return HasSelection(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Remove", "Remove"), LOCTEXT("RemoveTip", "Remove the selected element"),
					[this]() { DoRemove(); return FReply::Handled(); },
					[this]() { return HasSelection() && FOSCModelEdit::CanRemove(Context->GetSelection()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Duplicate", "Duplicate"), LOCTEXT("DuplicateTip", "Duplicate the selected element"),
					[this]() { DoDuplicate(); return FReply::Handled(); },
					[this]() { return HasSelection() && FOSCModelEdit::CanRemove(Context->GetSelection()) && Context->GetSelection().Type != EOSCNodeType::Trigger; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Up", "Up"), LOCTEXT("UpTip", "Move the selected element up"),
					[this]() { DoMove(-1); return FReply::Handled(); },
					[this]() { return HasSelection() && FOSCModelEdit::CanRemove(Context->GetSelection()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)
			[
				MakeButton(LOCTEXT("Down", "Down"), LOCTEXT("DownTip", "Move the selected element down"),
					[this]() { DoMove(1); return FReply::Handled(); },
					[this]() { return HasSelection() && FOSCModelEdit::CanRemove(Context->GetSelection()); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Apply", "Apply"), LOCTEXT("ApplyTip", "Write the edited storyboard back to the scenario asset"),
					[this]() { Context->Apply(); return FReply::Handled(); },
					[this]() { return Context->GetAsset() != nullptr && Context->IsDirty(); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeButton(LOCTEXT("Revert", "Revert"), LOCTEXT("RevertTip", "Discard unapplied edits"),
					[this]() { Context->Revert(); return FReply::Handled(); },
					[this]() { return Context->GetAsset() != nullptr && Context->IsDirty(); })
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4.f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.5f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))[ Tree.ToSharedRef() ]
			]
			+ SSplitter::Slot().Value(0.5f)
			[
				SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))[ Details->GetWidget().ToSharedRef() ]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock)
			.Text(this, &SOpenScenarioStoryboardTab::GetStatusText)
			.ColorAndOpacity(this, &SOpenScenarioStoryboardTab::GetStatusColor)
			.AutoWrapText(true)
		]
	];

	AssetHandle = Context->OnAssetChanged.AddSP(this, &SOpenScenarioStoryboardTab::HandleAssetChanged);
	StructureHandle = Context->OnStructureChanged.AddSP(this, &SOpenScenarioStoryboardTab::HandleStructureChanged);
	ValueHandle = Context->OnValueChanged.AddSP(this, &SOpenScenarioStoryboardTab::HandleValueChanged);

	RebuildRoots();
}

SOpenScenarioStoryboardTab::~SOpenScenarioStoryboardTab()
{
	if (Context)
	{
		Context->OnAssetChanged.Remove(AssetHandle);
		Context->OnStructureChanged.Remove(StructureHandle);
		Context->OnValueChanged.Remove(ValueHandle);
	}
}

// ------------------------------------------------------------------------------------------------
// Tree
// ------------------------------------------------------------------------------------------------

SOpenScenarioStoryboardTab::FNodePtr SOpenScenarioStoryboardTab::GetNode(const FOSCNodeRef& Ref)
{
	const FString Key = Ref.Key();
	if (FNodePtr* Found = NodeCache.Find(Key))
	{
		return *Found;
	}
	return NodeCache.Add(Key, MakeShared<FOSCNodeRef>(Ref));
}

void SOpenScenarioStoryboardTab::RebuildRoots()
{
	RootItems.Reset();
	if (Context->GetAsset())
	{
		FNodePtr Root = GetNode(FOSCNodeRef(EOSCNodeType::Storyboard));
		RootItems.Add(Root);
		Tree->RequestTreeRefresh();
		Tree->SetItemExpansion(Root, true);
	}
	else
	{
		Tree->RequestTreeRefresh();
	}
	UpdateDetails();
}

void SOpenScenarioStoryboardTab::ExpandAncestors(const FOSCNodeRef& Ref)
{
	FOSCNodeRef Current = Ref;
	FOSCNodeRef Parent;
	while (FOSCModelEdit::GetParent(Current, Parent))
	{
		Tree->SetItemExpansion(GetNode(Parent), true);
		Current = Parent;
	}
}

TSharedRef<ITableRow> SOpenScenarioStoryboardTab::OnGenerateRow(FNodePtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const FOSCNodeRef Ref = *Item;
	FOpenScenarioEditorContext* Ctx = Context;
	return SNew(STableRow<FNodePtr>, Owner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(6.f).HeightOverride(14.f)
				[
					SNew(SColorBlock).Color(NodeColor(Ref.Type))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text_Lambda([Ctx, Ref]()
				{
					return FText::FromString(FOSCModelEdit::Describe(Ctx->GetWorking(), Ref));
				})
			]
		];
}

void SOpenScenarioStoryboardTab::OnGetChildren(FNodePtr Item, TArray<FNodePtr>& OutChildren)
{
	TArray<FOSCNodeRef> Kids;
	FOSCModelEdit::GetChildren(Context->GetWorking(), *Item, Kids);
	for (const FOSCNodeRef& Kid : Kids)
	{
		OutChildren.Add(GetNode(Kid));
	}
}

void SOpenScenarioStoryboardTab::OnTreeSelectionChanged(FNodePtr Item, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingSelection)
	{
		return;
	}
	Context->SetSelection(Item.IsValid() ? *Item : FOSCNodeRef());
	UpdateDetails();
}

bool SOpenScenarioStoryboardTab::HasSelection() const
{
	return Context && Context->GetAsset() && Context->GetSelection().IsValid();
}

TSharedPtr<SWidget> SOpenScenarioStoryboardTab::OnContextMenuOpening()
{
	if (!HasSelection())
	{
		return nullptr;
	}
	FMenuBuilder Menu(true, nullptr);
	FillEditMenu(Menu, true);
	return Menu.MakeWidget();
}

// ------------------------------------------------------------------------------------------------
// Commands
// ------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SOpenScenarioStoryboardTab::BuildAddMenu()
{
	FMenuBuilder Menu(true, nullptr);
	TArray<EOSCNodeType> Types;
	FOSCModelEdit::GetAddableChildren(Context->GetSelection(), Types);
	if (Types.Num() == 0)
	{
		Menu.AddMenuEntry(LOCTEXT("NothingToAdd", "Nothing can be added to this element"), FText(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	for (const EOSCNodeType Type : Types)
	{
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("AddEntry", "Add {0}"), FText::FromString(FOSCModelEdit::TypeName(Type))),
			FText(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoAdd, Type)));
	}
	return Menu.MakeWidget();
}

void SOpenScenarioStoryboardTab::FillEditMenu(FMenuBuilder& Menu, bool bIncludeAdd)
{
	if (bIncludeAdd)
	{
		TArray<EOSCNodeType> Types;
		FOSCModelEdit::GetAddableChildren(Context->GetSelection(), Types);
		for (const EOSCNodeType Type : Types)
		{
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("AddEntry", "Add {0}"), FText::FromString(FOSCModelEdit::TypeName(Type))),
				FText(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoAdd, Type)));
		}
		Menu.AddMenuSeparator();
	}
	const FOSCNodeRef Sel = Context->GetSelection();
	const bool bEditable = FOSCModelEdit::CanRemove(Sel);
	const bool bNotTrigger = Sel.Type != EOSCNodeType::Trigger;
	Menu.AddMenuEntry(LOCTEXT("CtxDuplicate", "Duplicate"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoDuplicate), FCanExecuteAction::CreateLambda([=]() { return bEditable && bNotTrigger; })));
	Menu.AddMenuEntry(LOCTEXT("CtxRemove", "Remove"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoRemove), FCanExecuteAction::CreateLambda([=]() { return bEditable; })));
	Menu.AddMenuEntry(LOCTEXT("CtxUp", "Move Up"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoMove, -1), FCanExecuteAction::CreateLambda([=]() { return bEditable && bNotTrigger; })));
	Menu.AddMenuEntry(LOCTEXT("CtxDown", "Move Down"), FText(), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SOpenScenarioStoryboardTab::DoMove, 1), FCanExecuteAction::CreateLambda([=]() { return bEditable && bNotTrigger; })));
}

void SOpenScenarioStoryboardTab::DoAdd(EOSCNodeType ChildType)
{
	if (!HasSelection())
	{
		return;
	}
	Details->SetStructureData(nullptr);
	FOSCNodeRef NewNode;
	if (FOSCModelEdit::AddChild(Context->GetWorking(), Context->GetSelection(), ChildType, NewNode))
	{
		Context->NotifyStructureChanged();
		ExpandAncestors(NewNode);
		bUpdatingSelection = true;
		Tree->SetSelection(GetNode(NewNode));
		Tree->RequestScrollIntoView(GetNode(NewNode));
		bUpdatingSelection = false;
		Context->SetSelection(NewNode);
	}
	UpdateDetails();
}

void SOpenScenarioStoryboardTab::DoRemove()
{
	if (!HasSelection())
	{
		return;
	}
	Details->SetStructureData(nullptr);
	FOSCNodeRef Parent;
	const FOSCNodeRef Sel = Context->GetSelection();
	const bool bHasParent = FOSCModelEdit::GetParent(Sel, Parent);
	if (FOSCModelEdit::Remove(Context->GetWorking(), Sel))
	{
		Context->NotifyStructureChanged();
		bUpdatingSelection = true;
		Tree->ClearSelection();
		if (bHasParent)
		{
			Tree->SetSelection(GetNode(Parent));
		}
		bUpdatingSelection = false;
		Context->SetSelection(bHasParent ? Parent : FOSCNodeRef());
	}
	UpdateDetails();
}

void SOpenScenarioStoryboardTab::DoDuplicate()
{
	if (!HasSelection())
	{
		return;
	}
	Details->SetStructureData(nullptr);
	FOSCNodeRef NewNode;
	if (FOSCModelEdit::Duplicate(Context->GetWorking(), Context->GetSelection(), NewNode))
	{
		Context->NotifyStructureChanged();
		bUpdatingSelection = true;
		Tree->SetSelection(GetNode(NewNode));
		bUpdatingSelection = false;
		Context->SetSelection(NewNode);
	}
	UpdateDetails();
}

void SOpenScenarioStoryboardTab::DoMove(int32 Delta)
{
	if (!HasSelection())
	{
		return;
	}
	Details->SetStructureData(nullptr);
	FOSCNodeRef NewNode;
	if (FOSCModelEdit::Move(Context->GetWorking(), Context->GetSelection(), Delta, NewNode))
	{
		Context->NotifyStructureChanged();
		bUpdatingSelection = true;
		Tree->SetSelection(GetNode(NewNode));
		bUpdatingSelection = false;
		Context->SetSelection(NewNode);
	}
	UpdateDetails();
}

// ------------------------------------------------------------------------------------------------
// State sync
// ------------------------------------------------------------------------------------------------

void SOpenScenarioStoryboardTab::HandleAssetChanged()
{
	NodeCache.Reset();
	RebuildRoots();
}

void SOpenScenarioStoryboardTab::HandleStructureChanged()
{
	Tree->RequestTreeRefresh();
	// Drop a selection that no longer resolves (e.g. after a revert or reparse).
	const FOSCNodeRef Sel = Context->GetSelection();
	if (Sel.IsValid() && !FOSCModelEdit::GetData(Context->GetWorking(), Sel)
		&& Sel.Type != EOSCNodeType::Entities && Sel.Type != EOSCNodeType::Init)
	{
		bUpdatingSelection = true;
		Tree->ClearSelection();
		bUpdatingSelection = false;
		Context->SetSelection(FOSCNodeRef());
	}
	UpdateDetails();
}

void SOpenScenarioStoryboardTab::HandleValueChanged()
{
	// Row texts are evaluated lazily; nothing else to do.
}

void SOpenScenarioStoryboardTab::UpdateDetails()
{
	Details->SetStructureData(nullptr);
	const FOSCNodeRef Sel = Context->GetSelection();
	if (!Sel.IsValid() || !Context->GetAsset())
	{
		return;
	}
	UScriptStruct* Struct = StructForNode(Sel.Type);
	void* Data = FOSCModelEdit::GetData(Context->GetWorking(), Sel);
	if (Struct && Data)
	{
		Details->SetStructureData(MakeShared<FStructOnScope>(Struct, static_cast<uint8*>(Data)));
	}
}

void SOpenScenarioStoryboardTab::OnDetailsPropertyChanged(const FPropertyChangedEvent& Event)
{
	Context->NotifyValueChanged();
}

FText SOpenScenarioStoryboardTab::GetHeaderText() const
{
	const UOpenScenarioAsset* Asset = Context->GetAsset();
	if (!Asset)
	{
		return LOCTEXT("NoAsset", "No scenario selected. Pick or create one in the OpenSCENARIO editor mode.");
	}
	return FText::Format(LOCTEXT("Header", "{0}{1}"), FText::FromString(Asset->GetName()), Context->IsDirty() ? LOCTEXT("Dirty", "  * unapplied changes") : FText());
}

FText SOpenScenarioStoryboardTab::GetStatusText() const
{
	if (!Context->GetAsset())
	{
		return FText();
	}
	const TArray<FString>& Issues = Context->GetIssues();
	if (Issues.Num() == 0)
	{
		return LOCTEXT("NoIssues", "No problems found.");
	}
	return FText::Format(LOCTEXT("Issues", "{0} problem(s). First: {1}"), FText::AsNumber(Issues.Num()), FText::FromString(Issues[0]));
}

FSlateColor SOpenScenarioStoryboardTab::GetStatusColor() const
{
	return Context->GetIssues().Num() > 0 ? FSlateColor(FLinearColor(1.f, 0.7f, 0.2f)) : FSlateColor::UseSubduedForeground();
}

#undef LOCTEXT_NAMESPACE
