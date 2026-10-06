// OpenDRIVE traffic lights and signs: scripted and automatic light cycles, stop signs, speed limit signs.
#include "CoreMinimal.h"
#include "OpenScenarioModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Simulation/OpenScenarioRunner.h"
#include "Simulation/OpenScenarioEntityActor.h"

FVector FVector::OneVector(1, 1, 1);
FTransform FTransform::Identity;
void AOpenScenarioEntityActor::ConfigureFromEntity(const FOSCEntity&) {}

static int Failures = 0;
#define CHECK_MSG(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: %s -> ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); ++Failures; } } while (0)

static UOpenScenarioRunner* Start(const FString& Path)
{
	FString Xml;
	FFileHelper::LoadFileToString(Xml, *Path);
	UOpenScenarioAsset* Asset = new UOpenScenarioAsset();
	Asset->SetSource(Xml, Path);
	UOpenScenarioRunner* R = new UOpenScenarioRunner();
	R->bSpawnActors = false;
	R->Dynamics.Mode = EOpenScenarioDynamicsMode::Simple;
	if (!R->Initialize(nullptr, Asset)) { std::printf("cannot start %s\n", *Path); std::exit(2); }
	return R;
}

int main(int argc, char** argv)
{
	const FString Dir = argc > 1 ? FString(argv[1]) : FString(".");
	constexpr double Dt = 0.02;
	constexpr double Front = 1.4 + 2.25; // reference point to front bumper

	// ---------------------------------------------------------------- scripted light
	{
		std::printf("== SignalsTrafficLight: red until t = 14 s, set by the scenario\n");
		UOpenScenarioRunner* R = Start(Dir / "SignalsTrafficLight.xosc");
		EOpenScenarioSignalState State;
		CHECK_MSG(R->GetSignalState("10", State) && State == EOpenScenarioSignalState::Red, "light starts red");
		int Changes = 0;
		R->OnSignalChanged.F.push_back([&](const FString&, EOpenScenarioSignalState) { ++Changes; });
		double MaxFront = 0, TimeStopped = -1, ResumeTime = -1;
		for (int i = 1; i <= 2000 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const FOSCEntityState* E = R->GetEntityState("Ego");
			const double T = R->GetSimulationTime();
			if (T < 14.0)
			{
				MaxFront = FMath::Max(MaxFront, E->S + Front);
				if (E->Speed < 0.05 && TimeStopped < 0) { TimeStopped = T; }
			}
			else if (ResumeTime < 0 && E->Speed > 1.0) { ResumeTime = T; }
			if (i % 100 == 0) { std::printf("   t=%5.1f s=%6.1f v=%5.2f\n", T, E->S, E->Speed); }
		}
		const FOSCEntityState* E = R->GetEntityState("Ego");
		std::printf("   front before green max %.2f, stopped at t=%.1f, resumed at t=%.1f, final s=%.1f, changes %d\n", MaxFront, TimeStopped, ResumeTime, E->S, Changes);
		CHECK_MSG(MaxFront <= 100.0, "stays behind the stop line while red (front at %.2f)", MaxFront);
		CHECK_MSG(MaxFront > 94.0, "drives up to the line (front at %.2f)", MaxFront);
		CHECK_MSG(TimeStopped > 0 && TimeStopped < 14.0, "comes to a halt before the light turns green");
		CHECK_MSG(ResumeTime >= 14.0 && ResumeTime < 17.0, "moves off after green (t=%.1f)", ResumeTime);
		CHECK_MSG(E->S > 150.0, "passes the light after green (s=%.1f)", E->S);
		CHECK_MSG(Changes == 1, "one state change broadcast (%d)", Changes);
	}

	// ---------------------------------------------------------------- automatic cycle
	{
		std::printf("== SignalsAutoCycle: green 20 s, yellow 3 s, red 10 s\n");
		UOpenScenarioRunner* R = Start(Dir / "SignalsAutoCycle.xosc");
		EOpenScenarioSignalState State;
		double EgoPassTime = -1, LateMaxFrontWhileRed = 0, LatePassTime = -1;
		bool bSawYellow = false, bSawRed = false;
		for (int i = 1; i <= 3500 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const double T = R->GetSimulationTime();
			R->GetSignalState("10", State);
			bSawYellow |= State == EOpenScenarioSignalState::Yellow;
			bSawRed |= State == EOpenScenarioSignalState::Red;
			const FOSCEntityState* Ego = R->GetEntityState("Ego");
			const FOSCEntityState* Late = R->GetEntityState("Late");
			if (EgoPassTime < 0 && Ego->S + Front > 100.0) { EgoPassTime = T; }
			if (Late->bOnRoad && Late->RoadId == "10")
			{
				if (T < 33.0) { LateMaxFrontWhileRed = FMath::Max(LateMaxFrontWhileRed, Late->S + Front); }
				if (LatePassTime < 0 && Late->S + Front > 100.0) { LatePassTime = T; }
			}
			if (i % 150 == 0) { std::printf("   t=%5.1f light=%d ego s=%6.1f late s=%6.1f v=%5.2f\n", T, (int)State, Ego->S, Late->S, Late->Speed); }
		}
		std::printf("   Ego crosses at t=%.1f, Late max front before green %.2f, crosses at t=%.1f\n", EgoPassTime, LateMaxFrontWhileRed, LatePassTime);
		CHECK_MSG(bSawYellow && bSawRed, "light runs through yellow and red");
		CHECK_MSG(EgoPassTime > 0 && EgoPassTime < 20.0, "Ego passes during green (t=%.1f)", EgoPassTime);
		CHECK_MSG(LateMaxFrontWhileRed <= 100.0 && LateMaxFrontWhileRed > 90.0, "Late waits at the line (front %.2f)", LateMaxFrontWhileRed);
		CHECK_MSG(LatePassTime >= 33.0, "Late crosses only after the light is green again (t=%.1f)", LatePassTime);
	}

	// ---------------------------------------------------------------- scenario signal controller
	{
		std::printf("== SignalsController: controller phases, TrafficSignalCondition, controller action\n");
		UOpenScenarioRunner* R = Start(Dir / "SignalsController.xosc");
		EOpenScenarioSignalState State;
		double GreenAt = -1, RedAgain = -1, LateAppeared = -1, EgoMaxBefore = 0;
		bool bStartedRed = R->GetSignalState("10", State) && State == EOpenScenarioSignalState::Red;
		for (int i = 1; i <= 2400 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const double T = R->GetSimulationTime();
			R->GetSignalState("10", State);
			if (GreenAt < 0 && State == EOpenScenarioSignalState::Green) { GreenAt = T; }
			if (GreenAt > 0 && RedAgain < 0 && State == EOpenScenarioSignalState::Red) { RedAgain = T; }
			const FOSCEntityState* Ego = R->GetEntityState("Ego");
			if (GreenAt < 0) { EgoMaxBefore = FMath::Max(EgoMaxBefore, Ego->S + Front); }
			const FOSCEntityState* Late = R->GetEntityState("Late");
			if (LateAppeared < 0 && Late->bOnRoad && Late->RoadId == "10") { LateAppeared = T; }
		}
		std::printf("   red at start %d, green at t=%.2f, red again at t=%.2f, Late appears at t=%.2f, Ego front before green %.2f\n", (int)bStartedRed, GreenAt, RedAgain, LateAppeared, EgoMaxBefore);
		CHECK_MSG(bStartedRed, "controller sets its first phase at start");
		CHECK_MSG(GreenAt >= 11.9 && GreenAt < 12.2, "second phase after 12 s (t=%.2f)", GreenAt);
		CHECK_MSG(RedAgain >= 29.9 && RedAgain < 30.3, "controller action restores the stop phase (t=%.2f)", RedAgain);
		CHECK_MSG(LateAppeared >= GreenAt && LateAppeared < GreenAt + 0.5, "TrafficSignalCondition triggers on green (t=%.2f)", LateAppeared);
		CHECK_MSG(EgoMaxBefore <= 100.0, "Ego waits for the controller's green (front %.2f)", EgoMaxBefore);
	}

	// ---------------------------------------------------------------- stop sign and speed limit
	{
		std::printf("== SignalsStopAndSpeed: stop sign, 30 km/h sign, end of limit\n");
		UOpenScenarioRunner* R = Start(Dir / "SignalsStopAndSpeed.xosc");
		double StopStart = -1, StopEnd = -1, MaxFrontBeforeStop = 0, MaxLimited = 0, MaxAfter = 0;
		bool bAfterEnd = false;
		for (int i = 1; i <= 2250 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const double T = R->GetSimulationTime();
			const FOSCEntityState* Ego = R->GetEntityState("Ego");
			if (Ego->RoadId == "20" && Ego->S + Front < 100.0 + 0.001 && StopEnd < 0)
			{
				MaxFrontBeforeStop = FMath::Max(MaxFrontBeforeStop, Ego->S + Front);
				if (Ego->Speed < 0.05 && StopStart < 0) { StopStart = T; }
			}
			if (StopStart > 0 && StopEnd < 0 && Ego->Speed > 0.3) { StopEnd = T; }
			const FOSCEntityState* D = R->GetEntityState("Driver");
			if (D->RoadId == "30" && D->S > 30.0) { MaxLimited = FMath::Max(MaxLimited, D->Speed); }
			if (D->RoadId == "31")
			{
				if (D->S < 20.0) { MaxLimited = FMath::Max(MaxLimited, D->Speed); }
				else { bAfterEnd = true; MaxAfter = FMath::Max(MaxAfter, D->Speed); }
			}
			if (i % 150 == 0) { std::printf("   t=%5.1f ego %s s=%6.1f v=%5.2f | driver %s s=%6.1f v=%5.2f\n", T, *Ego->RoadId, Ego->S, Ego->Speed, *D->RoadId, D->S, D->Speed); }
		}
		const FOSCEntityState* Ego = R->GetEntityState("Ego");
		std::printf("   stop at t=%.1f..%.1f (front %.2f), limited max %.2f, after end max %.2f\n", StopStart, StopEnd, MaxFrontBeforeStop, MaxLimited, MaxAfter);
		CHECK_MSG(StopStart > 0, "Ego comes to a full stop at the stop sign");
		CHECK_MSG(MaxFrontBeforeStop <= 100.0 && MaxFrontBeforeStop > 94.0, "stops at the line (front %.2f)", MaxFrontBeforeStop);
		CHECK_MSG(StopEnd - StopStart >= 1.9, "waits at least 2 s (%.2f)", StopEnd - StopStart);
		CHECK_MSG(Ego->S > 150.0 || Ego->bAtDeadEnd, "continues after the stop (s=%.1f)", Ego->S);
		CHECK_MSG(MaxLimited <= 30.0 / 3.6 + 0.2, "30 km/h limit holds between the signs (max %.2f)", MaxLimited);
		CHECK_MSG(bAfterEnd && MaxAfter > 30.0 / 3.6 + 2.0, "limit is lifted after the end sign (max %.2f)", MaxAfter);
	}

	std::printf(Failures ? "\n%d signal check(s) FAILED\n" : "signal tests passed\n", Failures);
	return Failures ? 1 : 0;
}
