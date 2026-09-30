#include "Authoring/OpenScenarioMapVisualizer.h"
#include "Authoring/OpenScenarioEditorContext.h"
#include "Authoring/OpenScenarioEditorSettings.h"
#include "OpenDrive/OpenDriveMap.h"
#include "OpenScenarioCoordinates.h"
#include "Scenario/OpenScenarioModelEdit.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/World.h"

void FOpenScenarioMapVisualizer::AddArrow(const FVector& From, const FVector& To, const FColor& Color, float Thickness)
{
	Lines.Add({ From, To, Color, Thickness });
	const FVector Dir = (To - From).GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
	const double Head = (To - From).Size() * 0.3;
	Lines.Add({ To, To - Dir * Head + Side * Head * 0.5, Color, Thickness });
	Lines.Add({ To, To - Dir * Head - Side * Head * 0.5, Color, Thickness });
}

void FOpenScenarioMapVisualizer::AddBox(const FTransform& Pose, const FVector& CenterCm, const FVector& HalfExtentCm, const FColor& Color, float Thickness)
{
	FVector Corners[8];
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Local = CenterCm + FVector((i & 1) ? HalfExtentCm.X : -HalfExtentCm.X, (i & 2) ? HalfExtentCm.Y : -HalfExtentCm.Y, (i & 4) ? HalfExtentCm.Z : -HalfExtentCm.Z);
		Corners[i] = Pose.TransformPosition(Local);
	}
	static const int32 Edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
	for (const int32* E : Edges)
	{
		Lines.Add({ Corners[E[0]], Corners[E[1]], Color, Thickness });
	}
}

