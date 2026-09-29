// Simple vehicle dynamics: acceleration limits, speed limits, curves, and reacting to actors appearing ahead.
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

struct FRun
{
	UOpenScenarioRunner* Runner = nullptr;
	double EndTime = 0.0;
	bool bFinished = false;
};

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

	// ---------------------------------------------------------------- limits and curves
	{
		std::printf("== DynamicsLimits: acceleration, 50 km/h limit, curve, 30 km/h lane limit\n");
		UOpenScenarioRunner* R = Start(Dir / "DynamicsLimits.xosc");
		double MaxSpeed = 0, MaxAccel = 0, MinAccel = 0, MaxCurveSpeed = 0, MaxZoneSpeed = 0, CurveEntrySpeed = -1, SpeedAt1s = 0;
		bool bSawRoad2 = false, bSawZone = false;
		for (int i = 1; i <= 3000 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const FOSCEntityState* E = R->GetEntityState("Ego");
			MaxSpeed = FMath::Max(MaxSpeed, E->Speed);
			MaxAccel = FMath::Max(MaxAccel, E->Accel);
			MinAccel = FMath::Min(MinAccel, E->Accel);
			if (i == 50) { SpeedAt1s = E->Speed; }
			if (E->RoadId == "2")
			{
				if (!bSawRoad2) { CurveEntrySpeed = E->Speed; }
				bSawRoad2 = true;
				MaxCurveSpeed = FMath::Max(MaxCurveSpeed, E->Speed);
			}
			if (E->RoadId == "3" && E->S >= 55.0) { bSawZone = true; MaxZoneSpeed = FMath::Max(MaxZoneSpeed, E->Speed); }
			if (i % 100 == 0) { std::printf("   t=%5.1f road %s s=%6.1f v=%5.2f a=%5.2f constrained=%d\n", R->GetSimulationTime(), *E->RoadId, E->S, E->Speed, E->Accel, (int)E->bSpeedConstrained); }
		}
		std::printf("   max speed %.2f, accel %.2f..%.2f, curve max %.2f (entry %.2f), 30 km/h zone max %.2f, t=%.1f\n", MaxSpeed, MinAccel, MaxAccel, MaxCurveSpeed, CurveEntrySpeed, MaxZoneSpeed, R->GetSimulationTime());
		CHECK_MSG(R->IsFinished(), "scenario should end when the Ego reaches road 3");
		CHECK_MSG(SpeedAt1s > 2.0 && SpeedAt1s < 3.6, "accelerates with about 3.5 m/s^2 (v(1s)=%.2f)", SpeedAt1s);
		CHECK_MSG(MaxSpeed <= 50.0 / 3.6 + 0.05, "never exceeds the 50 km/h limit (max %.2f)", MaxSpeed);
		CHECK_MSG(MaxSpeed > 50.0 / 3.6 - 0.5, "reaches the limit on the straight (max %.2f)", MaxSpeed);
		CHECK_MSG(MaxAccel <= 3.55, "acceleration bounded (%.2f)", MaxAccel);
		CHECK_MSG(MinAccel >= -8.05, "deceleration bounded (%.2f)", MinAccel);
		CHECK_MSG(bSawRoad2 && MaxCurveSpeed <= 9.9, "curve speed <= sqrt(3*R) = 9.8 (max %.2f)", MaxCurveSpeed);
		CHECK_MSG(CurveEntrySpeed <= 10.3, "already slow when entering the curve (%.2f)", CurveEntrySpeed);
		CHECK_MSG(bSawZone && MaxZoneSpeed <= 30.0 / 3.6 + 0.15, "lane limit 30 km/h obeyed (max %.2f)", MaxZoneSpeed);
	}

	// ---------------------------------------------------------------- vehicle appearing ahead
	{
		std::printf("== DynamicsLeader: slow vehicle appears in front\n");
		UOpenScenarioRunner* R = Start(Dir / "DynamicsLeader.xosc");
		double MinGap = 1e9, MinAccel = 0, SpeedBefore = 0;
		bool bDetected = false;
		for (int i = 1; i <= 3000 && R->IsRunning(); ++i)
		{
			R->Step(Dt);
			const double T = R->GetSimulationTime();
			const FOSCEntityState* E = R->GetEntityState("Ego");
			const FOSCEntityState* L = R->GetEntityState("Leader");
			MinAccel = FMath::Min(MinAccel, E->Accel);
			if (std::fabs(T - 5.9) < Dt / 2) { SpeedBefore = E->Speed; }
			if (T > 6.1)
			{
				const double Cex = E->X + FMath::Cos(E->Heading) * E->Def.CenterX, Cey = E->Y + FMath::Sin(E->Heading) * E->Def.CenterX;
				const double Clx = L->X + FMath::Cos(L->Heading) * L->Def.CenterX, Cly = L->Y + FMath::Sin(L->Heading) * L->Def.CenterX;
				const double Gap = FMath::Sqrt(FMath::Square(Cex - Clx) + FMath::Square(Cey - Cly)) - 0.5 * (E->Def.Length + L->Def.Length);
				MinGap = FMath::Min(MinGap, Gap);
				if (E->LeaderName == "Leader") { bDetected = true; }
			}
			if (i % 100 == 0)
			{
				std::printf("   t=%5.1f ego road %s s=%6.1f v=%5.2f | leader road %s s=%6.1f v=%5.2f | gap %6.2f (%s)\n", T, *E->RoadId, E->S, E->Speed, *L->RoadId, L->S, L->Speed, E->LeaderGap, *E->LeaderName);
			}
		}
		const FOSCEntityState* E = R->GetEntityState("Ego");
		const FOSCEntityState* L = R->GetEntityState("Leader");
		std::printf("   ego v before appearance %.2f, min bumper gap %.2f, ego v at end %.2f, leader v %.2f\n", SpeedBefore, MinGap, E->Speed, L->Speed);
		CHECK_MSG(R->IsFinished(), "ends at the timeout");
		CHECK_MSG(SpeedBefore > 13.0, "Ego is at the speed limit before the vehicle appears (%.2f)", SpeedBefore);
		CHECK_MSG(bDetected, "Ego detects the vehicle in front");
		CHECK_MSG(MinGap > 0.5, "no collision, gap stays positive (min %.2f)", MinGap);
		CHECK_MSG(MinAccel >= -8.05, "deceleration bounded (%.2f)", MinAccel);
		CHECK_MSG(std::fabs(E->Speed - L->Speed) < 1.0, "Ego follows the leader's speed (%.2f vs %.2f)", E->Speed, L->Speed);
	}

	std::printf(Failures ? "%d FAILURES\n" : "dynamics tests passed\n", Failures);
	return Failures ? 1 : 0;
}
