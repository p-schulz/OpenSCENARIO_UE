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
	PlaybackState = EOpenScenarioPlaybackState::Stopped;
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
	Runner->Dynamics = Dynamics;
	Runner->TrafficSettings = Traffic;
	Runner->OnFinished.AddUObject(this, &AOpenScenarioActor::HandleFinished);
	Runner->OnEntitySpawned.AddUObject(this, &AOpenScenarioActor::HandleEntitySpawned);

	Accumulator = 0.0;
	if (!Runner->Initialize(World, Scenario))
	{
		Runner = nullptr;
		SetPlaybackState(EOpenScenarioPlaybackState::Stopped);
		return false;
	}
	SetPlaybackState(bStartPaused ? EOpenScenarioPlaybackState::Paused : EOpenScenarioPlaybackState::Playing);
	return true;
}

void AOpenScenarioActor::PlayScenario()
{
	switch (PlaybackState)
	{
	case EOpenScenarioPlaybackState::Paused:
		SetPlaybackState(EOpenScenarioPlaybackState::Playing);
		break;
	case EOpenScenarioPlaybackState::Playing:
		break;
	default:
	{
		const bool bWasPaused = bStartPaused;
		bStartPaused = false;
		StartScenario();
		bStartPaused = bWasPaused;
		break;
	}
	}
}

void AOpenScenarioActor::PauseScenario()
{
	if (PlaybackState == EOpenScenarioPlaybackState::Playing)
	{
		SetPlaybackState(EOpenScenarioPlaybackState::Paused);
	}
}

void AOpenScenarioActor::StepScenario()
{
	if (PlaybackState == EOpenScenarioPlaybackState::Stopped || PlaybackState == EOpenScenarioPlaybackState::Finished)
	{
		// Stepping from a stopped scenario starts it paused on its first step.
		const bool bWasPaused = bStartPaused;
		bStartPaused = true;
		const bool bStarted = StartScenario();
		bStartPaused = bWasPaused;
		if (!bStarted)
		{
			return;
		}
	}
	if (PlaybackState == EOpenScenarioPlaybackState::Paused && Runner && Runner->IsRunning())
	{
		Runner->Step(FMath::Max(0.001, static_cast<double>(FixedTimeStep)));
	}
}

bool AOpenScenarioActor::RestartScenario()
{
	StopScenario();
	return StartScenario();
}

void AOpenScenarioActor::SetTimeScale(float NewTimeScale)
{
	TimeScale = FMath::Max(0.f, NewTimeScale);
}

bool AOpenScenarioActor::ShouldTickIfViewportsOnly() const
{
	const UWorld* World = GetWorld();
	return PlaybackState == EOpenScenarioPlaybackState::Playing && World && !World->IsGameWorld();
}

void AOpenScenarioActor::SetPlaybackState(EOpenScenarioPlaybackState NewState)
{
	if (PlaybackState != NewState)
	{
		PlaybackState = NewState;
		OnPlaybackStateChanged.Broadcast(NewState);
	}
}

int32 AOpenScenarioActor::GetActiveTrafficCount() const
{
	return Runner ? Runner->GetTrafficCount() : 0;
}

bool AOpenScenarioActor::GetEntityDynamics(const FString& EntityName, double& OutSpeed, double& OutDesiredSpeed, double& OutLeaderGap, FString& OutLeaderName) const
{
	const FOSCEntityState* State = Runner ? Runner->GetEntityState(EntityName) : nullptr;
	if (!State)
	{
		return false;
	}
	OutSpeed = State->Speed;
	OutDesiredSpeed = State->DesiredSpeed;
	OutLeaderGap = State->LeaderGap;
	OutLeaderName = State->LeaderName;
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
	SetPlaybackState(EOpenScenarioPlaybackState::Stopped);
}

bool AOpenScenarioActor::IsScenarioRunning() const
{
	return PlaybackState == EOpenScenarioPlaybackState::Playing || PlaybackState == EOpenScenarioPlaybackState::Paused;
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
	if (PlaybackState != EOpenScenarioPlaybackState::Playing || !Runner || !Runner->IsRunning())
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
	SetPlaybackState(EOpenScenarioPlaybackState::Finished);
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
