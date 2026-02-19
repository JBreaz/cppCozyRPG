#include "DamageTestVolume.h"

#include "Components/BoxComponent.h"
#include "CPP_TestsCharacter.h"
#include "NPCCharacter.h"

#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

ADamageTestVolume::ADamageTestVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);

	// Trigger is the simplest, most reliable overlap setup for Pawns
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetGenerateOverlapEvents(true);

	Box->InitBoxExtent(FVector(100.f, 100.f, 100.f));
}

void ADamageTestVolume::BeginPlay()
{
	Super::BeginPlay();

	if (!Box) return;

	Box->OnComponentBeginOverlap.AddDynamic(this, &ADamageTestVolume::OnBoxBegin);
	Box->OnComponentEndOverlap.AddDynamic(this, &ADamageTestVolume::OnBoxEnd);

	// Helps, but not always enough by itself; tick-reconcile below is the real fix
	Box->UpdateOverlaps();

	// Seed once on next tick (after engine resolves initial overlaps)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!Box) return;

				Box->UpdateOverlaps();

				TArray<AActor*> InitialOverlaps;
				Box->GetOverlappingActors(InitialOverlaps);

				const FHitResult DummyHit;
				for (AActor* A : InitialOverlaps)
				{
					OnBoxBegin(Box, A, nullptr, 0, false, DummyHit);
				}

				if (bPrintDebug && GEngine)
				{
					const FString Msg = FString::Printf(TEXT("Seeded initial overlaps: %d"), InitialOverlaps.Num());
					GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, Msg);
				}
			})
		);
	}
}

void ADamageTestVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Box || DeltaSeconds <= 0.f)
	{
		return;
	}

	// --- Reconcile overlaps every tick (fixes start-overlap edge cases) ---
	TArray<AActor*> CurrentOverlaps;
	Box->GetOverlappingActors(CurrentOverlaps);

	TSet<TWeakObjectPtr<ACPP_TestsCharacter>> CurrentPlayers;
	TSet<TWeakObjectPtr<ANPCCharacter>> CurrentNPCs;

	// Build current sets and fire "enter" if needed
	{
		const FHitResult DummyHit;

		for (AActor* A : CurrentOverlaps)
		{
			if (!IsValid(A)) continue;

			if (ACPP_TestsCharacter* P = Cast<ACPP_TestsCharacter>(A))
			{
				CurrentPlayers.Add(P);

				if (!OverlappingPlayers.Contains(P))
				{
					OnBoxBegin(Box, P, nullptr, 0, false, DummyHit);
				}
				continue;
			}

			if (ANPCCharacter* N = Cast<ANPCCharacter>(A))
			{
				CurrentNPCs.Add(N);

				if (!OverlappingNPCs.Contains(N))
				{
					OnBoxBegin(Box, N, nullptr, 0, false, DummyHit);
				}
				continue;
			}
		}
	}

	// Fire "exit" for anything that used to be inside but isn't anymore
	{
		for (auto It = OverlappingPlayers.CreateIterator(); It; ++It)
		{
			ACPP_TestsCharacter* P = It->Get();
			if (!IsValid(P) || !CurrentPlayers.Contains(P))
			{
				OnBoxEnd(Box, P, nullptr, 0);
			}
		}

		for (auto It = OverlappingNPCs.CreateIterator(); It; ++It)
		{
			ANPCCharacter* N = It->Get();
			if (!IsValid(N) || !CurrentNPCs.Contains(N))
			{
				OnBoxEnd(Box, N, nullptr, 0);
			}
		}
	}

	// Replace sets with current (authoritative)
	OverlappingPlayers = MoveTemp(CurrentPlayers);
	OverlappingNPCs = MoveTemp(CurrentNPCs);

	// --- Apply effect each tick ---
	for (auto It = OverlappingPlayers.CreateIterator(); It; ++It)
	{
		ACPP_TestsCharacter* P = It->Get();
		if (!IsValid(P))
		{
			It.RemoveCurrent();
			continue;
		}
		ApplyToPlayer(P, DeltaSeconds);
	}

	for (auto It = OverlappingNPCs.CreateIterator(); It; ++It)
	{
		ANPCCharacter* N = It->Get();
		if (!IsValid(N))
		{
			It.RemoveCurrent();
			continue;
		}
		ApplyToNPC(N, DeltaSeconds);
	}
}