void FOpenScenarioMapVisualizer::Rebuild(FOpenScenarioEditorContext& Context)
{
	Lines.Reset();
	Labels.Reset();

	const UOpenScenarioEditorSettings& Opt = *Context.GetSettings();
	const TSharedPtr<const FOpenDriveMap> Map = Context.GetMap();
	const FTransform Origin = Context.ResolveOrigin();
	const float Thickness = Opt.LineThickness;
	const FVector Lift(0.0, 0.0, Opt.ZOffsetCm);

	auto ToWorld = [&](const FOpenDrivePose& P, double Extra = 0.0)
	{
		return Origin.TransformPosition(OpenScenarioCoords::ToUnrealLocation(P.X, P.Y, P.Z) + Lift + FVector(0, 0, Extra));
	};

	const FColor RefColor(255, 210, 0);
	const FColor BorderColor(230, 230, 230);
	const FColor JunctionColor(255, 130, 20);
	const FColor CenterColor(0, 200, 220);
	const FColor ArrowColor(60, 220, 90);

	if (Map.IsValid() && Opt.bShowMap)
	{
		const double Step = FMath::Max(0.25, static_cast<double>(Opt.SampleStep));
		for (const FOpenDriveRoad& Road : Map->GetRoads())
		{
			const bool bJunction = Opt.bHighlightJunctionRoads && Road.IsJunctionRoad();
			const int32 N = FMath::Max(1, FMath::CeilToInt(Road.Length / Step));

			bool bHavePrev = false;
			FVector PrevRef = FVector::ZeroVector;
			TMap<int32, FVector> PrevBorder;
			TMap<int32, FVector> PrevCenter;

			for (int32 i = 0; i <= N; ++i)
			{
				const double S = FMath::Min(Road.Length, i * Step);

				const FVector Ref = ToWorld(Map->EvaluatePose(Road, S, Map->GetLaneOffset(Road, S)));
				if (bHavePrev && Opt.bDrawReferenceLines)
				{
					Lines.Add({ PrevRef, Ref, bJunction ? JunctionColor : RefColor, Thickness });
				}
				PrevRef = Ref;
				bHavePrev = true;

				TMap<int32, FVector> CurBorder;
				TMap<int32, FVector> CurCenter;
				if (const FOpenDriveLaneSection* Section = Map->FindLaneSection(Road, S))
				{
					for (const FOpenDriveLane& Lane : Section->Lanes)
					{
						const double Center = Map->GetLaneCenterT(Road, S, Lane.Id);
						const double Half = 0.5 * Lane.GetWidth(S);
						const double Outer = Center + (Lane.Id > 0 ? Half : -Half);
						CurBorder.Add(Lane.Id, ToWorld(Map->EvaluatePose(Road, S, Outer)));
						CurCenter.Add(Lane.Id, ToWorld(Map->EvaluatePose(Road, S, Center)));
					}
				}
				if (Opt.bDrawLaneBorders)
				{
					for (const TPair<int32, FVector>& Pair : CurBorder)
					{
						if (const FVector* Prev = PrevBorder.Find(Pair.Key))
						{
							Lines.Add({ *Prev, Pair.Value, bJunction ? JunctionColor : BorderColor, Thickness * 0.75f });
						}
					}
				}
				if (Opt.bDrawLaneCenters)
				{
					for (const TPair<int32, FVector>& Pair : CurCenter)
					{
						if (const FVector* Prev = PrevCenter.Find(Pair.Key))
						{
							Lines.Add({ *Prev, Pair.Value, CenterColor, Thickness * 0.5f });
						}
					}
				}
				PrevBorder = MoveTemp(CurBorder);
				PrevCenter = MoveTemp(CurCenter);
			}

			if (Opt.bDrawDirectionArrows)
			{
				const double ArrowSpacing = FMath::Max(10.0, Step * 5.0);
				for (double S = 0.5 * ArrowSpacing; S < Road.Length; S += ArrowSpacing)
				{
					const FOpenDriveLaneSection* Section = Map->FindLaneSection(Road, S);
					if (!Section)
					{
						continue;
					}
					for (const FOpenDriveLane& Lane : Section->Lanes)
					{
						if (!Lane.IsDriving())
						{
							continue;
						}
						const double T = Map->GetLaneCenterT(Road, S, Lane.Id);
						const double Dir = Lane.Id < 0 ? 1.0 : -1.0;
						const double Len = FMath::Min(3.0, 0.5 * Lane.GetWidth(S) + 1.5);
						AddArrow(ToWorld(Map->EvaluatePose(Road, S - Dir * 0.5 * Len, T), 2.0), ToWorld(Map->EvaluatePose(Road, FMath::Clamp(S + Dir * 0.5 * Len, 0.0, Road.Length), T), 2.0), ArrowColor, Thickness);
					}
				}
			}

			if (Opt.bDrawRoadLabels)
			{
				const double S = 0.5 * Road.Length;
				Labels.Add({ ToWorld(Map->EvaluatePose(Road, S, Map->GetLaneOffset(Road, S)), 30.0),
					FString::Printf(TEXT("road %s%s"), *Road.Id, Road.IsJunctionRoad() ? TEXT(" (junction)") : TEXT("")), bJunction ? JunctionColor : RefColor });
			}
		}
	}

	if (Opt.bDrawEntityStarts)
	{
		const FOSCScenario& Scenario = Context.GetWorking();
		for (const FOSCInitActions& Group : Scenario.InitActions)
		{
			const FOSCEntity* Entity = Scenario.FindEntity(Group.EntityRef);
			for (const FOSCAction& Action : Group.Actions)
			{
				double X, Y, Z, H;
				if (Entity && Action.Type == EOSCActionType::Teleport && FOSCModelEdit::ResolveStaticPose(Map.Get(), Action.Position, X, Y, Z, H))
				{
					const FVector Loc = Origin.TransformPosition(OpenScenarioCoords::ToUnrealLocation(X, Y, Z) + Lift);
					const FQuat Rot = Origin.GetRotation() * FQuat(FRotator(0.0, OpenScenarioCoords::HeadingToYawDegrees(H), 0.0));
					const FTransform Pose(Rot, Loc);
					const FColor Color = Entity->Kind == EOSCEntityKind::Pedestrian ? FColor(255, 90, 200) : Entity->Kind == EOSCEntityKind::MiscObject ? FColor(200, 160, 90) : FColor(255, 60, 60);
					AddBox(Pose, OpenScenarioCoords::ToUnrealLocation(Entity->CenterX, Entity->CenterY, Entity->CenterZ),
						FVector(Entity->Length, Entity->Width, Entity->Height) * 50.0, Color, Thickness);
					AddArrow(Pose.TransformPosition(FVector::ZeroVector), Pose.TransformPosition(FVector(Entity->Length * 100.0 + 100.0, 0, 0)), Color, Thickness);
					Labels.Add({ Pose.TransformPosition(FVector(0, 0, Entity->Height * 100.0 + 40.0)), Entity->Name, Color });
					break; // first resolvable teleport of this entity
				}
			}
		}
	}

	CachedMap = Map.Get();
	CachedSettingsRevision = Opt.Revision;
	CachedModelRevision = Context.GetModelRevision();
	CachedOrigin = Origin;
	bHasCache = true;
}

void FOpenScenarioMapVisualizer::Clear()
{
	if (UWorld* World = DrawnWorld.Get())
	{
		FlushPersistentDebugLines(World);
		FlushDebugStrings(World);
	}
	DrawnWorld.Reset();
	bHasCache = false;
}

void FOpenScenarioMapVisualizer::Update(FOpenScenarioEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}
	if (!Context.GetAsset())
	{
		if (DrawnWorld.IsValid())
		{
			Clear();
		}
		return;
	}

	const UOpenScenarioEditorSettings& Opt = *Context.GetSettings();
	const FTransform Origin = Context.ResolveOrigin();
	const bool bChanged = !bHasCache || DrawnWorld.Get() != World || CachedMap != Context.GetMap().Get()
		|| CachedSettingsRevision != Opt.Revision || CachedModelRevision != Context.GetModelRevision() || !CachedOrigin.Equals(Origin);
	if (!bChanged)
	{
		return;
	}

	Rebuild(Context);
	FlushPersistentDebugLines(World);
	FlushDebugStrings(World);
	for (const FLine& L : Lines)
	{
		DrawDebugLine(World, L.A, L.B, L.Color, true, -1.f, SDPG_World, L.Thickness);
	}
	// Cap the labels so big maps stay readable and cheap.
	int32 Drawn = 0;
	for (const FLabel& L : Labels)
	{
		if (Drawn++ >= 400)
		{
			break;
		}
		DrawDebugString(World, L.Position, L.Text, nullptr, L.Color, -1.f, true);
	}
	DrawnWorld = World;
}
