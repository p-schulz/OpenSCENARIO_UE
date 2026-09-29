#include "Authoring/SOpenScenarioTransportBar.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "OpenScenarioTransportBar"

void SOpenScenarioTransportBar::Construct(const FArguments& InArgs, FOpenScenarioEditorContext& InContext)
{
	Context = &InContext;
	FOpenScenarioEditorContext* Ctx = Context;

	auto MakeButton = [](const FText& Label, const FText& Tooltip, TFunction<void()> OnClick, TFunction<bool()> IsEnabled)
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tooltip)
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); });
	};
	auto HasAsset = [Ctx]() { return Ctx->GetAsset() != nullptr; };

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Play", "Play"), LOCTEXT("PlayTip", "Start the scenario, or resume it when paused. Places an OpenScenario Actor if the level has none."),
					[Ctx]() { Ctx->PlaybackPlay(); },
					[Ctx]() { return Ctx->GetAsset() != nullptr && Ctx->GetPlaybackState() != EOpenScenarioPlaybackState::Playing; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Pause", "Pause"), LOCTEXT("PauseTip", "Pause the simulation"),
					[Ctx]() { Ctx->PlaybackPause(); },
					[Ctx]() { return Ctx->GetPlaybackState() == EOpenScenarioPlaybackState::Playing; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Step", "Step"), LOCTEXT("StepTip", "Advance one simulation step (starts the scenario paused if it is stopped)"),
					[Ctx]() { Ctx->PlaybackStep(); },
					[Ctx]() { return Ctx->GetAsset() != nullptr && Ctx->GetPlaybackState() != EOpenScenarioPlaybackState::Playing && Ctx->GetPlaybackState() != EOpenScenarioPlaybackState::Finished; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeButton(LOCTEXT("Stop", "Stop"), LOCTEXT("StopTip", "Stop the scenario and remove the entity actors"),
					[Ctx]() { Ctx->PlaybackStop(); },
					[Ctx]() { return Ctx->GetPlaybackState() != EOpenScenarioPlaybackState::Stopped; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				MakeButton(LOCTEXT("Restart", "Restart"), LOCTEXT("RestartTip", "Stop and start again from t = 0"),
					[Ctx]() { Ctx->PlaybackRestart(); }, HasAsset)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("SpeedLabel", "Speed"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(70.f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.f)
					.MaxValue(20.f)
					.MinSliderValue(0.f)
					.MaxSliderValue(4.f)
					.Delta(0.1f)
					.Value_Lambda([Ctx]() { return Ctx->GetPlaybackTimeScale(); })
					.OnValueChanged_Lambda([Ctx](float V) { Ctx->SetPlaybackTimeScale(V); })
					.IsEnabled_Lambda([Ctx]() { return Ctx->HasPlaybackActor(); })
					.ToolTipText(LOCTEXT("SpeedTip", "Playback speed (time scale of the OpenScenario Actor)"))
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(this, &SOpenScenarioTransportBar::GetStatusText)
		]
	];
}

FText SOpenScenarioTransportBar::GetStatusText() const
{
	if (!Context->GetAsset())
	{
		return FText();
	}
	if (!Context->HasPlaybackActor())
	{
		return LOCTEXT("NoActor", "Stopped. Play places an OpenScenario Actor in the level.");
	}
	const TCHAR* State = TEXT("Stopped");
	switch (Context->GetPlaybackState())
	{
	case EOpenScenarioPlaybackState::Playing: State = TEXT("Playing"); break;
	case EOpenScenarioPlaybackState::Paused: State = TEXT("Paused"); break;
	case EOpenScenarioPlaybackState::Finished: State = TEXT("Finished"); break;
	default: break;
	}
	return FText::FromString(FString::Printf(TEXT("%s   t = %.2f s"), State, Context->GetPlaybackTime()));
}

#undef LOCTEXT_NAMESPACE
