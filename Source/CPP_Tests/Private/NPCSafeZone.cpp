#include "NPCSafeZone.h"

#include "Components/SphereComponent.h"
#include "NPCCharacter.h"
#include "NavigationSystem.h"

ANPCSafeZone::ANPCSafeZone()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ZoneSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ZoneSphere"));
	ZoneSphere->SetupAttachment(Root);
	ZoneSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneSphere->SetHiddenInGame(false);
}

void ANPCSafeZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (ZoneSphere)
	{
		ZoneSphere->SetSphereRadius(ZoneRadius);
	}
}

FVector ANPCSafeZone::GetRandomPointInZone() const
{
	const FVector Center = GetActorLocation();

	// 2D disc sampling (uniform)
	FVector2D Dir2D = FVector2D(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
	if (!Dir2D.Normalize())
	{
		Dir2D = FVector2D(1.f, 0.f);
	}

	const float Dist = FMath::Sqrt(FMath::FRand()) * ZoneRadius;

	return Center + FVector(Dir2D.X * Dist, Dir2D.Y * Dist, 0.0f);
}

bool ANPCSafeZone::GetRandomReachablePointInZone(FVector& OutLocation, float RadiusOverride) const
{
	// IMPORTANT: do NOT store this as const UWorld*, GetCurrent() wants non-const.
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
	{
		return false;
	}

	const float RadiusToUse = (RadiusOverride > 0.0f) ? RadiusOverride : ZoneRadius;

	// Preferred: navmesh reachable point around zone center
	FNavLocation NavLoc;
	if (NavSys->GetRandomReachablePointInRadius(GetActorLocation(), RadiusToUse, NavLoc))
	{
		OutLocation = NavLoc.Location;
		return true;
	}

	// Fallback: project a random zone point onto navmesh
	const FVector Candidate = GetRandomPointInZone();
	const FVector Extent(200.f, 200.f, 500.f);

	if (NavSys->ProjectPointToNavigation(Candidate, NavLoc, Extent))
	{
		OutLocation = NavLoc.Location;
		return true;
	}

	return false;
}

void ANPCSafeZone::RegisterNPC(ANPCCharacter* NPC)
{
	if (!IsValid(NPC))
	{
		return;
	}

	BoundNPCs.AddUnique(NPC);
}

void ANPCSafeZone::UnregisterNPC(ANPCCharacter* NPC)
{
	if (!IsValid(NPC))
	{
		return;
	}

	BoundNPCs.Remove(NPC);
}
