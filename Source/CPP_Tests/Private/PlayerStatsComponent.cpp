#include "PlayerStatsComponent.h"

UPlayerStatsComponent::UPlayerStatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // character drives updates
}

void UPlayerStatsComponent::BeginPlay()
{
	Super::BeginPlay();
	RecalculateDerivedStats(true);
	OnStatsChanged.Broadcast();
}

void UPlayerStatsComponent::ModifyCurrency(int32 Delta)
{
	if (Delta == 0)
	{
		return;
	}

	const int32 NewVal = FMath::Max(0, Currency + Delta);
	if (NewVal == Currency)
	{
		return;
	}

	Currency = NewVal;
	OnStatsChanged.Broadcast();
}

bool UPlayerStatsComponent::SpendCurrency(int32 Cost)
{
	if (Cost <= 0)
	{
		return true;
	}
	if (Currency < Cost)
	{
		return false;
	}

	Currency -= Cost;
	Currency = FMath::Max(0, Currency);
	OnStatsChanged.Broadcast();
	return true;
}

float UPlayerStatsComponent::SafePercent(float Current, float Max)
{
	return (Max <= 0.f) ? 0.f : FMath::Clamp(Current / Max, 0.f, 1.f);
}

float UPlayerStatsComponent::ClampToRange(float Value, float Max)
{
	return FMath::Clamp(Value, 0.f, FMath::Max(0.f, Max));
}

float UPlayerStatsComponent::GetHealthPercent() const { return SafePercent(Health, MaxHealth); }
float UPlayerStatsComponent::GetStaminaPercent() const { return SafePercent(Stamina, MaxStamina); }
float UPlayerStatsComponent::GetMagicPercent() const { return SafePercent(Magic, MaxMagic); }

float UPlayerStatsComponent::GetAvailableStaminaMax() const
{
	const float MH = FMath::Max(0.f, MaxHealth);
	const float MS = FMath::Max(0.f, MaxStamina);

	if (MH <= 0.f || MS <= 0.f)
	{
		return 0.f;
	}

	const float MissingHealthPoints = FMath::Clamp(MH - Health, 0.f, MH);

	const float StaminaPerHealthPoint = MS / MH;
	const float StaminaCapReduction = (MissingHealthPoints / 3.f) * StaminaPerHealthPoint;

	const float AvMax = MS - StaminaCapReduction;
	return FMath::Clamp(AvMax, 0.f, MS);
}

float UPlayerStatsComponent::GetStaminaPercentOfAvailable() const
{
	const float AvMax = GetAvailableStaminaMax();
	return SafePercent(Stamina, AvMax);
}

void UPlayerStatsComponent::ClampStaminaToAvailable(bool& bDidChange)
{
	const float AvMax = GetAvailableStaminaMax();
	const float NewStam = FMath::Clamp(Stamina, 0.f, AvMax);

	if (!FMath::IsNearlyEqual(NewStam, Stamina))
	{
		Stamina = NewStam;
		bDidChange = true;
	}
}

void UPlayerStatsComponent::RecalculateDerivedStats(bool bKeepCurrentPercents)
{
	const float OldHP = GetHealthPercent();
	const float OldSP = GetStaminaPercent();
	const float OldMP = GetMagicPercent();

	MaxHealth  = 80.f + Strength  * 10.f;
	MaxStamina = 80.f + Endurance * 10.f;
	MaxMagic   = 60.f + Willpower * 12.f;

	BaseDamageOutput = 10.f + Strength * 2.f;
	BaseDamageReduction = Endurance * 0.5f;

	if (bKeepCurrentPercents)
	{
		Health  = ClampToRange(MaxHealth  * OldHP, MaxHealth);
		Stamina = ClampToRange(MaxStamina * OldSP, MaxStamina);
		Magic   = ClampToRange(MaxMagic   * OldMP, MaxMagic);
	}
	else
	{
		Health  = ClampToRange(Health,  MaxHealth);
		Stamina = ClampToRange(Stamina, MaxStamina);
		Magic   = ClampToRange(Magic,   MaxMagic);
	}

	bool bChanged = false;
	ClampStaminaToAvailable(bChanged);

	OnStatsChanged.Broadcast();
}

