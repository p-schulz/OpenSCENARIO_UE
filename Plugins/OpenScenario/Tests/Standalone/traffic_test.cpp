// TrafficSwarmAction / TrafficSourceAction / TrafficSinkAction against the mock UE layer.
#include "CoreMinimal.h"
#include "OpenScenarioModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Simulation/OpenScenarioRunner.h"
#include "Simulation/OpenScenarioEntityActor.h"
#include <set>

FVector FVector::OneVector(1, 1, 1);
FTransform FTransform::Identity;
void AOpenScenarioEntityActor::ConfigureFromEntity(const FOSCEntity&) {}

static int Failures = 0;
#define CHECK_MSG(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: %s -> ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); ++Failures; } } while (0)

static UOpenScenarioRunner* Start(const FString& Path, int32 Seed)
{
	FString Xml;
	FFileHelper::LoadFileToString(Xml, *Path);
	UOpenScenarioAsset* Asset = new UOpenScenarioAsset();
	Asset->SetSource(Xml, Path);
	UOpenScenarioRunner* R = new UOpenScenarioRunner();
	R->bSpawnActors = false;
	R->Dynamics.Mode = EOpenScenarioDynamicsMode::Simple;
	R->TrafficSettings.RandomSeed = Seed;
	if (!R->Initialize(nullptr, Asset)) { std::printf("cannot start %s\n", *Path); std::exit(2); }
	return R;
}

struct FPopulationResult
{
	double Checksum = 0.0;
	int32 Distinct = 0, MaxActive = 0, MinActiveAfterFill = 1000, PedSeen = 0, VehSeen = 0;
	double MinCenterDistance = 1e9, MaxEllipse = 0.0, MaxPedSpeed = 0.0;
	bool bEgoStuck = false;
};

static FPopulationResult RunPopulated(const FString& Path, int32 Seed)
{
	UOpenScenarioRunner* R = Start(Path, Seed);
	FPopulationResult Out;
	std::set<std::string> Names;
	for (int i = 1; i <= 90 * 50; ++i)
	{
		R->Step(0.02);
		const double T = R->GetSimulationTime();
		const FOSCEntityState* Ego = R->GetEntityState("Ego");
		int32 Active = 0;
		for (const FOSCEntityState& E : R->GetEntities())
		{
			if (!E.bActive || E.TrafficGenerator == INDEX_NONE) { continue; }
			++Active;
			if (Names.insert(E.Name.S).second)
			{
				(E.Def.Kind == EOSCEntityKind::Pedestrian ? Out.PedSeen : Out.VehSeen)++;
			}
			// inside the swarm ellipse (150 x 100 m) of the Ego, with the despawn hysteresis
			const double Dx = E.X - Ego->X, Dy = E.Y - Ego->Y;
			const double U = Dx * FMath::Cos(Ego->Heading) + Dy * FMath::Sin(Ego->Heading);
			const double V = -Dx * FMath::Sin(Ego->Heading) + Dy * FMath::Cos(Ego->Heading);
			Out.MaxEllipse = FMath::Max(Out.MaxEllipse, FMath::Sqrt(FMath::Square(U / 150.0) + FMath::Square(V / 100.0)));
			if (E.Def.Kind == EOSCEntityKind::Pedestrian) { Out.MaxPedSpeed = FMath::Max(Out.MaxPedSpeed, E.Speed); }
			if (T > 3.0 && E.Def.Kind == EOSCEntityKind::Vehicle)
			{
				// Compare with every other vehicle (traffic and Ego)
				for (const FOSCEntityState& O : R->GetEntities())
				{
					if (&O == &E || !O.bActive || O.Def.Kind != EOSCEntityKind::Vehicle) { continue; }
					Out.MinCenterDistance = FMath::Min(Out.MinCenterDistance, FMath::Sqrt(FMath::Square(E.X - O.X) + FMath::Square(E.Y - O.Y)));
				}
			}
		}
		Out.MaxActive = FMath::Max(Out.MaxActive, Active);
		if (T > 2.0) { Out.MinActiveAfterFill = FMath::Min(Out.MinActiveAfterFill, Active); }
		if (i == 50) { std::printf("   t=1s: %d traffic actors\n", Active); CHECK_MSG(Active == 12, "initial fill creates all %d actors at once (%d)", 12, Active); }
		if (i % 1000 == 0) { std::printf("   t=%5.1f active=%d distinct seen=%d ego v=%.1f road %s\n", T, Active, (int)Names.size(), Ego->Speed, *Ego->RoadId); }
		if (T > 10.0 && Ego->Speed < 0.5) { Out.bEgoStuck = true; }
	}
	Out.Distinct = (int32)Names.size();
	for (const FOSCEntityState& E : R->GetEntities()) { if (E.bActive) { Out.Checksum += E.X * 1.3 + E.Y * 0.7; } }
	return Out;
}

