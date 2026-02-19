#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Interactable.h"
#include "LockOnTargetable.h"
#include "MerchantInventoryDataAsset.h"
#include "InventoryComponent.h" // EItemRarity, FItemStack
#include "NPCCharacter.generated.h"

class AAIController;
class ANPCSafeZone;
class UStaticMeshComponent;
class UAnimInstance;
class UItemDataAsset;
class APickupItemActor;
class UNPCHealthBarWidget;
class UWidgetComponent;

class UInventoryComponent;
class UPlayerStatsComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnNPCDamaged, ANPCCharacter*, NPC, float, Damage, AActor*, DamageCauser);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCDied, ANPCCharacter*, NPC, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMerchantInteracted, ANPCCharacter*, Merchant, AActor*, Interactor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMerchantRelationshipChanged, ANPCCharacter*, Merchant, int32, NewRelationship);

USTRUCT(BlueprintType)
struct FPreferredItemConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UItemDataAsset> Item = nullptr;

	// Per-item sell multiplier (defaults to 2x)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.0"))
	float SellMultiplier = 2.0f;

	// Relationship points gained per unit sold of this item
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0"))
	int32 RelationshipPointsPerUnit = 0;
};

UCLASS()
class CPP_TESTS_API ANPCCharacter : public ACharacter, public IInteractable, public ILockOnTargetable
{
	GENERATED_BODY()

public:
	ANPCCharacter();

	// -------------------------
	// Interact
	// -------------------------
	virtual void Interact_Implementation(AActor* Interactor) override;