void UPlayerStatsComponent::ModifyHealth(float Delta)
{
	float NewVal = ClampToRange(Health + Delta, MaxHealth);
	if (FMath::IsNearlyEqual(NewVal, Health))
	{
		return;
	}

	Health = NewVal;

	bool bChanged = true;
	ClampStaminaToAvailable(bChanged);

	if (Health <= 0.f && !bHasDiedBroadcast)
	{
		bHasDiedBroadcast = true;
		OnDied.Broadcast();
	}

	OnStatsChanged.Broadcast();
}

void UPlayerStatsComponent::ModifyStamina(float Delta)
{
	const float AvMax = GetAvailableStaminaMax();
	const float NewVal = FMath::Clamp(Stamina + Delta, 0.f, AvMax);

	if (!FMath::IsNearlyEqual(NewVal, Stamina))
	{
		Stamina = NewVal;
		OnStatsChanged.Broadcast();
	}
}

void UPlayerStatsComponent::ModifyMagic(float Delta)
{
	const float NewVal = ClampToRange(Magic + Delta, MaxMagic);
	if (!FMath::IsNearlyEqual(NewVal, Magic))
	{
		Magic = NewVal;
		OnStatsChanged.Broadcast();
	}
}

float UPlayerStatsComponent::ApplyDamage(float RawDamage, AActor* DamageInstigator)
{
	if (RawDamage <= 0.f || IsDead())
	{
		return 0.f;
	}

	const float Mitigated = FMath::Max(0.f, RawDamage - BaseDamageReduction);
	if (Mitigated <= 0.f)
	{
		return 0.f;
	}

	const float OldHealth = Health;
	ModifyHealth(-Mitigated);

	const float Actual = FMath::Max(0.f, OldHealth - Health);
	if (Actual > 0.f)
	{
		OnDamaged.Broadcast(Actual, DamageInstigator);
	}

	return Actual;
}

void UPlayerStatsComponent::TickStamina(float DeltaSeconds, bool bWantsSprint, bool bIsMoving, float RegenMultiplier)
{
	if (DeltaSeconds <= 0.f) return;

	bool bAnyChanged = false;

	if (!bIsMoving && Health < MaxHealth && HealthRegenPerSecond_Standing > 0.f && !IsDead())
	{
		const float Regen = HealthRegenPerSecond_Standing * DeltaSeconds;
		const float NewH = ClampToRange(Health + Regen, MaxHealth);
		if (!FMath::IsNearlyEqual(NewH, Health))
		{
			Health = NewH;
			bAnyChanged = true;
		}
	}

	if (bWantsSprint && bIsMoving && Stamina > 0.f)
	{
		const float Drain = StaminaDrainPerSecond_Sprinting * DeltaSeconds;
		const float NewS = FMath::Clamp(Stamina - Drain, 0.f, GetAvailableStaminaMax());
		if (!FMath::IsNearlyEqual(NewS, Stamina))
		{
			Stamina = NewS;
			bAnyChanged = true;
		}

		if (bAnyChanged) OnStatsChanged.Broadcast();
		return;
	}

	const float BaseRegen = bIsMoving ? StaminaRegenPerSecond_Moving : StaminaRegenPerSecond_Standing;
	const float Regen = FMath::Max(0.f, BaseRegen * RegenMultiplier);

	if (Regen > 0.f)
	{
		const float AvMax = GetAvailableStaminaMax();
		const float NewS = FMath::Clamp(Stamina + (Regen * DeltaSeconds), 0.f, AvMax);
		if (!FMath::IsNearlyEqual(NewS, Stamina))
		{
			Stamina = NewS;
			bAnyChanged = true;
		}
	}

	ClampStaminaToAvailable(bAnyChanged);

	if (bAnyChanged)
	{
		OnStatsChanged.Broadcast();
	}
}

void UPlayerStatsComponent::TickMagic(float DeltaSeconds, float RegenMultiplier)
{
	if (DeltaSeconds <= 0.f) return;
	if (IsDead()) return;

	const float Regen = FMath::Max(0.f, MagicRegenPerSecond * RegenMultiplier);
	if (Regen <= 0.f) return;

	const float NewVal = ClampToRange(Magic + (Regen * DeltaSeconds), MaxMagic);
	if (!FMath::IsNearlyEqual(NewVal, Magic))
	{
		Magic = NewVal;
		OnStatsChanged.Broadcast();
	}
}
