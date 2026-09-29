#include "Simulation/OpenScenarioActor.h"
#include "OpenDrive/OpenDriveMap.h"
#include "OpenScenarioCoordinates.h"
#include "OpenScenarioModule.h"
#include "Scenario/OpenScenarioAsset.h"
#include "Simulation/OpenScenarioRunner.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

AOpenScenarioActor::AOpenScenarioActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AOpenScenarioActor::BeginPlay()
{
	Super::BeginPlay();
	if (bDrawRoadNetwork)
	{
		DrawRoadNetwork();
	}
	if (bAutoStart)
	{
		StartScenario();
	}
}

void AOpenScenarioActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Runner)
	{
		Runner->OnFinished.RemoveAll(this);
		Runner->OnEntitySpawned.RemoveAll(this);
		Runner->Stop(true);
		Runner = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool AOpenScenarioActor::StartScenario()
{
	UWorld* World = GetWorld();
	if (!World || !Scenario)
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("%s: no world or no scenario asset assigned."), *GetName());
		return false;
	}
	StopScenario();

	Runner = NewObject<UOpenScenarioRunner>(this);
	Runner->Origin = GetActorTransform();
	Runner->Origin.SetScale3D(FVector::OneVector);
	Runner->bSpawnActors = bSpawnEntityActors;
	Runner->bStopWhenStoryboardComplete = bStopWhenStoryboardComplete;
	Runner->DefaultVehicleClass = VehicleActorClass;
	Runner->DefaultPedestrianClass = PedestrianActorClass;
	Runner->DefaultMiscObjectClass = MiscObjectActorClass;
	Runner->EntityClassOverrides = EntityActorClasses;
	Runner->OnFinished.AddUObject(this, &AOpenScenarioActor::HandleFinished);
	Runner->OnEntitySpawned.AddUObject(this, &AOpenScenarioActor::HandleEntitySpawned);

	Accumulator = 0.0;
	if (!Runner->Initialize(World, Scenario))
	{
		Runner = nullptr;
		return false;
	}
	return true;
}

void AOpenScenarioActor::StopScenario()
{
	if (Runner)
	{
		Runner->OnFinished.RemoveAll(this);
		Runner->OnEntitySpawned.RemoveAll(this);
		Runner->Stop(true);
		Runner = nullptr;
	}
}

bool AOpenScenarioActor::IsScenarioRunning() const
{
	return Runner && Runner->IsRunning();
}

double AOpenScenarioActor::GetSimulationTime() const
{
	return Runner ? Runner->GetSimulationTime() : 0.0;
}

AActor* AOpenScenarioActor::GetEntityActor(const FString& EntityName) const
{
	return Runner ? Runner->GetEntityActor(EntityName) : nullptr;
}

double AOpenScenarioActor::GetEntitySpeed(const FString& EntityName) const
{
	const FOSCEntityState* State = Runner ? Runner->GetEntityState(EntityName) : nullptr;
	return State ? State->Speed : 0.0;
}

void AOpenScenarioActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Runner || !Runner->IsRunning())
	{
		return;
	}
	Accumulator += static_cast<double>(DeltaSeconds) * TimeScale;
	const double Step = FMath::Max(0.001, static_cast<double>(FixedTimeStep));
	int32 Steps = 0;
	while (Accumulator >= Step && Steps < MaxStepsPerFrame && Runner->IsRunning())
	{
		Runner->Step(Step);
		Accumulator -= Step;
		++Steps;
	}
	if (Steps == MaxStepsPerFrame)
	{
		Accumulator = 0.0; // drop the backlog instead of spiralling
	}
}

void AOpenScenarioActor::HandleFinished()
{
	if (bDestroyEntityActorsOnEnd && Runner)
	{
		Runner->Stop(true);
	}
	OnScenarioFinished.Broadcast();
}

void AOpenScenarioActor::HandleEntitySpawned(const FString& Name, AActor* Actor)
{
	OnEntitySpawned.Broadcast(Name, Actor);
}

void AOpenScenarioActor::DrawRoadNetwork()
{
	UWorld* World = GetWorld();
	if (!World || !Scenario)
	{
		return;
	}
	const TSharedPtr<const FOpenDriveMap> Map = Scenario->ResolveRoadNetwork();
	if (!Map.IsValid())
	{
		UE_LOG(LogOpenScenario, Warning, TEXT("%s: scenario has no loadable road network to draw."), *GetName());
		return;
	}

	const FTransform Origin = GetActorTransform();
	auto ToWorld = [&Origin](const FOpenDrivePose& P)
	{
		return Origin.TransformPosition(OpenScenarioCoords::ToUnrealLocation(P.X, P.Y, P.Z + 0.05));
	};

	FlushPersistentDebugLines(World);
	for (const FOpenDriveRoad& Road : Map->GetRoads())
	{
		const int32 N = FMath::Max(1, FMath::CeilToInt(Road.Length / 2.0));
		FVector PrevRef, PrevLeft, PrevRight;
		for (int32 i = 0; i <= N; ++i)
		{
			const double S = FMath::Min(Road.Length, i * 2.0);
			const FVector Ref = ToWorld(Map->EvaluatePose(Road, S, Map->GetLaneOffset(Road, S)));
			const FVector Left = ToWorld(Map->EvaluatePose(Road, S, Map->GetOuterBorderT(Road, S, true)));
			const FVector Right = ToWorld(Map->EvaluatePose(Road, S, Map->GetOuterBorderT(Road, S, false)));
			if (i > 0)
			{
				DrawDebugLine(World, PrevRef, Ref, FColor::Yellow, true, -1.f, 0, 4.f);
				DrawDebugLine(World, PrevLeft, Left, FColor::White, true, -1.f, 0, 3.f);
				DrawDebugLine(World, PrevRight, Right, FColor::White, true, -1.f, 0, 3.f);
			}
			PrevRef = Ref;
			PrevLeft = Left;
			PrevRight = Right;
		}
	}
}

void AOpenScenarioActor::ClearRoadNetworkDrawing()
{
	if (UWorld* World = GetWorld())
	{
		FlushPersistentDebugLines(World);
	}
}
