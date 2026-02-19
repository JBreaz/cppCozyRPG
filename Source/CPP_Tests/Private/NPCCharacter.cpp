#include "NPCCharacter.h"

#include "NPCHealthBarWidget.h"
#include "CPP_TestsPlayerController.h"
#include "InventoryComponent.h"
#include "PlayerStatsComponent.h"

#include "AIController.h"
#include "NPCSafeZone.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/NavMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#include "ItemDataAsset.h"
#include "PickupItemActor.h"

ANPCCharacter::ANPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();

	bUseControllerRotationYaw = false;

	HealthBarComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("HealthBar"));
	HealthBarComponent->SetupAttachment(GetCapsuleComponent());
	HealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
	HealthBarComponent->SetDrawAtDesiredSize(true);
	HealthBarComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HealthBarComponent->SetTwoSided(true);
	HealthBarComponent->SetHiddenInGame(true);
	HealthBarComponent->SetVisibility(false, true);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->RotationRate = FRotator(0.0f, RotationRateYaw, 0.0f);
		MoveComp->MaxWalkSpeed = WanderSpeed;

		MoveComp->bRequestedMoveUseAcceleration = true;

		if (FNavMovementProperties* NavProps = MoveComp->GetNavMovementProperties())
		{
			NavProps->bUseAccelerationForPaths = true;
		}
	}

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(GetCapsuleComponent());
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(CubeMesh.Object);
		VisualMesh->SetWorldScale3D(FVector(0.8f));
	}

	// Default relationship thresholds if user never sets them
	RelationshipPointsToNextLevel = { 10, 15, 20, 25, 30 };

	ApplyCollisionDefaults();
	ApplyVisualDefaults();
	ApplyAnimationDefaults();
}

FText ANPCCharacter::GetNPCDisplayName() const
{
	if (!NPCDisplayName.IsEmpty())
	{
		return NPCDisplayName;
	}

#if WITH_EDITOR
	// In editor, actor label is the friendly name people actually see
	return FText::FromString(GetActorLabel());
#else
	return FText::FromString(GetName());
#endif
}

float ANPCCharacter::GetRelationshipProgress01() const
{
	if (!bIsMerchant) return 0.f;

	if (RelationshipLevel >= 5)
	{
		return 1.f;
	}

	const int32 Req = GetPointsRequiredForNextLevel(RelationshipLevel);
	if (Req <= 0) return 0.f;

	return FMath::Clamp((float)RelationshipPoints / (float)Req, 0.f, 1.f);
}

int32 ANPCCharacter::GetPointsRequiredForNextLevel(int32 Level) const
{
	if (Level < 0) Level = 0;
	if (Level >= 5) return 0;

	if (RelationshipPointsToNextLevel.Num() >= 5)
	{
		return FMath::Max(0, RelationshipPointsToNextLevel[Level]);
	}

	// fallback safety
	static const int32 Defaults[5] = { 10, 15, 20, 25, 30 };
	return Defaults[Level];
}

float ANPCCharacter::GetRaritySellMultiplier(EItemRarity Rarity) const
{
	switch (Rarity)
	{
	case EItemRarity::Garbage:    return FMath::Max(0.f, SellMultiplier_Garbage);
	case EItemRarity::Acceptable: return FMath::Max(0.f, SellMultiplier_Acceptable);
	case EItemRarity::Fair:       return FMath::Max(0.f, SellMultiplier_Fair);
	case EItemRarity::Perfect:    return FMath::Max(0.f, SellMultiplier_Perfect);
	default:                      return 1.f;
	}
}

const FPreferredItemConfig* ANPCCharacter::FindPreferredConfig(const UItemDataAsset* Item) const
{
	if (!Item) return nullptr;

	for (const FPreferredItemConfig& Cfg : PreferredItemConfigs)
	{
		if (Cfg.Item == Item)
		{
			return &Cfg;
		}
	}

	return nullptr;
}

bool ANPCCharacter::IsPreferredItem(const UItemDataAsset* Item) const
{
	if (!Item) return false;

	if (PreferredItemConfigs.Num() > 0)
	{
		return FindPreferredConfig(Item) != nullptr;
	}

	// legacy fallback
	for (const TObjectPtr<UItemDataAsset>& Pref : PreferredItems)
	{
		if (Pref == Item) return true;
	}
	return false;
}

float ANPCCharacter::GetPreferredSellMultiplier(const UItemDataAsset* Item) const
{
	if (!Item) return 1.f;

	if (const FPreferredItemConfig* Cfg = FindPreferredConfig(Item))
	{
		return FMath::Max(0.f, Cfg->SellMultiplier);
	}

	// legacy fallback
	return IsPreferredItem(Item) ? FMath::Max(0.f, PreferredItemSellMultiplier) : 1.f;
}

int32 ANPCCharacter::GetPreferredRelationshipPointsPerUnit(const UItemDataAsset* Item) const
{
	if (!Item) return 0;

	if (const FPreferredItemConfig* Cfg = FindPreferredConfig(Item))
	{
		return FMath::Max(0, Cfg->RelationshipPointsPerUnit);
	}

	// legacy list had no relationship points
	return 0;
}

int32 ANPCCharacter::GetResaleBuyPricePerUnit(const UItemDataAsset* Item) const
{
	if (!Item) return 0;

	const int32 Base = FMath::Max(0, Item->BaseSellValue);
	if (Base <= 0) return 0;

	const float Mult = FMath::Max(1.f, ResaleBuyPriceMultiplier);
	return FMath::Max(1, FMath::RoundToInt((float)Base * Mult));
}

