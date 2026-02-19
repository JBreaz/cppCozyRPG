#include "InventorySlotWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "ItemDataAsset.h"
#include "GameFramework/PlayerController.h"

void UInventorySlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetIsFocusable(true);

	if (SlotButton)
	{
		SlotButton->OnHovered.AddDynamic(this, &UInventorySlotWidget::HandleHovered);
		SlotButton->OnUnhovered.AddDynamic(this, &UInventorySlotWidget::HandleUnhovered);
		SlotButton->OnClicked.AddDynamic(this, &UInventorySlotWidget::HandleClicked);
	}

	bSelected = false;
	bHasFocusVisual = false;
	bHasHoverVisual = false;
	bEffectiveHover = false;

	UpdateVisualState();
}

void UInventorySlotWidget::SetupSlot(UItemDataAsset* InItem, int32 InQuantity, EItemRarity InRarity, UTexture2D* InRarityIcon, FLinearColor InRarityTint)
{
	Item = InItem;
	Quantity = InQuantity;
	Rarity = InRarity;
	RarityTint = InRarityTint;

	if (IconImage)
	{
		IconImage->SetBrushFromTexture(Item ? Item->Icon : nullptr, true);
	}

	if (QtyText)
	{
		if (Quantity > 1)
		{
			QtyText->SetVisibility(ESlateVisibility::HitTestInvisible);
			QtyText->SetText(FText::AsNumber(Quantity));
		}
		else
		{
			QtyText->SetVisibility(ESlateVisibility::Collapsed);
			QtyText->SetText(FText::GetEmpty());
		}
	}

	if (RarityImage)
	{
		RarityImage->SetBrushFromTexture(InRarityIcon, true);
		RarityImage->SetColorAndOpacity(RarityTint);
		RarityImage->SetVisibility(InRarityIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	UpdateVisualState();
}

void UInventorySlotWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	UpdateVisualState();
}

void UInventorySlotWidget::FocusSlot()
{
	APlayerController* PC = GetOwningPlayer();
	if (!PC) return;

	if (SlotButton)
	{
		SlotButton->SetUserFocus(PC);
		SlotButton->SetKeyboardFocus();
		return;
	}

	SetUserFocus(PC);
	SetKeyboardFocus();
}

void UInventorySlotWidget::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	bHasFocusVisual = true;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
	bHasFocusVisual = false;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::HandleHovered()
{
	bHasHoverVisual = true;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::HandleUnhovered()
{
	bHasHoverVisual = false;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::HandleClicked()
{
	OnSlotClicked.Broadcast(this);
}

void UInventorySlotWidget::UpdateEffectiveHover()
{
	const bool bNewEffectiveHover = (bHasHoverVisual || bHasFocusVisual);

	// Always update visuals, even if no state change
	if (bNewEffectiveHover == bEffectiveHover)
	{
		UpdateVisualState();
		return;
	}

	bEffectiveHover = bNewEffectiveHover;
	UpdateVisualState();

	if (bEffectiveHover)
	{
		OnSlotHovered.Broadcast(this);
	}
	else
	{
		OnSlotUnhovered.Broadcast(this);
	}
}

void UInventorySlotWidget::UpdateVisualState()
{
	// 1) Selected overlay stays on when selected
	if (SelectedHighlight)
	{
		SelectedHighlight->SetVisibility(bSelected ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	// 2) Button tint shows hover/focus even for non-selected slots
	if (SlotButton)
	{
		if (bSelected)
		{
			SlotButton->SetBackgroundColor(SelectedButtonTint);
		}
		else if (bEffectiveHover)
		{
			SlotButton->SetBackgroundColor(HoverButtonTint);
		}
		else
		{
			SlotButton->SetBackgroundColor(UnselectedButtonTint);
		}
	}
}
