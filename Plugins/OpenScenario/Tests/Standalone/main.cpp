// Standalone regression driver: runs a scenario headless against the mock UE layer and prints entity states.
// Usage: osc_test <scenario.xosc> [expected_min_time] [expected_max_time]
#include "CoreMinimal.h"
#include "OpenScenarioModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Simulation/OpenScenarioRunner.h"
#include "Simulation/OpenScenarioEntityActor.h"

FVector FVector::OneVector(1, 1, 1);
FTransform FTransform::Identity;
void AOpenScenarioEntityActor::ConfigureFromEntity(const FOSCEntity&) {}

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: osc_test <scenario.xosc> [min_end_time] [max_end_time]\n"); return 64; }
	const double MinEnd = argc > 2 ? std::atof(argv[2]) : 0.0;
	const double MaxEnd = argc > 3 ? std::atof(argv[3]) : 1e9;

	FString Xml;
	if (!FFileHelper::LoadFileToString(Xml, argv[1])) { std::printf("cannot read %s\n", argv[1]); return 65; }
	UOpenScenarioAsset* Asset = new UOpenScenarioAsset();
	Asset->SetSource(Xml, FPaths::ConvertRelativePathToFull(FString(argv[1])));
	std::printf("valid=%d entities=%d parser messages=%d\n", (int)Asset->IsScenarioValid(), Asset->EntityNames.Num(), Asset->ParseMessages.Num());
	for (const FString& M : Asset->ParseMessages) { std::printf("  parser: %s\n", *M); }
	if (!Asset->IsScenarioValid()) { return 1; }

	UOpenScenarioRunner* R = new UOpenScenarioRunner();
	R->bSpawnActors = false;
	// Kinematic unless "--simple" is passed as the 4th argument: the example expectations below assume exact scripted speeds.
	R->Dynamics.Mode = (argc > 4 && std::string(argv[4]) == "--simple") ? EOpenScenarioDynamicsMode::Simple : EOpenScenarioDynamicsMode::Kinematic;
	bool bFinished = false;
	R->OnFinished.F.push_back([&]() { bFinished = true; });
	if (!R->Initialize(nullptr, Asset)) { std::printf("initialize failed\n"); return 1; }

	auto Print = [&]() {
		std::printf("t=%6.2f", R->GetSimulationTime());
		for (const FOSCEntityState& E : R->GetEntities()) {
			std::printf(" | %s (%.2f,%.2f) h=%.2f v=%.2f", *E.Name, E.X, E.Y, E.Heading, E.Speed);
			if (E.bOnRoad) { std::printf(" road=%s s=%.1f lane=%d", *E.RoadId, E.S, E.LaneId); }
		}
		std::printf("\n");
	};
	Print();
	for (int i = 1; i <= 60 * 50 && R->IsRunning(); ++i) { R->Step(0.02); if (i % 50 == 0) { Print(); } }
	Print();

	const double T = R->GetSimulationTime();
	std::printf("finished=%d at t=%.2f (expected %.1f..%.1f)\n", (int)bFinished, T, MinEnd, MaxEnd);
	return (bFinished && T >= MinEnd && T <= MaxEnd) ? 0 : 2;
}