void ANPCCharacter::ModifyMerchantCurrency(int32 Delta)
{
	if (!bIsMerchant) return;

	const int32 NewVal = CurrentCurrency + Delta;
	CurrentCurrency = FMath::Clamp(NewVal, 0, MaxCurrency);
}

void ANPCCharacter::AddResaleStock(UItemDataAsset* Item, int32 Quantity)
{
	if (!bIsMerchant || !Item || Quantity <= 0) return;

	// If merchant already sells it, just add stock (unless infinite)
	const int32 Index = FindMerchantEntryIndex_Runtime(Item);
	if (Index != INDEX_NONE)
	{
		FMerchantInventoryEntry& Entry = MerchantInventoryRuntime[Index];
		if (Entry.bInfiniteStock)
		{
			// Already infinite, nothing to add
			return;
		}

		Entry.Stock = FMath::Max(0, Entry.Stock + Quantity);
		return;
	}

	// Create a resale entry
	FMerchantInventoryEntry NewEntry;
	NewEntry.Item = Item;
	NewEntry.bInfiniteStock = false;
	NewEntry.Stock = Quantity;
	NewEntry.MinRelationship = FMath::Clamp(ResaleMinRelationshipLevel, 0, 5);
	NewEntry.BuyPrice = GetResaleBuyPricePerUnit(Item);

	MerchantInventoryRuntime.Add(NewEntry);
}

void ANPCCharacter::ConsumeMerchantStock(UItemDataAsset* Item, int32 Quantity)
{
	if (!bIsMerchant || !Item || Quantity <= 0) return;

	const int32 Index = FindMerchantEntryIndex_Runtime(Item);
	if (Index == INDEX_NONE) return;

	FMerchantInventoryEntry& Entry = MerchantInventoryRuntime[Index];
	if (Entry.bInfiniteStock) return;

	Entry.Stock = FMath::Max(0, Entry.Stock - Quantity);
}

void ANPCCharacter::AwardRelationshipForSale(UItemDataAsset* Item, int32 Quantity)
{
	if (!bIsMerchant || !Item || Quantity <= 0) return;

	const int32 PtsPerUnit = GetPreferredRelationshipPointsPerUnit(Item);
	if (PtsPerUnit <= 0) return;

	RelationshipPoints += (PtsPerUnit * Quantity);
	RelationshipPoints = FMath::Max(0, RelationshipPoints);

	// Level up as long as we have enough points
	while (RelationshipLevel < 5)
	{
		const int32 Req = GetPointsRequiredForNextLevel(RelationshipLevel);
		if (Req <= 0) break;

		if (RelationshipPoints >= Req)
		{
			RelationshipPoints -= Req;

			const int32 OldLevel = RelationshipLevel;
			RelationshipLevel = FMath::Clamp(RelationshipLevel + 1, 0, 5);

			if (RelationshipLevel != OldLevel)
			{
				OnMerchantRelationshipChanged.Broadcast(this, RelationshipLevel);
			}
		}
		else
		{
			break;
		}
	}
}

void ANPCCharacter::ApplyCollisionDefaults()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionProfileName(TEXT("Pawn"));
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	}
}

void ANPCCharacter::ApplyVisualDefaults()
{
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();

		if (USkeletalMeshComponent* SkelMesh = GetMesh())
		{
			SkelMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight + SkeletalMeshZOffsetExtra));
			SkelMesh->SetRelativeRotation(FRotator(0.0f, SkeletalMeshYawOffset, 0.0f));
		}

		if (VisualMesh)
		{
			VisualMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfHeight + 50.0f));
			VisualMesh->SetVisibility(bUsePlaceholderMesh, true);
		}
	}
}

void ANPCCharacter::ApplyAnimationDefaults()
{
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		if (NPCAnimBlueprintClass)
		{
			SkelMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			SkelMesh->SetAnimInstanceClass(NPCAnimBlueprintClass);
		}

		if (bAlwaysTickAnimation)
		{
			SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}
}

// -------------------------
// Low-health speed helpers
// -------------------------
float ANPCCharacter::GetHealthPercent01() const
{
	if (!bHealthInitialized) return 1.0f;
	if (MaxHealth <= 0.0f) return 1.0f;
	return FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f);
}

float ANPCCharacter::GetLowHealthMoveMultiplier() const
{
	if (bIsDead) return 1.0f;

	const float Pct = GetHealthPercent01();
	const float Threshold = FMath::Clamp(LowHealthSpeedThreshold, 0.0f, 1.0f);

	if (Pct <= Threshold)
	{
		return FMath::Max(0.0f, LowHealthMoveSpeedMultiplier);
	}

	return 1.0f;
}

void ANPCCharacter::CancelSpeedRamp()
{
	if (bSpeedRamping)
	{
		bSpeedRamping = false;
		SetActorTickEnabled(false);
	}
}

void ANPCCharacter::ReapplyMoveSpeedFromLastRequest()
{
	if (bIsDead) return;

	CancelSpeedRamp();

	if (LastRequestedBaseSpeed <= 0.0f)
	{
		const bool bReactionMode = (CurrentMode == ENPCMode::Chase) || (CurrentMode == ENPCMode::Flee);
		LastRequestedBaseSpeed = bReactionMode ? MaxReactionSpeed : WanderSpeed;
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = LastRequestedBaseSpeed * GetLowHealthMoveMultiplier();
	}
}

