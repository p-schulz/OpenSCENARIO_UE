#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FOpenScenarioEditorContext;
class IDetailsView;
class SVerticalBox;
struct FAssetData;

/** Panel of the OpenSCENARIO editor mode. */
class SOpenScenarioModePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SOpenScenarioModePanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext);
	virtual ~SOpenScenarioModePanel() override;

private:
	void OnAssetPicked(const FAssetData& AssetData);
	FString GetAssetPath() const;
	FReply OnImport(bool bRoad);
	FText GetInfoText() const;
	void RebuildMappingRows();
	TSharedRef<SWidget> MakeClassRow(const FText& Label, TFunction<const UClass*()> Getter, TFunction<void(const UClass*)> Setter, const FString& EntityName);

	FOpenScenarioEditorContext* Context = nullptr;
	TSharedPtr<SVerticalBox> MappingBox;
	TSharedPtr<IDetailsView> SettingsView;
	FDelegateHandle AssetHandle;
	FDelegateHandle StructureHandle;
	FDelegateHandle MappingHandle;
};