int main(int argc, char** argv)
{
	const FString Dir = argc > 1 ? FString(argv[1]) : FString(".");

	std::printf("== PopulatedWorld: swarm of 12 (cars, trucks, pedestrians) around the Ego, 90 s, no stop trigger\n");
	const FPopulationResult A = RunPopulated(Dir / "PopulatedWorld.xosc", 7);
	std::printf("   max active %d, min active %d, distinct %d (vehicles %d, pedestrians %d), min vehicle distance %.1f m, max ellipse radius %.2f, ped speed max %.2f\n",
		A.MaxActive, A.MinActiveAfterFill, A.Distinct, A.VehSeen, A.PedSeen, A.MinCenterDistance, A.MaxEllipse, A.MaxPedSpeed);
	CHECK_MSG(A.MaxActive == 12, "never more than n actors (%d)", A.MaxActive);
	CHECK_MSG(A.MinActiveAfterFill >= 9, "swarm is refilled after actors leave the ellipse (min %d)", A.MinActiveAfterFill);
	CHECK_MSG(A.Distinct > 20, "actors are despawned and respawned while the Ego moves (%d distinct)", A.Distinct);
	CHECK_MSG(A.VehSeen > 0 && A.PedSeen > 0, "both vehicles and pedestrians appear");
	CHECK_MSG(A.MinCenterDistance > 3.0, "vehicles do not overlap (%.2f)", A.MinCenterDistance);
	CHECK_MSG(A.MaxEllipse <= 1.2, "actors stay inside the swarm ellipse (%.2f)", A.MaxEllipse);
	CHECK_MSG(A.MaxPedSpeed < 2.0, "pedestrians walk (%.2f)", A.MaxPedSpeed);
	CHECK_MSG(!A.bEgoStuck, "the Ego keeps driving");

	std::printf("== determinism: same seed, same population\n");
	const FPopulationResult A2 = RunPopulated(Dir / "PopulatedWorld.xosc", 7);
	CHECK_MSG(std::fabs(A.Checksum - A2.Checksum) < 1e-6 && A.Distinct == A2.Distinct, "identical runs (%f vs %f)", A.Checksum, A2.Checksum);
	const FPopulationResult B = RunPopulated(Dir / "PopulatedWorld.xosc", 8);
	CHECK_MSG(std::fabs(A.Checksum - B.Checksum) > 1e-6, "different seed differs");

	std::printf("== TrafficSourceSink: source 0.5/s on road 0, sink on road 3, 60 s\n");
	{
		UOpenScenarioRunner* R = Start(Dir / "TrafficSourceSink.xosc", 3);
		std::set<std::string> Names;
		int32 MaxActive = 0;
		bool bOnRoad2or4 = false;
		for (int i = 1; i <= 90 * 50 && R->IsRunning(); ++i)
		{
			R->Step(0.02);
			int32 Active = 0;
			for (const FOSCEntityState& E : R->GetEntities())
			{
				if (!E.bActive) { continue; }
				++Active;
				Names.insert(E.Name.S);
				bOnRoad2or4 |= (E.RoadId == "2" || E.RoadId == "4");
			}
			MaxActive = FMath::Max(MaxActive, Active);
		}
		const int32 End = R->GetTrafficCount();
		std::printf("   finished=%d t=%.1f spawned %d, active at end %d, max active %d\n", (int)R->IsFinished(), R->GetSimulationTime(), (int)Names.size(), End, MaxActive);
		CHECK_MSG(R->IsFinished(), "ends at the stop trigger even though generators are active");
		CHECK_MSG((int)Names.size() >= 24 && (int)Names.size() <= 31, "about one actor every 2 s (%d)", (int)Names.size());
		CHECK_MSG((int)Names.size() - End >= 10, "sink and dead-end rule remove actors (%d spawned, %d left)", (int)Names.size(), End);
		CHECK_MSG(bOnRoad2or4, "actors drive through the junction");
	}

	std::printf(Failures ? "%d FAILURES\n" : "traffic tests passed\n", Failures);
	return Failures ? 1 : 0;
}