void ANPCCharacter::InitializeRuntimeState()
{
	// Enforce: NPC is either Enemy, Merchant, or Neutral (not both)
	if (bIsMerchant && bIsAggressive)
	{
		UE_LOG(LogTemp, Warning, TEXT("NPC '%s' has bIsMerchant and bIsAggressive set. Merchant wins; disabling aggression."), *GetName());
		bIsAggressive = false;
	}

	MaxHealth = FMath::Max(1.0f, MaxHealth);

	if (CurrentHealth <= 0.0f)
	{
		CurrentHealth = MaxHealth;
	}
	CurrentHealth = FMath::Clamp(CurrentHealth, 0.0f, MaxHealth);

	bIsDead = (CurrentHealth <= 0.0f);

	// Ensure relationship thresholds valid
	if (RelationshipPointsToNextLevel.Num() < 5)
	{
		RelationshipPointsToNextLevel = { 10, 15, 20, 25, 30 };
	}

	if (bIsMerchant)
	{
		RelationshipLevel = FMath::Clamp(RelationshipLevel, 0, 5);
		MaxCurrency = FMath::Max(0, MaxCurrency);

		if (CurrentCurrency <= 0)
		{
			CurrentCurrency = MaxCurrency;
		}
		CurrentCurrency = FMath::Clamp(CurrentCurrency, 0, MaxCurrency);

		RelationshipPoints = FMath::Max(0, RelationshipPoints);

		MerchantInventoryRuntime.Reset();
		if (MerchantInventoryData)
		{
			MerchantInventoryRuntime = MerchantInventoryData->Entries;
		}
	}
	else
	{
		CurrentCurrency = 0;
		RelationshipPoints = 0;
		MerchantInventoryRuntime.Reset();
	}

	bHealthInitialized = true;

	if (LastRequestedBaseSpeed <= 0.0f)
	{
		LastRequestedBaseSpeed = WanderSpeed;
	}

	ReapplyMoveSpeedFromLastRequest();
}

void ANPCCharacter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->RotationRate = FRotator(0.0f, RotationRateYaw, 0.0f);
		MoveComp->bRequestedMoveUseAcceleration = true;

		if (FNavMovementProperties* NavProps = MoveComp->GetNavMovementProperties())
		{
			NavProps->bUseAccelerationForPaths = true;
		}
	}

	if (HealthBarComponent)
	{
		HealthBarComponent->SetRelativeLocation(HealthBarWorldOffset);

		if (HealthBarWidgetClass)
		{
			HealthBarComponent->SetWidgetClass(HealthBarWidgetClass);
			HealthBarComponent->InitWidget();
		}

		HealthBarComponent->SetHiddenInGame(true);
		HealthBarComponent->SetVisibility(false, true);
	}

	InitializeRuntimeState();

	ApplyCollisionDefaults();
	ApplyVisualDefaults();
	ApplyAnimationDefaults();

	if (LastRegisteredZone.IsValid() && LastRegisteredZone.Get() != SafeZone)
	{
		LastRegisteredZone->UnregisterNPC(this);
		LastRegisteredZone = nullptr;
	}

	if (IsValid(SafeZone))
	{
		SafeZone->RegisterNPC(this);
		LastRegisteredZone = SafeZone;
	}
}

void ANPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	HomeLocation = GetActorLocation();
	InitializeRuntimeState();

	if (UWorld* World = GetWorld())
	{
		LastDamageTimeSeconds = World->GetTimeSeconds();
	}
	else
	{
		LastDamageTimeSeconds = 0.0f;
	}

	if (HealthBarComponent)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (ULocalPlayer* LP = PC->GetLocalPlayer())
			{
				HealthBarComponent->SetOwnerPlayer(LP);
			}
		}

		if (HealthBarWidgetClass)
		{
			HealthBarComponent->SetWidgetClass(HealthBarWidgetClass);
			HealthBarComponent->InitWidget();
		}

		HealthBarComponent->SetHiddenInGame(true);
		HealthBarComponent->SetVisibility(false, true);
	}

	if (GetWorld())
	{
		NextWanderAllowedTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(WanderWaitMin, WanderWaitMax);
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bRequestedMoveUseAcceleration = true;

		if (FNavMovementProperties* NavProps = MoveComp->GetNavMovementProperties())
		{
			NavProps->bUseAccelerationForPaths = true;
		}
	}

	ApplyAnimationDefaults();

	GetWorldTimerManager().SetTimer(
		BrainTimerHandle,
		this,
		&ANPCCharacter::BrainTick,
		BrainTickSeconds,
		true
	);
}

void ANPCCharacter::ShowHealthBarNow()
{
	if (!HealthBarComponent || bIsDead) return;

	if (!HealthBarComponent->GetUserWidgetObject() && HealthBarWidgetClass)
	{
		HealthBarComponent->SetWidgetClass(HealthBarWidgetClass);
		HealthBarComponent->InitWidget();
	}

	UNPCHealthBarWidget* W = Cast<UNPCHealthBarWidget>(HealthBarComponent->GetUserWidgetObject());
	if (!W) return;

	HealthBarComponent->SetHiddenInGame(false);
	HealthBarComponent->SetVisibility(true, true);

	const float Pct = (MaxHealth > 0.f) ? (CurrentHealth / MaxHealth) : 0.f;
	W->SetHealthPercent(Pct);
	W->ShowInstant();

	GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);
	GetWorldTimerManager().SetTimer(
		HealthBarHideTimerHandle,
		this,
		&ANPCCharacter::FadeHealthBar,
		HealthBarHideDelaySeconds,
		false
	);
}

void ANPCCharacter::FadeHealthBar()
{
	if (!HealthBarComponent || bIsDead) return;

	if (UNPCHealthBarWidget* W = Cast<UNPCHealthBarWidget>(HealthBarComponent->GetUserWidgetObject()))
	{
		W->PlayFadeOut();
	}
}