	// -------------------------
	// Health / Damage (ALL NPCs)
	// -------------------------
	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser
	) override;

	UFUNCTION(BlueprintCallable, Category="NPC|Health")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintCallable, Category="NPC|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, Category="NPC|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintCallable, Category="NPC|Health")
	bool IsImmortal() const { return bIsImmortal; }

	UFUNCTION(BlueprintCallable, Category="NPC|Health")
	void ApplyDamageSimple(float Damage, AActor* DamageCauser = nullptr, AController* DamageInstigator = nullptr);

	UPROPERTY(BlueprintAssignable, Category="NPC|Health")
	FOnNPCDamaged OnNPCDamaged;

	UPROPERTY(BlueprintAssignable, Category="NPC|Death")
	FOnNPCDied OnNPCDied;

	UFUNCTION(BlueprintImplementableEvent, Category="NPC|Health")
	void BP_OnDamaged(float Damage, AActor* DamageCauser);

	UFUNCTION(BlueprintImplementableEvent, Category="NPC|Death")
	void BP_OnDied(AActor* Killer);

	// -------------------------
	// Role helpers
	// -------------------------
	UFUNCTION(BlueprintCallable, Category="NPC|Role")
	bool IsEnemy() const { return bIsAggressive && !bIsMerchant; }

	UFUNCTION(BlueprintCallable, Category="NPC|Role")
	bool IsMerchant() const { return bIsMerchant; }

	UFUNCTION(BlueprintCallable, Category="NPC|Role")
	bool IsNeutral() const { return !bIsMerchant && !bIsAggressive && !bIsScaredOfPlayer; }

	// -------------------------
	// Identity (UI)
	// -------------------------
	UFUNCTION(BlueprintPure, Category="NPC|Identity")
	FText GetNPCDisplayName() const;

	UFUNCTION(BlueprintPure, Category="NPC|Merchant")
	FText GetMerchantDisplayName() const { return GetNPCDisplayName(); }

	UFUNCTION(BlueprintPure, Category="NPC|Merchant|UI")
	FLinearColor GetMerchantCurrencyTint() const { return MerchantCurrencyTint; }

	// -------------------------
	// Merchant API (all in NPC)
	// -------------------------
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	bool CanTrade() const { return bIsMerchant && RelationshipLevel > 0; }

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	int32 GetMaxCurrency() const { return MaxCurrency; }

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	int32 GetCurrentCurrency() const { return CurrentCurrency; }

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	int32 GetRelationshipLevel() const { return RelationshipLevel; }

	// 0..1 progress toward next relationship level
	UFUNCTION(BlueprintPure, Category="NPC|Merchant|Relationship")
	float GetRelationshipProgress01() const;

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void SetRelationshipLevel(int32 NewLevel);

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void ModifyRelationship(int32 Delta);

	// Existing API kept for compatibility (uses Acceptable multiplier)
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	int32 GetSellValueForItem(const UItemDataAsset* Item, int32 Quantity) const;

	// New: rarity-aware sell value (THIS is what you want for correct pricing)
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	int32 GetSellValueForItemRarity(const UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity) const;

	// Modify merchant currency (used by menu trade confirm)
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void ModifyMerchantCurrency(int32 Delta);

	// Add items sold by player into merchant runtime inventory so they show up for buying
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void AddResaleStock(UItemDataAsset* Item, int32 Quantity);

	// Consume stock when player buys from merchant
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void ConsumeMerchantStock(UItemDataAsset* Item, int32 Quantity);

	// Relationship points from selling to merchant (preferred item rules)
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	void AwardRelationshipForSale(UItemDataAsset* Item, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	bool TryBuyFromPlayer(UItemDataAsset* Item, int32 Quantity, int32& OutPaidAmount);

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	bool TrySellToPlayer(UItemDataAsset* Item, int32 Quantity, int32& OutCost);

	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	TArray<FMerchantInventoryEntry> GetUnlockedMerchantInventory() const;

	// NEW: optional quick-sell helper (off by default)
	UFUNCTION(BlueprintCallable, Category="NPC|Merchant")
	bool QuickSellAllFromPlayer(AActor* Interactor, int32& OutTotalPaid, int32& OutStacksSold);

	UPROPERTY(BlueprintAssignable, Category="NPC|Merchant")
	FOnMerchantInteracted OnMerchantInteracted;

	UPROPERTY(BlueprintAssignable, Category="NPC|Merchant")
	FOnMerchantRelationshipChanged OnMerchantRelationshipChanged;

	UFUNCTION(BlueprintImplementableEvent, Category="NPC|Merchant")
	void BP_OnMerchantInteracted(AActor* Interactor);

	// -------------------------
	// LockOnTargetable interface
	// -------------------------
	virtual FVector GetLockOnWorldLocation_Implementation() const override;
	virtual bool IsLockOnAllowed_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;

private:
	// --- Interaction / Roles ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Interaction")
	bool bIsInteractable = true;

	UPROPERTY(EditAnywhere, Category="NPC Config|Interaction", meta=(EditCondition="bIsInteractable", EditConditionHides))
	bool bIsMerchant = false;

	UPROPERTY(EditAnywhere, Category="NPC Config|Interaction", meta=(ClampMin="0.0", Units="s"))
	float InteractionFaceSeconds = 10.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Interaction", meta=(ClampMin="0.0"))
	float InteractionFaceInterpSpeed = 10.0f;

	float InteractionPauseUntilTime = -1.0f;
	TWeakObjectPtr<AActor> InteractionFaceTarget;

	void BeginInteractionPause(AActor* Interactor);
	void UpdateFaceTarget(float DeltaSeconds);
	void EndInteractionPause();

	// --- Behavior Flags ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Behavior")
	bool bIsStationary = false;

	UPROPERTY(EditAnywhere, Category="NPC Config|Behavior")
	bool bIsScaredOfPlayer = false;

	UPROPERTY(EditAnywhere, Category="NPC Config|Behavior")
	bool bIsAggressive = false;

	// --- Identity ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Identity")
	FText NPCDisplayName;

	// --- Health ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Health", meta=(ClampMin="1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, Category="NPC Runtime|Health")
	float CurrentHealth = 0.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Health")
	bool bIsImmortal = false;

	// --- Low Health Movement Penalty ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Health", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LowHealthSpeedThreshold = 0.25f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Health", meta=(ClampMin="0.0"))
	float LowHealthMoveSpeedMultiplier = 0.5f;

	// --- Auto-restore health when calm ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Health|AutoRestore")
	bool bAutoRestoreHealthWhenCalm = true;

	UPROPERTY(EditAnywhere, Category="NPC Config|Health|AutoRestore",
		meta=(EditCondition="bAutoRestoreHealthWhenCalm", ClampMin="0.0", Units="s"))
	float RestoreHealthDelaySeconds = 30.0f;

	// --- Death ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Death")
	bool bDestroyOnDeath = true;

	UPROPERTY(EditAnywhere, Category="NPC Config|Death", meta=(ClampMin="0.0", Units="s"))
	float DestroyDelaySeconds = 6.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Death")
	TArray<TSubclassOf<APickupItemActor>> DropsOnDeath;

	UPROPERTY(EditAnywhere, Category="NPC Config|Death", meta=(ClampMin="0.0", Units="cm"))
	float DropScatterRadius = 60.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Death", meta=(ClampMin="-200.0", ClampMax="200.0", Units="cm"))
	float DropSpawnZOffset = 20.0f;

	// --- Ragdoll on death ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Death")
	bool bRagdollOnDeath = true;

	UPROPERTY(EditAnywhere, Category="NPC Config|Death", meta=(EditCondition="bRagdollOnDeath", ClampMin="0.0"))
	float RagdollImpulseStrength = 0.0f;

	// --- Merchant Economy ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant", ClampMin="0"))
	int32 MaxCurrency = 500;

	UPROPERTY(VisibleInstanceOnly, Category="NPC Runtime|Merchant", meta=(EditCondition="bIsMerchant", ClampMin="0"))
	int32 CurrentCurrency = 0;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant", ClampMin="0", ClampMax="5"))
	int32 RelationshipLevel = 3;

	// Relationship points within the current level (used for progress bar)
	UPROPERTY(VisibleInstanceOnly, Category="NPC Runtime|Merchant", meta=(EditCondition="bIsMerchant", ClampMin="0"))
	int32 RelationshipPoints = 0;

	// Points required to go from level N -> N+1 (expects 5 entries for levels 0..4)
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Relationship", meta=(EditCondition="bIsMerchant"))
	TArray<int32> RelationshipPointsToNextLevel;

	// New preferred item config (includes relationship points)
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant"))
	TArray<FPreferredItemConfig> PreferredItemConfigs;

	// Legacy preferred list kept so existing merchants still work if you don't fill configs
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant"))
	TArray<TObjectPtr<UItemDataAsset>> PreferredItems;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant", ClampMin="1.0"))
	float PreferredItemSellMultiplier = 2.0f;

	// Rarity multipliers (sell value)
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Economy", meta=(EditCondition="bIsMerchant", ClampMin="0.0"))
	float SellMultiplier_Garbage = 0.5f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Economy", meta=(EditCondition="bIsMerchant", ClampMin="0.0"))
	float SellMultiplier_Acceptable = 1.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Economy", meta=(EditCondition="bIsMerchant", ClampMin="0.0"))
	float SellMultiplier_Fair = 1.5f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Economy", meta=(EditCondition="bIsMerchant", ClampMin="0.0"))
	float SellMultiplier_Perfect = 2.0f;

	// Resale behavior
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Resale", meta=(EditCondition="bIsMerchant", ClampMin="1.0"))
	float ResaleBuyPriceMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|Resale", meta=(EditCondition="bIsMerchant", ClampMin="0", ClampMax="5"))
	int32 ResaleMinRelationshipLevel = 1;

	// Optional UI tint per merchant (so you can test/configure it)
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant|UI", meta=(EditCondition="bIsMerchant"))
	FLinearColor MerchantCurrencyTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant"))
	TObjectPtr<UMerchantInventoryDataAsset> MerchantInventoryData = nullptr;

	// optional behavior toggle (keeps your old merchant test behavior available)
	UPROPERTY(EditAnywhere, Category="NPC Config|Merchant", meta=(EditCondition="bIsMerchant"))
	bool bQuickSellAllOnInteract = false;

	// --- Lock-on aim tuning ---
	UPROPERTY(EditAnywhere, Category="NPC Config|LockOn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LockOnAimHeightRatio = 0.72f;

	// --- Perception ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Perception", meta=(ClampMin="0.0", Units="cm"))
	float ReactionRange = 900.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Perception", meta=(ClampMin="1.0", ClampMax="180.0", Units="deg"))
	float NoticeFOVDegrees = 110.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Perception", meta=(ClampMin="0.0", Units="s"))
	float LoseInterestSeconds = 4.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Perception", meta=(ClampMin="0.05", Units="s"))
	float ReactionRepathInterval = 0.35f;

	// --- AI / Timing ---
	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="0.05"))
	float BrainTickSeconds = 0.15f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="50.0", Units="cm"))
	float ChaseAcceptanceRadius = 150.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="100.0", Units="cm"))
	float FleeDistance = 800.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="50.0", Units="cm"))
	float ReturnHomeAcceptanceRadius = 120.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="0.0", Units="s"))
	float StuckAbortSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="1"))
	int32 FleeSampleTries = 8;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg"))
	float FleeAngleJitterDegrees = 90.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|AI", meta=(ClampMin="10.0", Units="cm"))
	float FleeNavSearchRadius = 300.0f;

	// --- Wander ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Wander", meta=(EditCondition="!bIsStationary", ClampMin="50.0", Units="cm"))
	float WanderRadius = 900.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Wander", meta=(EditCondition="!bIsStationary", ClampMin="10.0", Units="cm"))
	float WanderAcceptanceRadius = 80.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Wander", meta=(EditCondition="!bIsStationary", ClampMin="0.0", Units="s"))
	float WanderWaitMin = 0.8f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Wander", meta=(EditCondition="!bIsStationary", ClampMin="0.0", Units="s"))
	float WanderWaitMax = 2.8f;

	UPROPERTY(EditInstanceOnly, Category="NPC Config|Wander", meta=(EditCondition="!bIsStationary"))
	TObjectPtr<ANPCSafeZone> SafeZone;

	// --- Speed / Turning ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Speed", meta=(ClampMin="0.0", Units="cm/s"))
	float WanderSpeed = 400.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Speed", meta=(ClampMin="0.0", Units="cm/s"))
	float MaxReactionSpeed = 600.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Speed", meta=(ClampMin="0.05", Units="s"))
	float WanderRampSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Speed", meta=(ClampMin="0.0"))
	float RotationRateYaw = 540.0f;

	// --- Animation ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Animation")
	TSubclassOf<UAnimInstance> NPCAnimBlueprintClass;

	UPROPERTY(EditAnywhere, Category="NPC Config|Animation")
	bool bAlwaysTickAnimation = true;

	// --- Visual / Placement ---
	UPROPERTY(EditAnywhere, Category="NPC Config|Visual")
	bool bUsePlaceholderMesh = true;

	UPROPERTY(EditAnywhere, Category="NPC Config|Visual")
	float SkeletalMeshZOffsetExtra = 0.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|Visual")
	float SkeletalMeshYawOffset = -90.0f;

	UPROPERTY(VisibleAnywhere, Category="NPC Config|Visual")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	// -------------------------
	// UI: Health bar
	// -------------------------
	UPROPERTY(VisibleAnywhere, Category="NPC|UI")
	TObjectPtr<UWidgetComponent> HealthBarComponent = nullptr;

	UPROPERTY(EditAnywhere, Category="NPC Config|UI")
	TSubclassOf<UNPCHealthBarWidget> HealthBarWidgetClass;

	UPROPERTY(EditAnywhere, Category="NPC Config|UI", meta=(ClampMin="0.1"))
	float HealthBarHideDelaySeconds = 5.0f;

	UPROPERTY(EditAnywhere, Category="NPC Config|UI")
	FVector HealthBarWorldOffset = FVector(0.f, 0.f, 110.f);

	FTimerHandle HealthBarHideTimerHandle;

	void ShowHealthBarNow();
	void FadeHealthBar();

	// ------------------------------------------------------------
	// Runtime state
	// ------------------------------------------------------------
	FTimerHandle BrainTimerHandle;

	FVector HomeLocation = FVector::ZeroVector;
	TWeakObjectPtr<ANPCSafeZone> LastRegisteredZone;

	enum class ENPCMode : uint8
	{
		Wander,
		Chase,
		Flee,
		ReturnHome
	};

	ENPCMode CurrentMode = ENPCMode::Wander;

	float OutOfRangeStartTime = -1.0f;
	float NextWanderAllowedTime = 0.0f;
	bool bWasMovingLastTick = false;

	bool bSpeedRamping = false;
	float RampStartTime = 0.0f;
	float RampDuration = 0.0f;
	float RampStartSpeed = 0.0f;
	float RampTargetSpeed = 0.0f;

	float LastReactionMoveTime = -1000.0f;
	float StuckStartTime = -1.0f;

	bool bHasReturnTarget = false;
	FVector CachedReturnTarget = FVector::ZeroVector;
	float LastReturnTargetPickTime = -1000.0f;

	bool bIsDead = false;

	bool bHealthInitialized = false;
	float LastRequestedBaseSpeed = 0.0f;
	float LastDamageTimeSeconds = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category="NPC Runtime|Merchant")
	TArray<FMerchantInventoryEntry> MerchantInventoryRuntime;

	// ------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------
	void BrainTick();

	void Wander(AAIController* AIC);
	void ChasePlayer(AAIController* AIC, APawn* PlayerPawn);
	void FleeFromPlayer(AAIController* AIC, APawn* PlayerPawn);
	void ReturnHome(AAIController* AIC);

	bool CanNoticePlayerCone(const APawn* PlayerPawn) const;
	bool IsPlayerInReactionRange(const APawn* PlayerPawn) const;

	void UpdateLoseInterestTimer(bool bPlayerInRange);
	void ClearLoseInterestTimer();

	FVector GetHomeCenter() const;

	void StartSpeedRampTo(float TargetSpeed, float DurationSeconds, bool bFromZero);
	void SetSpeedImmediate(float Speed);

	float GetHealthPercent01() const;
	float GetLowHealthMoveMultiplier() const;
	void CancelSpeedRamp();
	void ReapplyMoveSpeedFromLastRequest();

	void TryAutoRestoreHealth(float NowSeconds);

	bool IsAIMoving(const AAIController* AIC) const;
	bool FindFleeDestination(APawn* PlayerPawn, FVector& OutDest) const;

	bool IsInsideSafeZone2D() const;
	void ResetReturnHomeCache();

	void ApplyCollisionDefaults();
	void ApplyVisualDefaults();
	void ApplyAnimationDefaults();

	void InitializeRuntimeState();
	void HandleDeath(AActor* Killer);
	void SpawnDrops();

	void EnterRagdoll(AActor* DamageCauser);

	int32 FindMerchantEntryIndex_Runtime(const UItemDataAsset* Item) const;

	// Preferred rules
	const FPreferredItemConfig* FindPreferredConfig(const UItemDataAsset* Item) const;
	bool IsPreferredItem(const UItemDataAsset* Item) const;
	float GetPreferredSellMultiplier(const UItemDataAsset* Item) const;
	int32 GetPreferredRelationshipPointsPerUnit(const UItemDataAsset* Item) const;

	// Relationship thresholds
	int32 GetPointsRequiredForNextLevel(int32 Level) const;

	// Rarity
	float GetRaritySellMultiplier(EItemRarity Rarity) const;

	// Resale
	int32 GetResaleBuyPricePerUnit(const UItemDataAsset* Item) const;
};
