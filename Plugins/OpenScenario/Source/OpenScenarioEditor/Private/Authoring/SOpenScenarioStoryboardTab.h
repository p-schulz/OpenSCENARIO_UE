#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/STreeView.h"
#include "Scenario/OpenScenarioModelEdit.h"

class FOpenScenarioEditorContext;
class FMenuBuilder;
class IStructureDetailsView;
class FStructOnScope;
struct FPropertyChangedEvent;

/**
 * Dockable storyboard overview: a tree of entities, init actions, stories, acts, maneuver groups,
 * maneuvers, events, actions and triggers/conditions, with add/remove/duplicate/move commands and a
 * details panel that edits the selected element.
 */
class SOpenScenarioStoryboardTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenScenarioStoryboardTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext);
	virtual ~SOpenScenarioStoryboardTab() override;

private:
	using FNodePtr = TSharedPtr<FOSCNodeRef>;

	FNodePtr GetNode(const FOSCNodeRef& Ref);
	void RebuildRoots();
	void ExpandAncestors(const FOSCNodeRef& Ref);

	// Tree callbacks
	TSharedRef<ITableRow> OnGenerateRow(FNodePtr Item, const TSharedRef<STableViewBase>& Owner);
	void OnGetChildren(FNodePtr Item, TArray<FNodePtr>& OutChildren);
	void OnTreeSelectionChanged(FNodePtr Item, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> OnContextMenuOpening();

	// Commands
	TSharedRef<SWidget> BuildAddMenu();
	void FillEditMenu(FMenuBuilder& Menu, bool bIncludeAdd);
	void DoAdd(EOSCNodeType ChildType);
	void DoRemove();
	void DoDuplicate();
	void DoMove(int32 Delta);
	bool HasSelection() const;

	// State sync
	void HandleAssetChanged();
	void HandleStructureChanged();
	void HandleValueChanged();
	void UpdateDetails();
	void OnDetailsPropertyChanged(const FPropertyChangedEvent& Event);

	FText GetHeaderText() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	FOpenScenarioEditorContext* Context = nullptr;
	TArray<FNodePtr> RootItems;
	TMap<FString, FNodePtr> NodeCache;
	TSharedPtr<STreeView<FNodePtr>> Tree;
	TSharedPtr<IStructureDetailsView> Details;
	FDelegateHandle AssetHandle;
	FDelegateHandle StructureHandle;
	FDelegateHandle ValueHandle;
	bool bUpdatingSelection = false;
};