void ANPCCharacter::EnterRagdoll(AActor* DamageCauser)
{
	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (!SkelMesh) return;

	if (!SkelMesh->GetPhysicsAsset())
	{
		UE_LOG(LogTemp, Warning, TEXT("NPC '%s' has no PhysicsAsset; ragdoll skipped."), *GetName());
		return;
	}

	SkelMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	SkelMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	SkelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	SkelMesh->SetSimulatePhysics(true);
	SkelMesh->SetAllBodiesSimulatePhysics(true);
	SkelMesh->WakeAllRigidBodies();
	SkelMesh->bBlendPhysics = true;

	if (RagdollImpulseStrength > 0.0f && IsValid(DamageCauser))
	{
		const FVector Dir = (SkelMesh->GetComponentLocation() - DamageCauser->GetActorLocation()).GetSafeNormal();
		SkelMesh->AddImpulse(Dir * RagdollImpulseStrength, NAME_None, true);
	}
}

void ANPCCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bSpeedRamping || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const float Alpha = FMath::Clamp((Now - RampStartTime) / FMath::Max(RampDuration, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	const float NewSpeedActual = FMath::Lerp(RampStartSpeed, RampTargetSpeed, Alpha);

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = NewSpeedActual;
	}

	if (Alpha >= 1.0f)
	{
		bSpeedRamping = false;
		SetActorTickEnabled(false);
	}
}

void ANPCCharacter::BeginInteractionPause(AActor* Interactor)
{
	if (!GetWorld())
	{
		return;
	}

	InteractionFaceTarget = Interactor;
	InteractionPauseUntilTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, InteractionFaceSeconds);

	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
		if (Interactor)
		{
			AIC->SetFocus(Interactor);
		}
	}
}

void ANPCCharacter::EndInteractionPause()
{
	InteractionPauseUntilTime = -1.0f;
	InteractionFaceTarget = nullptr;

	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

void ANPCCharacter::UpdateFaceTarget(float DeltaSeconds)
{
	if (!InteractionFaceTarget.IsValid())
	{
		return;
	}

	const FVector MyLoc = GetActorLocation();
	FVector To = InteractionFaceTarget->GetActorLocation() - MyLoc;
	To.Z = 0.f;

	if (To.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FRotator Desired(0.f, To.Rotation().Yaw, 0.f);
	const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), Desired, DeltaSeconds, FMath::Max(0.f, InteractionFaceInterpSpeed));
	SetActorRotation(NewRot);
}

void ANPCCharacter::Interact_Implementation(AActor* Interactor)
{
	if (!bIsInteractable)
	{
		return;
	}

	// Enemies do not "talk" and should NOT be paused by interact.
	if (IsEnemy())
	{
		return;
	}

	BeginInteractionPause(Interactor);

	if (bIsMerchant)
	{
		if (RelationshipLevel <= 0)
		{
			const TCHAR* Msg = TEXT("I don't serve you.");
			UE_LOG(LogTemp, Warning, TEXT("%s"), Msg);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, Msg);
			}
			return;
		}

		// Open player menu with this merchant context
		if (APawn* Pawn = Cast<APawn>(Interactor))
		{
			if (ACPP_TestsPlayerController* PC = Cast<ACPP_TestsPlayerController>(Pawn->GetController()))
			{
				PC->OpenMenuWithMerchant(this);
			}
		}

		// Optional: quick-sell test behavior (off by default)
		if (bQuickSellAllOnInteract)
		{
			int32 TotalPaid = 0;
			int32 StacksSold = 0;
			if (QuickSellAllFromPlayer(Interactor, TotalPaid, StacksSold))
			{
				const FString Msg = FString::Printf(TEXT("Sold %d stacks for %d"), StacksSold, TotalPaid);
				UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, Msg);
				}
			}
			else
			{
				const TCHAR* Msg = TEXT("Nothing I can buy (or I'm broke).");
				UE_LOG(LogTemp, Log, TEXT("%s"), Msg);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, Msg);
				}
			}
		}

		OnMerchantInteracted.Broadcast(this, Interactor);
		BP_OnMerchantInteracted(Interactor);
		return;
	}

	const TCHAR* Msg = TEXT("Hello, Stranger");
	UE_LOG(LogTemp, Warning, TEXT("%s"), Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, Msg);
	}
}

bool ANPCCharacter::QuickSellAllFromPlayer(AActor* Interactor, int32& OutTotalPaid, int32& OutStacksSold)
{
	OutTotalPaid = 0;
	OutStacksSold = 0;

	if (!CanTrade() || !Interactor)
	{
		return false;
	}

	APawn* Pawn = Cast<APawn>(Interactor);
	if (!Pawn)
	{
		return false;
	}

	UInventoryComponent* Inv = Pawn->FindComponentByClass<UInventoryComponent>();
	UPlayerStatsComponent* Stats = Pawn->FindComponentByClass<UPlayerStatsComponent>();
	if (!Inv || !Stats)
	{
		return false;
	}

	const TArray<FItemStack>& Items = Inv->GetItems();
	if (Items.Num() == 0)
	{
		return false;
	}

	// Snapshot because we'll mutate inventory while iterating.
	const TArray<FItemStack> Snapshot = Items;

	for (const FItemStack& Stack : Snapshot)
	{
		if (!Stack.Item || Stack.Quantity <= 0) continue;

		const int32 Value = GetSellValueForItemRarity(Stack.Item, Stack.Quantity, Stack.Rarity);
		if (Value <= 0) continue;

		// Strict rule: only buy if we can afford the full stack.
		if (CurrentCurrency < Value) continue;

		ModifyMerchantCurrency(-Value);
		Stats->ModifyCurrency(Value);
		Inv->RemoveItemExact(Stack.Item, Stack.Quantity, Stack.Rarity);

		// Resale + relationship points
		AddResaleStock(Stack.Item, Stack.Quantity);
		AwardRelationshipForSale(Stack.Item, Stack.Quantity);

		OutTotalPaid += Value;
		OutStacksSold += 1;
	}

	return OutTotalPaid > 0;
}

