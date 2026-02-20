#include "InventorySlotWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
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

	if (BTN_Add)
	{
		BTN_Add->OnClicked.AddDynamic(this, &UInventorySlotWidget::HandleAddClicked);
	}

	if (BTN_Sub)
	{
		BTN_Sub->OnClicked.AddDynamic(this, &UInventorySlotWidget::HandleSubClicked);
	}

	bSelected = false;
	bTradeModeEnabled = false;
	bTradeQuantityPickerEnabled = true;
	SelectedTradeQuantity = 0;
	bHasFocusVisual = false;
	bHasHoverVisual = false;
	bEffectiveHover = false;

	UpdateTradeQuantityVisual();
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

	if (ItemCostText)
	{
		ItemCostText->SetVisibility(ESlateVisibility::Collapsed);
		ItemCostText->SetText(FText::GetEmpty());
	}

	UpdateVisualState();
}

void UInventorySlotWidget::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;

	if (!bSelected)
	{
		SelectedTradeQuantity = 0;
	}

	UpdateTradeQuantityVisual();
	UpdateVisualState();
}

void UInventorySlotWidget::SetTradeModeEnabled(bool bEnabled)
{
	bTradeModeEnabled = bEnabled;

	if (!bTradeModeEnabled)
	{
		SelectedTradeQuantity = 0;
	}

	UpdateTradeQuantityVisual();
}

void UInventorySlotWidget::SetTradeQuantityPickerEnabled(bool bEnabled)
{
	bTradeQuantityPickerEnabled = bEnabled;
	UpdateTradeQuantityVisual();
}

void UInventorySlotWidget::SetSelectedTradeQuantity(int32 NewQty)
{
	SelectedTradeQuantity = FMath::Max(0, NewQty);
	UpdateTradeQuantityVisual();
}

void UInventorySlotWidget::ResetTradeQuantity()
{
	SelectedTradeQuantity = 0;
	UpdateTradeQuantityVisual();
}

void UInventorySlotWidget::SetItemCostText(const FText& InCostText)
{
	if (!ItemCostText) return;

	if (InCostText.IsEmpty())
	{
		ItemCostText->SetVisibility(ESlateVisibility::Collapsed);
		ItemCostText->SetText(FText::GetEmpty());
		return;
	}

	ItemCostText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ItemCostText->SetText(InCostText);
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
	OnSlotHovered.Broadcast(this);
	bHasHoverVisual = true;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::HandleUnhovered()
{
	OnSlotUnhovered.Broadcast(this);
	bHasHoverVisual = false;
	UpdateEffectiveHover();
}

void UInventorySlotWidget::HandleClicked()
{
	OnSlotClicked.Broadcast(this);
}

void UInventorySlotWidget::HandleAddClicked()
{
	OnSlotAddClicked.Broadcast(this);
}

void UInventorySlotWidget::HandleSubClicked()
{
	OnSlotSubClicked.Broadcast(this);
}

void UInventorySlotWidget::UpdateTradeQuantityVisual()
{
	const bool bShowTradeQty = bTradeModeEnabled && bSelected && bTradeQuantityPickerEnabled;

	if (HB_QtySelection)
	{
		HB_QtySelection->SetVisibility(bShowTradeQty ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (SellQtyText)
	{
		if (bShowTradeQty)
		{
			SellQtyText->SetVisibility(ESlateVisibility::HitTestInvisible);
			SellQtyText->SetText(FText::AsNumber(SelectedTradeQuantity));
		}
		else
		{
			SellQtyText->SetVisibility(ESlateVisibility::Collapsed);
			SellQtyText->SetText(FText::GetEmpty());
		}
	}
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
