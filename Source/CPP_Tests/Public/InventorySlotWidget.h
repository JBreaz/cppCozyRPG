#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryComponent.h" // EItemRarity
#include "InventorySlotWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UItemDataAsset;
class UTexture2D;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySlotHovered, UInventorySlotWidget*, SlotWidget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySlotUnhovered, UInventorySlotWidget*, SlotWidget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySlotClicked, UInventorySlotWidget*, SlotWidget);

UCLASS()
class CPP_TESTS_API UInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Inventory")
	void SetupSlot(UItemDataAsset* InItem, int32 InQuantity, EItemRarity InRarity, UTexture2D* InRarityIcon, FLinearColor InRarityTint);

	UItemDataAsset* GetItem() const { return Item; }
	int32 GetQuantity() const { return Quantity; }
	EItemRarity GetRarity() const { return Rarity; }

	UFUNCTION(BlueprintCallable, Category="Inventory")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool IsSelected() const { return bSelected; }

	UFUNCTION(BlueprintCallable, Category="Inventory")
	void FocusSlot();

	UPROPERTY(BlueprintAssignable) FOnInventorySlotHovered OnSlotHovered;
	UPROPERTY(BlueprintAssignable) FOnInventorySlotUnhovered OnSlotUnhovered;
	UPROPERTY(BlueprintAssignable) FOnInventorySlotClicked OnSlotClicked;

protected:
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> SlotButton = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> IconImage = nullptr;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> QtyText = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> RarityImage = nullptr;

	// This is now a TRUE selected overlay (stays on when selected)
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> SelectedHighlight = nullptr;

	virtual void NativeOnInitialized() override;

	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;

private:
	UPROPERTY() TObjectPtr<UItemDataAsset> Item = nullptr;
	UPROPERTY() int32 Quantity = 0;
	UPROPERTY() EItemRarity Rarity = EItemRarity::Garbage;

	UPROPERTY() bool bSelected = false;

	UPROPERTY() bool bHasFocusVisual = false;
	UPROPERTY() bool bHasHoverVisual = false;
	UPROPERTY() bool bEffectiveHover = false;

	UPROPERTY() FLinearColor RarityTint = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, Category="Inventory|UI")
	FLinearColor HoverButtonTint = FLinearColor(0.85f, 0.90f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Inventory|UI")
	FLinearColor SelectedButtonTint = FLinearColor(0.65f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Inventory|UI")
	FLinearColor UnselectedButtonTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleUnhovered();
	UFUNCTION() void HandleClicked();

	void UpdateEffectiveHover();
	void UpdateVisualState();
};