void ANPCCharacter::ApplyDamageSimple(float Damage, AActor* DamageCauser, AController* DamageInstigator)
{
	if (Damage <= 0.0f) return;
	UGameplayStatics::ApplyDamage(this, Damage, DamageInstigator, DamageCauser, nullptr);
}

float ANPCCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (bIsDead)
	{
		return 0.0f;
	}

	const float Actual = FMath::Max(0.0f, DamageAmount);
	if (Actual <= 0.0f)
	{
		return 0.0f;
	}

	if (UWorld* World = GetWorld())
	{
		LastDamageTimeSeconds = World->GetTimeSeconds();
	}

	CurrentHealth = FMath::Clamp(CurrentHealth - Actual, 0.0f, MaxHealth);

	ReapplyMoveSpeedFromLastRequest();

	OnNPCDamaged.Broadcast(this, Actual, DamageCauser);
	BP_OnDamaged(Actual, DamageCauser);

	ShowHealthBarNow();

	if (bIsImmortal)
	{
		if (CurrentHealth <= 0.0f)
		{
			CurrentHealth = 1.0f;
		}
		return Actual;
	}

	if (CurrentHealth <= 0.0f && !bIsDead)
	{
		HandleDeath(DamageCauser);
	}

	return Actual;
}

void ANPCCharacter::HandleDeath(AActor* Killer)
{
	bIsDead = true;
	CurrentHealth = 0.0f;

	CancelSpeedRamp();
	GetWorldTimerManager().ClearTimer(BrainTimerHandle);

	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
		AIC->UnPossess();
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (HealthBarComponent)
	{
		GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);
		HealthBarComponent->SetHiddenInGame(true);
		HealthBarComponent->SetVisibility(false, true);
	}

	if (bRagdollOnDeath)
	{
		EnterRagdoll(Killer);
	}

	SpawnDrops();

	OnNPCDied.Broadcast(this, Killer);
	BP_OnDied(Killer);

	if (bDestroyOnDeath)
	{
		SetLifeSpan(FMath::Max(0.01f, DestroyDelaySeconds));
	}
}

void ANPCCharacter::SpawnDrops()
{
	if (!GetWorld() || DropsOnDeath.Num() == 0)
	{
		return;
	}

	const FVector BaseLoc = GetActorLocation() + FVector(0, 0, DropSpawnZOffset);

	for (TSubclassOf<APickupItemActor> DropClass : DropsOnDeath)
	{
		if (!DropClass)
		{
			continue;
		}

		const FVector Rand2D = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		const FVector Offset = Rand2D * FMath::FRandRange(0.f, DropScatterRadius);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		GetWorld()->SpawnActor<APickupItemActor>(
			DropClass,
			BaseLoc + Offset,
			FRotator::ZeroRotator,
			Params
		);
	}
}

// -------------------------
// LockOnTargetable
// -------------------------
FVector ANPCCharacter::GetLockOnWorldLocation_Implementation() const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return GetActorLocation();
	}

	const float Half = Capsule->GetScaledCapsuleHalfHeight();
	const float Z = Half * FMath::Clamp(LockOnAimHeightRatio, 0.0f, 1.0f);

	FVector Loc = GetActorLocation();
	Loc.Z += Z;
	return Loc;
}

bool ANPCCharacter::IsLockOnAllowed_Implementation() const
{
	// Lock-on works for ALL NPCs (merchant, enemy, neutral), as long as they aren't dead.
	return !bIsDead;
}

// -------------------------
// AI helpers (unchanged logic + auto-restore)
// -------------------------
bool ANPCCharacter::IsAIMoving(const AAIController* AIC) const
{
	if (!AIC)
	{
		return false;
	}
	return AIC->GetMoveStatus() == EPathFollowingStatus::Moving;
}

bool ANPCCharacter::IsPlayerInReactionRange(const APawn* PlayerPawn) const
{
	if (!IsValid(PlayerPawn))
	{
		return false;
	}
	return FVector::Dist2D(GetActorLocation(), PlayerPawn->GetActorLocation()) <= ReactionRange;
}

