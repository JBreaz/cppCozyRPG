#include "PlayerHUDWidget.h"

#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"

void UPlayerHUDWidget::InitializeFromComponents(UPlayerStatsComponent* InStats, UStatusEffectComponent* InEffects)
{
	Stats = InStats;
	Effects = InEffects;

	if (Stats)
	{
		Stats->OnStatsChanged.AddDynamic(this, &UPlayerHUDWidget::HandleStatsChanged);
	}
	if (Effects)
	{
		Effects->OnEffectsChanged.AddDynamic(this, &UPlayerHUDWidget::HandleEffectsChanged);
	}

	RefreshBars();
	RefreshStatusIcons();
	RefreshCurrencyOnly();
}

void UPlayerHUDWidget::NativeDestruct()
{
	if (Stats)
	{
		Stats->OnStatsChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleStatsChanged);
	}
	if (Effects)
	{
		Effects->OnEffectsChanged.RemoveDynamic(this, &UPlayerHUDWidget::HandleEffectsChanged);
	}

	Super::NativeDestruct();
}

void UPlayerHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Currency might change without OnStatsChanged firing (common early bug).
	RefreshCurrencyOnly();
}

void UPlayerHUDWidget::HandleStatsChanged()
{
	RefreshBars();
	RefreshCurrencyOnly();
}

void UPlayerHUDWidget::HandleEffectsChanged()
{
	RefreshStatusIcons();
}

float UPlayerHUDWidget::ApplyOptionalClamp(float Width) const
{
	if (MaxWidthClamp > 0.f)
	{
		return FMath::Min(Width, MaxWidthClamp);
	}
	return Width;
}

void UPlayerHUDWidget::RefreshCurrencyOnly()
{
	if (!Currency) return;

	const int32 NewValue = Stats ? Stats->GetCurrency() : 0;
	if (NewValue != LastCurrencyShown)
	{
		LastCurrencyShown = NewValue;
		Currency->SetText(FText::AsNumber(NewValue));
	}
}

void UPlayerHUDWidget::RefreshBars()
{
	if (!Stats)
	{
		if (HealthBar) HealthBar->SetPercent(0.f);
		if (MagicBar) MagicBar->SetPercent(0.f);
		if (StaminaFillBar) StaminaFillBar->SetPercent(0.f);
		if (StaminaAvailBar) StaminaAvailBar->SetPercent(0.f);
		if (StaminaMaxBar) StaminaMaxBar->SetPercent(1.f);

		if (Currency) Currency->SetText(FText::FromString(TEXT("0")));
		LastCurrencyShown = 0;
		return;
	}

	// Currency (also handled in RefreshCurrencyOnly / Tick)
	RefreshCurrencyOnly();

	// Fill percents
	const float HPct = Stats->GetHealthPercent();
	if (HealthBar) HealthBar->SetPercent(HPct);
	if (MagicBar)  MagicBar->SetPercent(Stats->GetMagicPercent());

	// Stamina trays
	if (StaminaMaxBar) StaminaMaxBar->SetPercent(1.f);

	const float AvMax = Stats->GetAvailableStaminaMax();
	const float GoldPercent = (Stats->MaxStamina <= 0.f) ? 0.f : FMath::Clamp(AvMax / Stats->MaxStamina, 0.f, 1.f);

	if (StaminaAvailBar) StaminaAvailBar->SetPercent(GoldPercent);

	float GreenPercent = 0.f;
	if (Stats->MaxStamina > 0.f)
	{
		GreenPercent = FMath::Clamp(Stats->Stamina / Stats->MaxStamina, 0.f, 1.f);
		GreenPercent = FMath::Min(GreenPercent, GoldPercent);
	}

	if (StaminaFillBar)
	{
		StaminaFillBar->SetPercent(GreenPercent);
	}

	if (StaminaAvailBar)
	{
		const bool bHideYellow = (HPct >= 0.999f);
		StaminaAvailBar->SetVisibility(bHideYellow ? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	}

	// Souls-style bar container width growth
	const float StrengthPts  = (float)FMath::Max(0, Stats->Strength);
	const float EndurancePts = (float)FMath::Max(0, Stats->Endurance);
	const float WillpowerPts = (float)FMath::Max(0, Stats->Willpower);

	float HealthW  = BaseHealthWidth  + (StrengthPts  * HealthPixelsPerPoint);
	float StaminaW = BaseStaminaWidth + (EndurancePts * StaminaPixelsPerPoint);
	float MagicW   = BaseMagicWidth   + (WillpowerPts * MagicPixelsPerPoint);

	HealthW  = ApplyOptionalClamp(HealthW);
	StaminaW = ApplyOptionalClamp(StaminaW);
	MagicW   = ApplyOptionalClamp(MagicW);

	if (HealthSizeBox)  HealthSizeBox->SetWidthOverride(HealthW);
	if (StaminaSizeBox) StaminaSizeBox->SetWidthOverride(StaminaW);
	if (MagicSizeBox)   MagicSizeBox->SetWidthOverride(MagicW);
}

void UPlayerHUDWidget::RefreshStatusIcons()
{
	// MVP: later populate StatusEffectSlots with icon widgets / buildup meters.
}
