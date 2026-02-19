#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPCHealthBarWidget.generated.h"

class UProgressBar;
class UWidgetAnimation;

UCLASS(Abstract)
class CPP_TESTS_API UNPCHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Set % (0..1)
	UFUNCTION(BlueprintCallable, Category="NPC|UI")
	void SetHealthPercent(float Percent);

	// Show immediately (cancel fade)
	UFUNCTION(BlueprintCallable, Category="NPC|UI")
	void ShowInstant();

	// Play fade animation (will hide at end)
	UFUNCTION(BlueprintCallable, Category="NPC|UI")
	void PlayFadeOut();

protected:
	virtual void NativeOnInitialized() override;

	// Must match widget name in WBP
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> PB_Health = nullptr;

	// Must match animation name in WBP
	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> Anim_FadeOut = nullptr;

private:
	UFUNCTION()
	void HandleFadeOutFinished();
};