bool ANPCCharacter::CanNoticePlayerCone(const APawn* PlayerPawn) const
{
	if (!IsValid(PlayerPawn))
	{
		return false;
	}

	const FVector MyLoc = GetActorLocation();
	const FVector PlayerLoc = PlayerPawn->GetActorLocation();

	const FVector ToPlayer2D(PlayerLoc.X - MyLoc.X, PlayerLoc.Y - MyLoc.Y, 0.0f);
	const float Dist2D = ToPlayer2D.Size();

	if (Dist2D > ReactionRange || Dist2D <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector Forward2D(GetActorForwardVector().X, GetActorForwardVector().Y, 0.0f);
	const FVector ForwardN = Forward2D.GetSafeNormal();
	const FVector DirN = ToPlayer2D.GetSafeNormal();

	const float HalfAngleRad = FMath::DegreesToRadians(NoticeFOVDegrees * 0.5f);
	const float CosThreshold = FMath::Cos(HalfAngleRad);

	return FVector::DotProduct(ForwardN, DirN) >= CosThreshold;
}

void ANPCCharacter::UpdateLoseInterestTimer(bool bPlayerInRange)
{
	if (!GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (bPlayerInRange)
	{
		ClearLoseInterestTimer();
		return;
	}

	if (CurrentMode != ENPCMode::Chase && CurrentMode != ENPCMode::Flee)
	{
		ClearLoseInterestTimer();
		return;
	}

	if (OutOfRangeStartTime < 0.0f)
	{
		OutOfRangeStartTime = Now;
		return;
	}

	if ((Now - OutOfRangeStartTime) >= LoseInterestSeconds)
	{
		CurrentMode = ENPCMode::ReturnHome;
		ClearLoseInterestTimer();
		ResetReturnHomeCache();
	}
}

void ANPCCharacter::ClearLoseInterestTimer()
{
	OutOfRangeStartTime = -1.0f;
}

FVector ANPCCharacter::GetHomeCenter() const
{
	return IsValid(SafeZone) ? SafeZone->GetActorLocation() : HomeLocation;
}

void ANPCCharacter::SetSpeedImmediate(float Speed)
{
	LastRequestedBaseSpeed = Speed;

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = Speed * GetLowHealthMoveMultiplier();
	}
}

void ANPCCharacter::StartSpeedRampTo(float TargetSpeed, float DurationSeconds, bool bFromZero)
{
	if (!GetWorld())
	{
		return;
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		LastRequestedBaseSpeed = TargetSpeed;

		RampStartTime = GetWorld()->GetTimeSeconds();
		RampDuration = DurationSeconds;

		RampStartSpeed = bFromZero ? 0.0f : MoveComp->MaxWalkSpeed;
		RampTargetSpeed = TargetSpeed * GetLowHealthMoveMultiplier();

		bSpeedRamping = true;
		SetActorTickEnabled(true);

		MoveComp->MaxWalkSpeed = RampStartSpeed;
	}
}

void ANPCCharacter::TryAutoRestoreHealth(float NowSeconds)
{
	if (!bAutoRestoreHealthWhenCalm) return;
	if (bIsDead) return;
	if (bIsImmortal) return;

	if (MaxHealth <= 0.0f) return;
	if (CurrentHealth >= MaxHealth) return;

	if (CurrentMode == ENPCMode::Chase || CurrentMode == ENPCMode::Flee)
	{
		return;
	}

	const float Delay = FMath::Max(0.0f, RestoreHealthDelaySeconds);
	if ((NowSeconds - LastDamageTimeSeconds) < Delay)
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	const bool bThreat =
		IsValid(PlayerPawn) &&
		(IsPlayerInReactionRange(PlayerPawn) || CanNoticePlayerCone(PlayerPawn));

	if (bThreat)
	{
		return;
	}

	CurrentHealth = MaxHealth;
	ReapplyMoveSpeedFromLastRequest();

	if (HealthBarComponent)
	{
		GetWorldTimerManager().ClearTimer(HealthBarHideTimerHandle);

		if (UNPCHealthBarWidget* W = Cast<UNPCHealthBarWidget>(HealthBarComponent->GetUserWidgetObject()))
		{
			W->SetHealthPercent(1.0f);
		}

		HealthBarComponent->SetHiddenInGame(true);
		HealthBarComponent->SetVisibility(false, true);
	}
}

bool ANPCCharacter::FindFleeDestination(APawn* PlayerPawn, FVector& OutDest) const
{
	if (!IsValid(PlayerPawn))
	{
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys)
	{
		return false;
	}

	const FVector MyLoc = GetActorLocation();
	const FVector PlayerLoc = PlayerPawn->GetActorLocation();

	FVector AwayDir = (MyLoc - PlayerLoc);
	AwayDir.Z = 0.0f;

	if (AwayDir.SizeSquared() <= FMath::Square(10.0f))
	{
		AwayDir = FMath::VRand();
		AwayDir.Z = 0.0f;
	}

	AwayDir = AwayDir.GetSafeNormal();
	if (AwayDir.IsNearlyZero())
	{
		return false;
	}

	const float HalfJitter = FleeAngleJitterDegrees * 0.5f;

	for (int32 i = 0; i < FleeSampleTries; ++i)
	{
		const float Angle = FMath::FRandRange(-HalfJitter, HalfJitter);
		const FVector RotDir = AwayDir.RotateAngleAxis(Angle, FVector::UpVector);

		const FVector Desired = MyLoc + RotDir * FleeDistance;

		FNavLocation NavLoc;
		if (NavSys->GetRandomReachablePointInRadius(Desired, FleeNavSearchRadius, NavLoc))
		{
			OutDest = NavLoc.Location;
			return true;
		}
	}

	FNavLocation NavLoc;
	if (NavSys->GetRandomReachablePointInRadius(MyLoc, FleeDistance, NavLoc))
	{
		OutDest = NavLoc.Location;
		return true;
	}

	return false;
}

bool ANPCCharacter::IsInsideSafeZone2D() const
{
	if (!IsValid(SafeZone))
	{
		return false;
	}

	const float Dist = FVector::Dist2D(GetActorLocation(), SafeZone->GetActorLocation());
	return Dist <= SafeZone->GetZoneRadius();
}

void ANPCCharacter::ResetReturnHomeCache()
{
	bHasReturnTarget = false;
	CachedReturnTarget = FVector::ZeroVector;
	LastReturnTargetPickTime = -1000.0f;
}

void ANPCCharacter::BrainTick()
{
	if (bIsDead)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	TryAutoRestoreHealth(Now);

	// Pause AI and face the interactor for a short time after interacting
	if (InteractionPauseUntilTime > 0.f)
	{
		if (Now >= InteractionPauseUntilTime)
		{
			EndInteractionPause();
		}
		else
		{
			UpdateFaceTarget(BrainTickSeconds);
			return;
		}
	}

	if (bIsStationary)
	{
		return;
	}

	AAIController* AIC = Cast<AAIController>(GetController());
	if (!AIC)
	{
		return;
	}

	const bool bMovingNow = IsAIMoving(AIC);

	const float Speed2D = GetVelocity().Size2D();
	if ((CurrentMode == ENPCMode::Wander || CurrentMode == ENPCMode::ReturnHome) && bMovingNow && Speed2D < 3.0f)
	{
		if (StuckStartTime < 0.0f)
		{
			StuckStartTime = Now;
		}
		else if ((Now - StuckStartTime) >= StuckAbortSeconds)
		{
			AIC->StopMovement();
			StuckStartTime = -1.0f;
		}
	}
	else
	{
		StuckStartTime = -1.0f;
	}

	if (bWasMovingLastTick && !bMovingNow && CurrentMode == ENPCMode::Wander)
	{
		NextWanderAllowedTime = Now + FMath::FRandRange(WanderWaitMin, WanderWaitMax);
	}
	bWasMovingLastTick = bMovingNow;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);

	const bool bCanNotice = CanNoticePlayerCone(PlayerPawn);
	const bool bInRange = IsPlayerInReactionRange(PlayerPawn);

	if ((CurrentMode == ENPCMode::Wander || CurrentMode == ENPCMode::ReturnHome) && bCanNotice)
	{
		ClearLoseInterestTimer();

		if (bIsAggressive && IsValid(PlayerPawn))
		{
			CurrentMode = ENPCMode::Chase;
		}
		else if (bIsScaredOfPlayer && IsValid(PlayerPawn))
		{
			CurrentMode = ENPCMode::Flee;
		}
	}

	UpdateLoseInterestTimer(bInRange);

	const bool bCanRepathNow = (Now - LastReactionMoveTime) >= ReactionRepathInterval;

	if (CurrentMode == ENPCMode::Chase)
	{
		if (bCanRepathNow && IsValid(PlayerPawn))
		{
			LastReactionMoveTime = Now;
			ChasePlayer(AIC, PlayerPawn);
		}
		return;
	}

	if (CurrentMode == ENPCMode::Flee)
	{
		if (bCanRepathNow && IsValid(PlayerPawn))
		{
			LastReactionMoveTime = Now;
			FleeFromPlayer(AIC, PlayerPawn);
		}
		return;
	}

	if (CurrentMode == ENPCMode::ReturnHome)
	{
		ReturnHome(AIC);

		if (CurrentMode == ENPCMode::Wander)
		{
			Wander(AIC);
		}
		return;
	}

	CurrentMode = ENPCMode::Wander;
	Wander(AIC);
}

