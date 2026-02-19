#include "StatusEffectComponent.h"

#include "PlayerStatsComponent.h"
#include "Math/UnrealMathUtility.h"

UStatusEffectComponent::UStatusEffectComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UStatusEffectComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UStatusEffectComponent::BroadcastIfChanged(bool& bAnyChanged)
{
    bAnyChanged = true;
}

void UStatusEffectComponent::ApplyPoisonExposure()
{
    PoisonTimeRemaining = PoisonPostExposureDuration;
    OnEffectsChanged.Broadcast();
}

void UStatusEffectComponent::AddFearPoints(float Points)
{
    if (Points <= 0.f) return;
    bFearExposedThisFrame = true;
    FearPoints = FMath::Max(0.f, FearPoints + Points);
    OnEffectsChanged.Broadcast();
}

void UStatusEffectComponent::ApplyBurn(bool bEnableBurn)
{
    if (bBurned != bEnableBurn)
    {
        bBurned = bEnableBurn;
        OnEffectsChanged.Broadcast();
    }
}

void UStatusEffectComponent::AddFrostPoints(float Points)
{
    if (Points <= 0.f) return;
    bFrostExposedThisFrame = true;
    FrostPoints = FMath::Max(0.f, FrostPoints + Points);
    OnEffectsChanged.Broadcast();
}

void UStatusEffectComponent::AddBleedPoints(float Points)
{
    if (Points <= 0.f) return;
    bBleedExposedThisFrame = true;
    BleedPoints = FMath::Max(0.f, BleedPoints + Points);
    OnEffectsChanged.Broadcast();
}

void UStatusEffectComponent::AddStatusPoints(EStatusEffectType Type, float Points)
{
    switch (Type)
    {
        case EStatusEffectType::Poison: ApplyPoisonExposure(); break;
        case EStatusEffectType::Fear:   AddFearPoints(Points); break;
        case EStatusEffectType::Burn:   ApplyBurn(Points > 0.f); break;
        case EStatusEffectType::Frost:  AddFrostPoints(Points); break;
        case EStatusEffectType::Bleed:  AddBleedPoints(Points); break;
        default: break;
    }
}

void UStatusEffectComponent::ClearAll()
{
    PoisonTimeRemaining = 0.f;
    FearPoints = 0.f;
    bBurned = false;
    FrostPoints = 0.f;
    BleedPoints = 0.f;

    OnEffectsChanged.Broadcast();
}

float UStatusEffectComponent::GetMoveSpeedMultiplier() const
{
    float Mult = 1.f;

    if (FearPoints > 0.f)
    {
        Mult -= (FearPoints * FearMovePenaltyPerPoint);
    }

    if (FrostPoints > 0.f)
    {
        Mult -= (FrostPoints * 0.002f);
    }

    return FMath::Clamp(Mult, MinMoveMultiplier, 1.f);
}

float UStatusEffectComponent::GetStaminaRegenMultiplier() const
{
    float Mult = 1.f;
    if (FearPoints > 0.f)
    {
        Mult -= (FearPoints * FearRegenPenaltyPerPoint);
    }
    return FMath::Clamp(Mult, 0.f, 1.f);
}

float UStatusEffectComponent::GetMagicRegenMultiplier() const
{
    float Mult = 1.f;
    if (FearPoints > 0.f)
    {
        Mult -= (FearPoints * FearRegenPenaltyPerPoint);
    }
    return FMath::Clamp(Mult, 0.f, 1.f);
}

void UStatusEffectComponent::TickEffects(float DeltaSeconds, UPlayerStatsComponent* Stats, bool bIsMoving)
{
    if (!Stats || DeltaSeconds <= 0.f)
    {
        bFearExposedThisFrame = false;
        bFrostExposedThisFrame = false;
        bBleedExposedThisFrame = false;
        return;
    }

    bool bAnyChanged = false;

    if (PoisonTimeRemaining > 0.f)
    {
        PoisonTimeRemaining = FMath::Max(0.f, PoisonTimeRemaining - DeltaSeconds);
        Stats->ModifyHealth(-PoisonDamagePerSecond * DeltaSeconds);
        BroadcastIfChanged(bAnyChanged);
    }

    if (FearPoints > 0.f && !bFearExposedThisFrame)
    {
        FearPoints = FMath::Max(0.f, FearPoints - (FearDecayPerSecond * DeltaSeconds));
        BroadcastIfChanged(bAnyChanged);
    }

    if (bBurned && bIsMoving)
    {
        Stats->ModifyHealth(-BurnMoveDamagePerSecond * DeltaSeconds);
        BroadcastIfChanged(bAnyChanged);
    }

    if (FrostPoints > 0.f)
    {
        if (Stats->Stamina > 0.f)
        {
            Stats->ModifyStamina(-FrostStaminaDrainPerSecond * DeltaSeconds);
        }
        else
        {
            Stats->ModifyHealth(-FrostHealthDamagePerSecond_IfNoStamina * DeltaSeconds);
        }
        BroadcastIfChanged(bAnyChanged);
    }

    if (FrostPoints > 0.f && !bFrostExposedThisFrame)
    {
        FrostPoints = FMath::Max(0.f, FrostPoints - (FrostDecayPerSecond * DeltaSeconds));
        BroadcastIfChanged(bAnyChanged);
    }

    if (BleedPoints > 0.f)
    {
        const float HealthDPS = BleedPoints * BleedHealthDamagePerSecondPerPoint;
        const float StamDPS = BleedPoints * BleedStaminaDrainPerSecondPerPoint;

        Stats->ModifyHealth(-HealthDPS * DeltaSeconds);
        Stats->ModifyStamina(-StamDPS * DeltaSeconds);
        BroadcastIfChanged(bAnyChanged);
    }

    if (BleedPoints > 0.f && !bBleedExposedThisFrame)
    {
        BleedPoints = FMath::Max(0.f, BleedPoints - (BleedDecayPerSecond * DeltaSeconds));
        BroadcastIfChanged(bAnyChanged);
    }

    bFearExposedThisFrame = false;
    bFrostExposedThisFrame = false;
    bBleedExposedThisFrame = false;

    if (bAnyChanged)
    {
        OnEffectsChanged.Broadcast();
    }
}
