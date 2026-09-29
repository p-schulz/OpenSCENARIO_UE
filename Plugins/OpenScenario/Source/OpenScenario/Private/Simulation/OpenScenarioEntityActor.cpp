#include "Simulation/OpenScenarioEntityActor.h"
#include "OpenScenarioCoordinates.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AOpenScenarioEntityActor::AOpenScenarioEntityActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(Root);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BodyMesh->SetStaticMesh(Cube.Object);
	}
}

void AOpenScenarioEntityActor::ConfigureFromEntity(const FOSCEntity& Entity)
{
	EntityName = Entity.Name;
	EntityCategory = Entity.Category;
	BoundingBoxSize = FVector(Entity.Length, Entity.Width, Entity.Height);

	// The engine cube is 100 cm wide, so the scale equals the size in metres.
	BodyMesh->SetRelativeScale3D(BoundingBoxSize);
	BodyMesh->SetRelativeLocation(OpenScenarioCoords::ToUnrealLocation(Entity.CenterX, Entity.CenterY, Entity.CenterZ));

	OnEntityConfigured(EntityName, EntityCategory, BoundingBoxSize);
}