void ANPCCharacter::ChasePlayer(AAIController* AIC, APawn* PlayerPawn)
{
	if (!AIC || !PlayerPawn) return;

	SetSpeedImmediate(MaxReactionSpeed);
	AIC->MoveToActor(PlayerPawn, ChaseAcceptanceRadius);
}

void ANPCCharacter::FleeFromPlayer(AAIController* AIC, APawn* PlayerPawn)
{
	if (!AIC || !PlayerPawn) return;

	SetSpeedImmediate(MaxReactionSpeed);

	FVector Dest;
	if (FindFleeDestination(PlayerPawn, Dest))
	{
		AIC->MoveToLocation(Dest, 80.0f);
	}
}

void ANPCCharacter::ReturnHome(AAIController* AIC)
{
	if (!AIC || !GetWorld()) return;

	SetSpeedImmediate(WanderSpeed);

	if (IsInsideSafeZone2D())
	{
		AIC->StopMovement();

		CurrentMode = ENPCMode::Wander;
		ClearLoseInterestTimer();
		ResetReturnHomeCache();

		const float Now = GetWorld()->GetTimeSeconds();
		NextWanderAllowedTime = Now + FMath::FRandRange(WanderWaitMin, WanderWaitMax);

		bWasMovingLastTick = false;
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	const float RePickCooldown = 0.75f;

	if (!bHasReturnTarget || (Now - LastReturnTargetPickTime) > RePickCooldown)
	{
		bool bGot = false;

		if (IsValid(SafeZone))
		{
			const float RadiusToUse = FMath::Min(SafeZone->GetZoneRadius(), FMath::Max(200.0f, WanderRadius));
			bGot = SafeZone->GetRandomReachablePointInZone(CachedReturnTarget, RadiusToUse);
		}

		if (!bGot)
		{
			if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
			{
				FNavLocation NavLoc;
				if (NavSys->GetRandomReachablePointInRadius(HomeLocation, WanderRadius, NavLoc))
				{
					CachedReturnTarget = NavLoc.Location;
					bGot = true;
				}
			}
		}

		if (bGot)
		{
			bHasReturnTarget = true;
			LastReturnTargetPickTime = Now;
		}
	}

	if (bHasReturnTarget && !IsAIMoving(AIC))
	{
		AIC->MoveToLocation(CachedReturnTarget, ReturnHomeAcceptanceRadius);
	}

	if (bHasReturnTarget && !IsAIMoving(AIC))
	{
		const float DistToTarget = FVector::Dist2D(GetActorLocation(), CachedReturnTarget);
		if (DistToTarget <= FMath::Max(ReturnHomeAcceptanceRadius * 2.0f, 200.0f))
		{
			CurrentMode = ENPCMode::Wander;
			ResetReturnHomeCache();
			NextWanderAllowedTime = Now + FMath::FRandRange(WanderWaitMin, WanderWaitMax);
		}
	}
}

void ANPCCharacter::Wander(AAIController* AIC)
{
	if (!AIC || !GetWorld()) return;

	const float Now = GetWorld()->GetTimeSeconds();

	if (IsAIMoving(AIC)) return;
	if (Now < NextWanderAllowedTime) return;

	FVector Dest = FVector::ZeroVector;
	bool bHasDest = false;

	if (IsValid(SafeZone))
	{
		const float RadiusToUse = FMath::Min(WanderRadius, SafeZone->GetZoneRadius());
		bHasDest = SafeZone->GetRandomReachablePointInZone(Dest, RadiusToUse);
	}

	if (!bHasDest)
	{
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
		{
			FNavLocation NavLoc;
			if (NavSys->GetRandomReachablePointInRadius(HomeLocation, WanderRadius, NavLoc))
			{
				Dest = NavLoc.Location;
				bHasDest = true;
			}
		}
	}

	if (!bHasDest)
	{
		NextWanderAllowedTime = Now + 0.35f;
		return;
	}

	StartSpeedRampTo(WanderSpeed, WanderRampSeconds, true);
	AIC->MoveToLocation(Dest, WanderAcceptanceRadius);
}

// -------------------------
// Merchant helpers
// -------------------------
int32 ANPCCharacter::FindMerchantEntryIndex_Runtime(const UItemDataAsset* Item) const
{
	if (!Item) return INDEX_NONE;

	for (int32 i = 0; i < MerchantInventoryRuntime.Num(); ++i)
	{
		if (MerchantInventoryRuntime[i].Item == Item)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 ANPCCharacter::GetSellValueForItem(const UItemDataAsset* Item, int32 Quantity) const
{
	// Keep old API behavior: treat as Acceptable
	return GetSellValueForItemRarity(Item, Quantity, EItemRarity::Acceptable);
}

int32 ANPCCharacter::GetSellValueForItemRarity(const UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity) const
{
	if (!Item || Quantity <= 0) return 0;

	const int32 BaseUnit = FMath::Max(0, Item->BaseSellValue);
	if (BaseUnit <= 0) return 0;

	float Total = (float)BaseUnit * (float)Quantity;

	// Apply rarity multiplier
	Total *= GetRaritySellMultiplier(Rarity);

	// Apply preferred multiplier (per item if configured)
	Total *= GetPreferredSellMultiplier(Item);

	return FMath::Max(1, FMath::RoundToInt(Total));
}

TArray<FMerchantInventoryEntry> ANPCCharacter::GetUnlockedMerchantInventory() const
{
	TArray<FMerchantInventoryEntry> Out;

	if (!bIsMerchant || RelationshipLevel <= 0)
	{
		return Out;
	}

	for (const FMerchantInventoryEntry& Entry : MerchantInventoryRuntime)
	{
		if (!Entry.Item) continue;
		if (RelationshipLevel < Entry.MinRelationship) continue;
		if (!Entry.bInfiniteStock && Entry.Stock <= 0) continue;

		Out.Add(Entry);
	}

	return Out;
}

bool ANPCCharacter::TryBuyFromPlayer(UItemDataAsset* Item, int32 Quantity, int32& OutPaidAmount)
{
	OutPaidAmount = 0;

	if (!CanTrade() || !Item || Quantity <= 0)
	{
		return false;
	}

	const int32 Value = GetSellValueForItem(Item, Quantity);
	if (Value <= 0) return false;

	const int32 Paid = FMath::Min(Value, CurrentCurrency);
	if (Paid <= 0) return false;

	ModifyMerchantCurrency(-Paid);
	OutPaidAmount = Paid;
	return true;
}

bool ANPCCharacter::TrySellToPlayer(UItemDataAsset* Item, int32 Quantity, int32& OutCost)
{
	OutCost = 0;

	if (!CanTrade() || !Item || Quantity <= 0)
	{
		return false;
	}

	const int32 Index = FindMerchantEntryIndex_Runtime(Item);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	FMerchantInventoryEntry& Entry = MerchantInventoryRuntime[Index];

	if (RelationshipLevel < Entry.MinRelationship)
	{
		return false;
	}

	if (!Entry.bInfiniteStock && Entry.Stock < Quantity)
	{
		return false;
	}

	const int32 Cost = Entry.BuyPrice * Quantity;
	if (Cost <= 0)
	{
		return false;
	}

	if (!Entry.bInfiniteStock)
	{
		Entry.Stock = FMath::Max(0, Entry.Stock - Quantity);
	}

	ModifyMerchantCurrency(+Cost);
	OutCost = Cost;
	return true;
}

void ANPCCharacter::SetRelationshipLevel(int32 NewLevel)
{
	if (!bIsMerchant) return;

	const int32 Clamped = FMath::Clamp(NewLevel, 0, 5);
	if (Clamped == RelationshipLevel) return;

	RelationshipLevel = Clamped;

	// Reset points if you force-set a level (keeps UI sane)
	RelationshipPoints = 0;

	OnMerchantRelationshipChanged.Broadcast(this, RelationshipLevel);
}

void ANPCCharacter::ModifyRelationship(int32 Delta)
{
	SetRelationshipLevel(RelationshipLevel + Delta);
}
