#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerHUDWidget.generated.h"

class UProgressBar;
class UImage;
class UHorizontalBox;
class UPlayerStatsComponent;
class UStatusEffectComponent;
class USizeBox;
class UTextBlock;

UCLASS()
class CPP_TESTS_API UPlayerHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="HUD")
	void InitializeFromComponents(UPlayerStatsComponent* InStats, UStatusEffectComponent* InEffects);

protected:
	UPROPERTY(meta=(BindWidget)) UProgressBar* HealthBar;

	UPROPERTY(meta=(BindWidget)) UProgressBar* StaminaFillBar;
	UPROPERTY(meta=(BindWidget)) UProgressBar* StaminaAvailBar;
	UPROPERTY(meta=(BindWidget)) UProgressBar* StaminaMaxBar;

	UPROPERTY(meta=(BindWidget)) UProgressBar* MagicBar;

	UPROPERTY(meta=(BindWidget)) UImage* TalismanImage;

	UPROPERTY(meta=(BindWidget)) UHorizontalBox* StatusEffectSlots;

	UPROPERTY(meta=(BindWidgetOptional)) USizeBox* HealthSizeBox;
	UPROPERTY(meta=(BindWidgetOptional)) USizeBox* StaminaSizeBox;
	UPROPERTY(meta=(BindWidgetOptional)) USizeBox* MagicSizeBox;

	// TextBlock named "Currency" in WBP_PlayerHUD
	UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* Currency;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float BaseHealthWidth = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float BaseStaminaWidth = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float BaseMagicWidth = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float HealthPixelsPerPoint = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float StaminaPixelsPerPoint = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float MagicPixelsPerPoint = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="HUD|Bar Width", meta=(ClampMin="0.0"))
	float MaxWidthClamp = 0.f;

	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY() UPlayerStatsComponent* Stats = nullptr;
	UPROPERTY() UStatusEffectComponent* Effects = nullptr;

	UFUNCTION() void HandleStatsChanged();
	UFUNCTION() void HandleEffectsChanged();

	void RefreshBars();
	void RefreshStatusIcons();

	// NEW: robust currency refresh
	void RefreshCurrencyOnly();
	int32 LastCurrencyShown = INT32_MIN;

	float ApplyOptionalClamp(float Width) const;
};
