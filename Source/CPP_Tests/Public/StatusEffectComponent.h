#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StatusEffectComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEffectsChanged);

UENUM(BlueprintType)
enum class EStatusEffectType : uint8
{
    None   UMETA(DisplayName="None"),
    Poison UMETA(DisplayName="Poison"),
    Fear   UMETA(DisplayName="Fear"),
    Burn   UMETA(DisplayName="Burn"),
    Frost  UMETA(DisplayName="Frost"),
    Bleed  UMETA(DisplayName="Bleed")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CPP_TESTS_API UStatusEffectComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UStatusEffectComponent();

    UPROPERTY(BlueprintAssignable, Category="Status")
    FOnEffectsChanged OnEffectsChanged;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Status|Poison")
    float PoisonTimeRemaining = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Status|Fear")
    float FearPoints = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Status|Burn")
    bool bBurned = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Status|Frost")
    float FrostPoints = 0.f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Status|Bleed")
    float BleedPoints = 0.f;

    UFUNCTION(BlueprintPure, Category="Status|UI") float GetPoisonTimeRemaining() const { return PoisonTimeRemaining; }
    UFUNCTION(BlueprintPure, Category="Status|UI") float GetFearPoints() const { return FearPoints; }
    UFUNCTION(BlueprintPure, Category="Status|UI") bool IsBurned() const { return bBurned; }
    UFUNCTION(BlueprintPure, Category="Status|UI") float GetFrostPoints() const { return FrostPoints; }
    UFUNCTION(BlueprintPure, Category="Status|UI") float GetBleedPoints() const { return BleedPoints; }

    // Tuning
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Poison", meta=(ClampMin="0.0"))
    float PoisonDamagePerSecond = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Poison", meta=(ClampMin="0.0"))
    float PoisonPostExposureDuration = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Fear", meta=(ClampMin="0.0"))
    float FearDecayPerSecond = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Fear", meta=(ClampMin="0.0"))
    float FearRegenPenaltyPerPoint = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Fear", meta=(ClampMin="0.0"))
    float FearMovePenaltyPerPoint = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Burn", meta=(ClampMin="0.0"))
    float BurnMoveDamagePerSecond = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Frost", meta=(ClampMin="0.0"))
    float FrostStaminaDrainPerSecond = 15.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Frost", meta=(ClampMin="0.0"))
    float FrostHealthDamagePerSecond_IfNoStamina = 4.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Frost", meta=(ClampMin="0.0"))
    float FrostDecayPerSecond = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Bleed", meta=(ClampMin="0.0"))
    float BleedHealthDamagePerSecondPerPoint = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Bleed", meta=(ClampMin="0.0"))
    float BleedStaminaDrainPerSecondPerPoint = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Bleed", meta=(ClampMin="0.0"))
    float BleedDecayPerSecond = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tuning|Global", meta=(ClampMin="0.05", ClampMax="1.0"))
    float MinMoveMultiplier = 0.25f;

public:
    void TickEffects(float DeltaSeconds, class UPlayerStatsComponent* Stats, bool bIsMoving);

    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void ApplyPoisonExposure();

    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void AddFearPoints(float Points);

    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void ApplyBurn(bool bEnableBurn = true);

    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void AddFrostPoints(float Points);

    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void AddBleedPoints(float Points);

    // Generic helper (tools can call this)
    UFUNCTION(BlueprintCallable, Category="Status|Apply")
    void AddStatusPoints(EStatusEffectType Type, float Points);

    UFUNCTION(BlueprintCallable, Category="Status|Clear")
    void ClearAll();

    float GetMoveSpeedMultiplier() const;
    float GetStaminaRegenMultiplier() const;
    float GetMagicRegenMultiplier() const;

protected:
    virtual void BeginPlay() override;

private:
    bool bFearExposedThisFrame = false;
    bool bFrostExposedThisFrame = false;
    bool bBleedExposedThisFrame = false;

    void BroadcastIfChanged(bool& bAnyChanged);
};
