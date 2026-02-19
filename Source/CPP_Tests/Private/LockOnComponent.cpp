#include "LockOnComponent.h"

#include "LockOnTargetable.h"

#include "Engine/World.h"
#include "Engine/OverlapResult.h"      
#include "Engine/EngineTypes.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ULockOnComponent::ULockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULockOnComponent::BeginPlay()
{
	Super::BeginPlay();
}

void ULockOnComponent::ToggleLockOn()
{
	// If active -> clear
	if (IsValid(CurrentTarget))
	{
		ClearLockOn();
		return;
	}

	// If inactive -> acquire
	CurrentTarget = FindBestTarget();
	OnLockOnStateChanged.Broadcast(IsValid(CurrentTarget), CurrentTarget);
}

void ULockOnComponent::ClearLockOn()
{
	CurrentTarget = nullptr;
	OnLockOnStateChanged.Broadcast(false, nullptr);
}

bool ULockOnComponent::GetViewPoint(FVector& OutLoc, FRotator& OutRot) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		return false;
	}

	// Prefer real camera viewpoint if we have a player controller
	if (APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
	{
		PC->GetPlayerViewPoint(OutLoc, OutRot);
		return true;
	}

	// Fallback
	OutLoc = OwnerPawn->GetActorLocation();
	OutRot = OwnerPawn->GetActorRotation();
	return true;
}

bool ULockOnComponent::HasLineOfSightTo(AActor* Candidate, const FVector& ViewLoc) const
{
	if (!bRequireLineOfSight)
	{
		return true;
	}

	if (!IsValid(Candidate) || !GetWorld())
	{
		return false;
	}

	// Candidate is expected to be LockOnTargetable (we filter earlier),
	// but guard anyway to avoid calling Execute_ on non-interface actors.
	FVector TargetLoc = Candidate->GetActorLocation();
	if (Candidate->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
	{
		TargetLoc = ILockOnTargetable::Execute_GetLockOnWorldLocation(Candidate);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LockOnLOS), false);
	Params.AddIgnoredActor(GetOwner());

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLoc,
		TargetLoc,
		LineOfSightTraceChannel,
		Params
	);

	if (!bHit)
	{
		return true;
	}

	// If we hit the candidate (or something attached to it), accept
	return Hit.GetActor() == Candidate;
}

AActor* ULockOnComponent::FindBestTarget() const
{
	UWorld* World = GetWorld();
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!World || !OwnerPawn)
	{
		return nullptr;
	}

	if (LockOnSearchRadius <= 0.0f)
	{
		return nullptr;
	}

	FVector ViewLoc;
	FRotator ViewRot;
	if (!GetViewPoint(ViewLoc, ViewRot))
	{
		return nullptr;
	}

	const FVector ViewDir = ViewRot.Vector();
	const FVector Origin = OwnerPawn->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(LockOnSearchRadius);

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LockOnOverlap), false);
	Params.AddIgnoredActor(OwnerPawn);

	const bool bAny = World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjParams,
		Sphere,
		Params
	);

	if (!bAny)
	{
		return nullptr;
	}

	AActor* Best = nullptr;
	float BestScore = -FLT_MAX;

	for (const FOverlapResult& O : Overlaps)
	{
		AActor* Candidate = O.GetActor();
		if (!IsValid(Candidate)) continue;
		if (Candidate == OwnerPawn) continue;

		// Only consider LockOnTargetable actors.
		if (!Candidate->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
		{
			continue;
		}

		// Gate (dead, hidden, etc.)
		if (!ILockOnTargetable::Execute_IsLockOnAllowed(Candidate))
		{
			continue;
		}

		const FVector TargetLoc = ILockOnTargetable::Execute_GetLockOnWorldLocation(Candidate);
		const FVector ToTarget = (TargetLoc - ViewLoc);

		const float DistSq = ToTarget.SizeSquared();
		if (DistSq <= KINDA_SMALL_NUMBER) continue;

		const float Dist = FMath::Sqrt(DistSq);
		if (Dist > LockOnSearchRadius) continue;

		const FVector Dir = ToTarget / Dist;
		const float Dot = FVector::DotProduct(ViewDir, Dir);
		if (Dot < MinViewDot) continue;

		if (!HasLineOfSightTo(Candidate, ViewLoc))
		{
			continue;
		}

		// Score: prioritize centered targets and nearer targets
		const float DistScore = 1.0f - FMath::Clamp(Dist / LockOnSearchRadius, 0.0f, 1.0f);
		const float Score = (Dot * 2.0f) + (DistScore * 1.0f);

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}

	return Best;
}