void ADamageTestVolume::OnBoxBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	if (!IsValid(OtherActor)) return;

	// Player
	if (ACPP_TestsCharacter* Player = Cast<ACPP_TestsCharacter>(OtherActor))
	{
		const bool bWasAlreadyInside = OverlappingPlayers.Contains(Player);
		OverlappingPlayers.Add(Player);

		// Burn is “sticky”: apply once on entry (optional)
		if (!bWasAlreadyInside && EffectType == EStatusEffectType::Burn && bApplyBurnOnEnter)
		{
			if (UStatusEffectComponent* Effects = Player->FindComponentByClass<UStatusEffectComponent>())
			{
				Effects->ApplyBurn(true);
			}
		}

		if (bPrintDebug && GEngine && !bWasAlreadyInside)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, TEXT("Entered Hazard Volume (Player)"));
		}
		return;
	}

	// NPC
	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(OtherActor))
	{
		const bool bWasAlreadyInside = OverlappingNPCs.Contains(NPC);
		OverlappingNPCs.Add(NPC);

		if (bPrintDebug && GEngine && !bWasAlreadyInside)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, TEXT("Entered Hazard Volume (NPC)"));
		}
		return;
	}
}

void ADamageTestVolume::OnBoxEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!IsValid(OtherActor)) return;

	if (ACPP_TestsCharacter* Player = Cast<ACPP_TestsCharacter>(OtherActor))
	{
		const bool bWasInside = OverlappingPlayers.Contains(Player);
		OverlappingPlayers.Remove(Player);

		if (bPrintDebug && GEngine && bWasInside)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, TEXT("Exited Hazard Volume (Player)"));
		}
		return;
	}

	if (ANPCCharacter* NPC = Cast<ANPCCharacter>(OtherActor))
	{
		const bool bWasInside = OverlappingNPCs.Contains(NPC);
		OverlappingNPCs.Remove(NPC);

		if (bPrintDebug && GEngine && bWasInside)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, TEXT("Exited Hazard Volume (NPC)"));
		}
		return;
	}
}

void ADamageTestVolume::ApplyToPlayer(ACPP_TestsCharacter* Player, float DeltaSeconds)
{
	if (!IsValid(Player) || DeltaSeconds <= 0.f) return;

	UPlayerStatsComponent* Stats = Player->FindComponentByClass<UPlayerStatsComponent>();
	UStatusEffectComponent* Effects = Player->FindComponentByClass<UStatusEffectComponent>();
	if (!Stats || !Effects) return;

	switch (EffectType)
	{
	case EStatusEffectType::None:   break;
	case EStatusEffectType::Poison: Effects->ApplyPoisonExposure(); break;
	case EStatusEffectType::Fear:   Effects->AddFearPoints(PointsPerSecond * DeltaSeconds); break;
	case EStatusEffectType::Burn:   break; // applied on enter
	case EStatusEffectType::Frost:  Effects->AddFrostPoints(PointsPerSecond * DeltaSeconds); break;
	case EStatusEffectType::Bleed:  Effects->AddBleedPoints(PointsPerSecond * DeltaSeconds); break;
	default: break;
	}

	if (EffectType == EStatusEffectType::None && DamagePerSecond > 0.f)
	{
		Stats->ModifyHealth(-DamagePerSecond * DeltaSeconds);
	}
}

void ADamageTestVolume::ApplyToNPC(ANPCCharacter* NPC, float DeltaSeconds)
{
	if (!IsValid(NPC) || DeltaSeconds <= 0.f) return;

	if (DamagePerSecond > 0.f)
	{
		const float DamageThisTick = DamagePerSecond * DeltaSeconds;

		// NOTE: This calls NPCCharacter::TakeDamage (your override)
		UGameplayStatics::ApplyDamage(NPC, DamageThisTick, nullptr, this, nullptr);
	}
}
