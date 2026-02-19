#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlayerStatsComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStatsChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamaged, float, DamageAmount, AActor*, DamageInstigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDied);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CPP_TESTS_API UPlayerStatsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerStatsComponent();

	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnStatsChanged OnStatsChanged;

	UPROPERTY(BlueprintAssignable, Category="Stats|Combat")
	FOnDamaged OnDamaged;

	UPROPERTY(BlueprintAssignable, Category="Stats|Combat")
	FOnDied OnDied;

	// Upgradable stats
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Upgradable") int32 Strength = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Upgradable") int32 Endurance = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Upgradable") int32 Willpower = 5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Upgradable") int32 Luck = 5;

	// Derived max values
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Derived") float MaxHealth = 100.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Derived") float MaxStamina = 100.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Derived") float MaxMagic = 100.f;

	// Current values
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Current") float Health = 100.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Current") float Stamina = 100.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats|Current") float Magic = 100.f;

	// Economy
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Economy", meta=(ClampMin="0"))
	int32 Currency = 0;

	UFUNCTION(BlueprintCallable, Category="Stats|Economy")
	int32 GetCurrency() const { return Currency; }

	UFUNCTION(BlueprintCallable, Category="Stats|Economy")
	void ModifyCurrency(int32 Delta);

	UFUNCTION(BlueprintCallable, Category="Stats|Economy")
	bool CanAfford(int32 Cost) const { return Cost <= 0 ? true : Currency >= Cost; }

	UFUNCTION(BlueprintCallable, Category="Stats|Economy")
	bool SpendCurrency(int32 Cost);

	// Base combat numbers
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Combat") float BaseDamageOutput = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Combat") float BaseDamageReduction = 0.f;

	// Sprint / regen tuning
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Stamina") float StaminaDrainPerSecond_Sprinting = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Stamina") float StaminaRegenPerSecond_Standing = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Stamina") float StaminaRegenPerSecond_Moving = 7.5f;

	// Health regen (Souls test rule you requested)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Health") float HealthRegenPerSecond_Standing = 1.f;

	// Magic regen is driven by status effects (multiplier), base is here
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stats|Magic") float MagicRegenPerSecond = 6.f;

	UFUNCTION(BlueprintCallable, Category="Stats") float GetHealthPercent() const;
	UFUNCTION(BlueprintCallable, Category="Stats") float GetStaminaPercent() const;
	UFUNCTION(BlueprintCallable, Category="Stats") float GetMagicPercent() const;

	UFUNCTION(BlueprintCallable, Category="Stats|Stamina") float GetAvailableStaminaMax() const;
	UFUNCTION(BlueprintCallable, Category="Stats|Stamina") float GetStaminaPercentOfAvailable() const;

	UFUNCTION(BlueprintCallable, Category="Stats") void RecalculateDerivedStats(bool bKeepCurrentPercents = true);

	UFUNCTION(BlueprintCallable, Category="Stats") void ModifyHealth(float Delta);
	UFUNCTION(BlueprintCallable, Category="Stats") void ModifyStamina(float Delta);
	UFUNCTION(BlueprintCallable, Category="Stats") void ModifyMagic(float Delta);

	UFUNCTION(BlueprintCallable, Category="Stats|Combat")
	float ApplyDamage(float RawDamage, AActor* DamageInstigator);

	UFUNCTION(BlueprintCallable, Category="Stats|Combat")
	bool IsDead() const { return Health <= 0.f; }

	void TickStamina(float DeltaSeconds, bool bWantsSprint, bool bIsMoving, float RegenMultiplier);
	void TickMagic(float DeltaSeconds, float RegenMultiplier);

protected:
	virtual void BeginPlay() override;

private:
	static float SafePercent(float Current, float Max);
	static float ClampToRange(float Value, float Max);

	void ClampStaminaToAvailable(bool& bDidChange);

	bool bHasDiedBroadcast = false;
};
