#include "SeasonRegionVolume.h"

#include "SeasonWorldManager.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ASeasonRegionVolume::ASeasonRegionVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetGenerateOverlapEvents(true);
	Box->InitBoxExtent(FVector(300.0f, 300.0f, 200.0f));
}

void ASeasonRegionVolume::BeginPlay()
{
	Super::BeginPlay();

	if (Box)
	{
		Box->OnComponentBeginOverlap.AddDynamic(this, &ASeasonRegionVolume::OnBoxBegin);
		Box->OnComponentEndOverlap.AddDynamic(this, &ASeasonRegionVolume::OnBoxEnd);
		Box->UpdateOverlaps();
	}

	ResolveManagerIfNeeded();
	if (CachedManager.IsValid())
	{
		CachedManager->RegisterRegionVolume(this);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!IsValid(Box))
				{
					return;
				}

				Box->UpdateOverlaps();

				TArray<AActor*> InitialOverlaps;
				Box->GetOverlappingActors(InitialOverlaps, APawn::StaticClass());

				const FHitResult DummyHit;
				for (AActor* OverlapActor : InitialOverlaps)
				{
					OnBoxBegin(Box, OverlapActor, nullptr, 0, false, DummyHit);
				}
			}));
	}
}

void ASeasonRegionVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedManager.IsValid())
	{
		CachedManager->UnregisterRegionVolume(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool ASeasonRegionVolume::IsActorInside(const AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> WeakActor(const_cast<AActor*>(Actor));
	if (OverlappingPawns.Contains(WeakActor))
	{
		return true;
	}

	return IsValid(Box) && Box->IsOverlappingActor(const_cast<AActor*>(Actor));
}

void ASeasonRegionVolume::BuildOverride(float GlobalTimeOfDayHours, EWorldSeason& OutSeason, float& OutTimeOfDayHours)
{
	if (!bRuntimeClockInitialized)
	{
		InitializeRuntimeClock(GlobalTimeOfDayHours);
	}

	OutSeason = OverrideSeason;
	OutTimeOfDayHours = WrapTimeOfDayHours(RuntimeTimeOfDayHours);
}

void ASeasonRegionVolume::InitializeRuntimeClock(float GlobalTimeOfDayHours)
{
	if (Mode == ESeasonRegionMode::Procedural)
	{
		RuntimeTimeOfDayHours = WrapTimeOfDayHours(GlobalTimeOfDayHours);
		bRuntimeClockInitialized = true;
		return;
	}

	if (LockedTimeRule == ESeasonLockedTimeRule::FixedTime)
	{
		RuntimeTimeOfDayHours = WrapTimeOfDayHours(LockedTimeOfDayHours);
		bRuntimeClockInitialized = true;
		return;
	}

	// Offset rule supports earlier/later local time with negative or positive offset.
	RuntimeTimeOfDayHours = WrapTimeOfDayHours(GlobalTimeOfDayHours + LockedTimeOffsetHours);
	bRuntimeClockInitialized = true;
}

void ASeasonRegionVolume::TickRuntimeClock(float DeltaSeconds)
{
	if (!bRuntimeClockInitialized || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float SafeDayLengthMinutes = FMath::Max(0.1f, DayLengthMinutes);
	const float HoursPerSecond = 24.0f / (SafeDayLengthMinutes * 60.0f);
	RuntimeTimeOfDayHours = WrapTimeOfDayHours(RuntimeTimeOfDayHours + (HoursPerSecond * DeltaSeconds));
}

void ASeasonRegionVolume::OnBoxBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn))
	{
		return;
	}

	const TWeakObjectPtr<AActor> WeakPawn(Pawn);
	const bool bWasInside = OverlappingPawns.Contains(WeakPawn);
	OverlappingPawns.Add(WeakPawn);

	if (!bWasInside)
	{
		NotifyManagerOverlapChanged();
	}
}

void ASeasonRegionVolume::OnBoxEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!IsValid(Pawn))
	{
		return;
	}

	const TWeakObjectPtr<AActor> WeakPawn(Pawn);
	const bool bWasInside = OverlappingPawns.Contains(WeakPawn);
	OverlappingPawns.Remove(WeakPawn);

	if (bWasInside)
	{
		NotifyManagerOverlapChanged();
	}
}

void ASeasonRegionVolume::ResolveManagerIfNeeded()
{
	if (CachedManager.IsValid())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		CachedManager = Cast<ASeasonWorldManager>(
			UGameplayStatics::GetActorOfClass(World, ASeasonWorldManager::StaticClass()));
	}
}

void ASeasonRegionVolume::NotifyManagerOverlapChanged()
{
	ResolveManagerIfNeeded();
	if (CachedManager.IsValid())
	{
		CachedManager->NotifyRegionOverlapChanged(this);
	}
}

float ASeasonRegionVolume::WrapTimeOfDayHours(float InHours)
{
	float Wrapped = FMath::Fmod(InHours, 24.0f);
	if (Wrapped < 0.0f)
	{
		Wrapped += 24.0f;
	}
	return Wrapped;
}
