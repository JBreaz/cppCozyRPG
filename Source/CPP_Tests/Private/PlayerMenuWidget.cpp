#include "PlayerMenuWidget.h"

#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/HorizontalBox.h"
#include "Layout/Margin.h"

#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"
#include "InventoryComponent.h"
#include "ItemDataAsset.h"
#include "InventorySlotWidget.h"
#include "NPCCharacter.h"

#include "InputCoreTypes.h"

void UPlayerMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (InventoryTabButton) InventoryTabButton->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ShowInventoryTab);
	if (StatsTabButton)     StatsTabButton->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ShowStatsTab);

	// Bind BOTH possible button name sets
	if (BTN_ConfirmTrade)
	{
		BTN_ConfirmTrade->OnClicked.RemoveAll(this);
		BTN_ConfirmTrade->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ConfirmTrade);
	}
	if (BTN_ClearTrade)
	{
		BTN_ClearTrade->OnClicked.RemoveAll(this);
		BTN_ClearTrade->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ClearTrade);
	}

	if (ConfirmTradeButton)
	{
		ConfirmTradeButton->OnClicked.RemoveAll(this);
		ConfirmTradeButton->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ConfirmTrade);
	}
	if (ClearTradeButton)
	{
		ClearTradeButton->OnClicked.RemoveAll(this);
		ClearTradeButton->OnClicked.AddDynamic(this, &UPlayerMenuWidget::ClearTrade);
	}

	ClearDetails();
	SetDetailsVisibility(false);

	UpdateMerchantModeVisibility();
	UpdateCurrencyUI();
}

void UPlayerMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	bInventoryDirty = true;
	RefreshPlayerInventoryGrid();
	RefreshMerchantInventoryGrid();

	RefreshStatsText();
	RefreshEffectsText();

	UpdateMerchantModeVisibility();
	UpdateCurrencyUI();
	ApplyDetails();
}

void UPlayerMenuWidget::NativeDestruct()
{
	if (Stats)     Stats->OnStatsChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleStatsChanged);
	if (Effects)   Effects->OnEffectsChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleEffectsChanged);
	if (Inventory) Inventory->OnInventoryChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleInventoryChanged);

	Super::NativeDestruct();
}

FReply UPlayerMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!IsInventoryTabActive())
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const FKey Key = InKeyEvent.GetKey();
	const bool bRight = (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right);
	const bool bLeft  = (Key == EKeys::Left  || Key == EKeys::Gamepad_DPad_Left);

	if (bRight && HandleWrapHorizontal(true))  return FReply::Handled();
	if (bLeft  && HandleWrapHorizontal(false)) return FReply::Handled();

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPlayerMenuWidget::InitializeFromComponents(UPlayerStatsComponent* InStats, UStatusEffectComponent* InEffects)
{
	if (Stats)   Stats->OnStatsChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleStatsChanged);
	if (Effects) Effects->OnEffectsChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleEffectsChanged);

	Stats = InStats;
	Effects = InEffects;

	if (Stats)   Stats->OnStatsChanged.AddDynamic(this, &UPlayerMenuWidget::HandleStatsChanged);
	if (Effects) Effects->OnEffectsChanged.AddDynamic(this, &UPlayerMenuWidget::HandleEffectsChanged);

	ShowInventoryTab();
	UpdateCurrencyUI();
}

void UPlayerMenuWidget::InitializeInventory(UInventoryComponent* InInventory)
{
	if (Inventory) Inventory->OnInventoryChanged.RemoveDynamic(this, &UPlayerMenuWidget::HandleInventoryChanged);

	Inventory = InInventory;

	if (Inventory) Inventory->OnInventoryChanged.AddDynamic(this, &UPlayerMenuWidget::HandleInventoryChanged);

	bInventoryDirty = true;
	RefreshPlayerInventoryGrid();
	UpdateCurrencyUI();
	ApplyDetails();
}

void UPlayerMenuWidget::SetActiveMerchant(ANPCCharacter* InMerchant)
{
	ActiveMerchant = InMerchant;

	// Reset trade carts whenever merchant context changes
	SellCart.Reset();
	BuyCart.Reset();

	// Clear visuals for trade selection
	const bool bTradeModeEnabled = ActiveMerchant.IsValid();
	SelectedPlayerSlotWidget = nullptr;
	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisiblePlayerSlots)
	{
		if (!S.IsValid()) continue;
		S->SetTradeQuantityPickerEnabled(S->GetQuantity() > 1);
		S->SetTradeModeEnabled(bTradeModeEnabled);
		S->SetSelected(false);
	}
	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisibleMerchantSlots)
	{
		if (!S.IsValid()) continue;
		S->SetTradeModeEnabled(bTradeModeEnabled);
		S->SetSelected(false);
	}

	// Clear hover to prevent stale pointers overriding details
	HoveredSlotWidget = nullptr;

	UpdateMerchantModeVisibility();
	RefreshMerchantPanel();
	RefreshMerchantInventoryGrid();
	UpdateCurrencyUI();
	ApplyDetails();
}

void UPlayerMenuWidget::ShowInventoryTab()
{
	if (PageSwitcher) PageSwitcher->SetActiveWidgetIndex(0);
	EnsureInventoryFocus();
	UpdateMerchantModeVisibility();
	ApplyDetails();
}

void UPlayerMenuWidget::ShowStatsTab()
{
	if (PageSwitcher) PageSwitcher->SetActiveWidgetIndex(1);
	UpdateMerchantModeVisibility();
	ClearDetails();
}

void UPlayerMenuWidget::NextTab()
{
	if (!PageSwitcher) return;

	const int32 Count = PageSwitcher->GetNumWidgets();
	if (Count <= 0) return;

	PageSwitcher->SetActiveWidgetIndex((PageSwitcher->GetActiveWidgetIndex() + 1) % Count);

	if (IsInventoryTabActive())
	{
		EnsureInventoryFocus();
		ApplyDetails();
	}
	else
	{
		ClearDetails();
	}

	UpdateMerchantModeVisibility();
}

void UPlayerMenuWidget::PrevTab()
{
	if (!PageSwitcher) return;

	const int32 Count = PageSwitcher->GetNumWidgets();
	if (Count <= 0) return;

	int32 Prev = PageSwitcher->GetActiveWidgetIndex() - 1;
	if (Prev < 0) Prev = Count - 1;

	PageSwitcher->SetActiveWidgetIndex(Prev);

	if (IsInventoryTabActive())
	{
		EnsureInventoryFocus();
		ApplyDetails();
	}
	else
	{
		ClearDetails();
	}

	UpdateMerchantModeVisibility();
}

void UPlayerMenuWidget::EnsureInventoryFocus()
{
	if (!IsInventoryTabActive()) return;

	// If mouse currently hovering something, do not steal focus.
	if (HoveredSlotWidget.IsValid()) return;

	// If we have any player slots, focus first.
	if (VisiblePlayerSlots.Num() > 0 && VisiblePlayerSlots[0].IsValid())
	{
		VisiblePlayerSlots[0]->FocusSlot();
		return;
	}

	if (InventoryTabButton)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			InventoryTabButton->SetUserFocus(PC);
		}
	}
}

void UPlayerMenuWidget::ForceRefresh()
{
	RefreshStatsText();
	RefreshEffectsText();

	if (bInventoryDirty)
	{
		RefreshPlayerInventoryGrid();
	}

	// merchant panel + currency can change from relationship/carts
	RefreshMerchantPanel();
	UpdateCurrencyUI();
	ApplyDetails();
}

void UPlayerMenuWidget::HandleStatsChanged()
{
	RefreshStatsText();
	UpdateCurrencyUI();
}

void UPlayerMenuWidget::HandleEffectsChanged()
{
	RefreshEffectsText();
}

void UPlayerMenuWidget::HandleInventoryChanged()
{
	bInventoryDirty = true;

	if (IsInViewport())
	{
		RefreshPlayerInventoryGrid();
		UpdateCurrencyUI();
		ApplyDetails();
	}
}

void UPlayerMenuWidget::SetValueText(UTextBlock* TB, const FString& ValueOnly)
{
	if (TB) TB->SetText(FText::FromString(ValueOnly));
}

void UPlayerMenuWidget::SetTextTint(UTextBlock* TB, const FLinearColor& Tint)
{
	if (!TB) return;
	TB->SetColorAndOpacity(FSlateColor(Tint));
}

void UPlayerMenuWidget::RefreshStatsText()
{
	if (!Stats) return;

	SetValueText(ValHealth,  FString::Printf(TEXT("%.0f/%.0f"), Stats->Health, Stats->MaxHealth));
	SetValueText(ValStamina, FString::Printf(TEXT("%.0f/%.0f"), Stats->Stamina, Stats->MaxStamina));
	SetValueText(ValMagic,   FString::Printf(TEXT("%.0f/%.0f"), Stats->Magic, Stats->MaxMagic));

	SetValueText(ValBaseDamageOutput,    FString::Printf(TEXT("%.1f"), Stats->BaseDamageOutput));
	SetValueText(ValBaseDamageReduction, FString::Printf(TEXT("%.1f"), Stats->BaseDamageReduction));

	SetValueText(ValStrength,  FString::Printf(TEXT("%d"), Stats->Strength));
	SetValueText(ValEndurance, FString::Printf(TEXT("%d"), Stats->Endurance));
	SetValueText(ValWillpower, FString::Printf(TEXT("%d"), Stats->Willpower));
	SetValueText(ValLuck,      FString::Printf(TEXT("%d"), Stats->Luck));
}

void UPlayerMenuWidget::RefreshEffectsText()
{
	if (!Effects)
	{
		SetValueText(ValPoison, TEXT("-"));
		SetValueText(ValFear,   TEXT("-"));
		SetValueText(ValBurn,   TEXT("-"));
		SetValueText(ValFrost,  TEXT("-"));
		SetValueText(ValBleed,  TEXT("-"));
		return;
	}

	SetValueText(ValPoison, FString::Printf(TEXT("%.1fs"), Effects->GetPoisonTimeRemaining()));
	SetValueText(ValFear,   FString::Printf(TEXT("%.0f"),  Effects->GetFearPoints()));
	SetValueText(ValBurn,   Effects->IsBurned() ? TEXT("Burned") : TEXT("No"));
	SetValueText(ValFrost,  FString::Printf(TEXT("%.0f"),  Effects->GetFrostPoints()));
	SetValueText(ValBleed,  FString::Printf(TEXT("%.0f"),  Effects->GetBleedPoints()));
}

UTexture2D* UPlayerMenuWidget::GetRarityIcon(EItemRarity InRarity) const
{
	switch (InRarity)
	{
	case EItemRarity::Garbage:    return Rarity_Common;
	case EItemRarity::Acceptable: return Rarity_Uncommon;
	case EItemRarity::Fair:       return Rarity_Rare;
	case EItemRarity::Perfect:    return Rarity_Epic;
	default:                      return nullptr;
	}
}

FLinearColor UPlayerMenuWidget::GetRarityTint(EItemRarity InRarity) const
{
	switch (InRarity)
	{
	case EItemRarity::Garbage:    return RarityTint_Garbage;
	case EItemRarity::Acceptable: return RarityTint_Acceptable;
	case EItemRarity::Fair:       return RarityTint_Fair;
	case EItemRarity::Perfect:    return RarityTint_Perfect;
	default:                      return FLinearColor::White;
	}
}

void UPlayerMenuWidget::SetDetailsVisibility(bool bVisible)
{
	if (VB_ItemDetails)
	{
		VB_ItemDetails->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UPlayerMenuWidget::ClearDetails()
{
	if (DetailIconImage) DetailIconImage->SetBrushFromTexture(nullptr, true);
	if (DetailRarityImage)
	{
		DetailRarityImage->SetBrushFromTexture(nullptr, true);
		DetailRarityImage->SetVisibility(ESlateVisibility::Hidden);
	}

	if (DetailNameText) DetailNameText->SetText(FText::GetEmpty());
	if (DetailDescriptionText) DetailDescriptionText->SetText(FText::GetEmpty());

	SetDetailsVisibility(false);
}

float UPlayerMenuWidget::GetRarityMultiplier(EItemRarity Rarity) const
{
	switch (Rarity)
	{
	case EItemRarity::Garbage:    return FMath::Max(0.f, SellMultiplier_Garbage);
	case EItemRarity::Acceptable: return FMath::Max(0.f, SellMultiplier_Acceptable);
	case EItemRarity::Fair:       return FMath::Max(0.f, SellMultiplier_Fair);
	case EItemRarity::Perfect:    return FMath::Max(0.f, SellMultiplier_Perfect);
	default:                      return 1.f;
	}
}

int32 UPlayerMenuWidget::GetRaritySellValue(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity) const
{
	if (!Item || Quantity <= 0) return 0;

	const int32 BaseUnit = FMath::Max(0, Item->BaseSellValue);
	if (BaseUnit <= 0) return 0;
	const float Mult = GetRarityMultiplier(Rarity);
	if (Mult <= 0.f) return 0;

	int32 UnitValue = FMath::FloorToInt((float)BaseUnit * Mult);
	UnitValue = FMath::Max(1, UnitValue);
	return UnitValue * Quantity;
}

void UPlayerMenuWidget::SetDetailsFromPlayerSlot(UInventorySlotWidget* SlotWidget)
{
	if (!SlotWidget || !SlotWidget->GetItem())
	{
		ClearDetails();
		return;
	}

	UItemDataAsset* Item = SlotWidget->GetItem();
	const int32 Qty = SlotWidget->GetQuantity();
	const EItemRarity Rar = SlotWidget->GetRarity();

	SetDetailsVisibility(true);

	if (DetailIconImage) DetailIconImage->SetBrushFromTexture(Item->Icon, true);

	if (DetailRarityImage)
	{
		UTexture2D* RarityIconTexture = GetRarityIcon(Rar);
		DetailRarityImage->SetBrushFromTexture(RarityIconTexture, true);
		DetailRarityImage->SetColorAndOpacity(GetRarityTint(Rar));
		DetailRarityImage->SetVisibility(RarityIconTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (DetailNameText)
	{
		DetailNameText->SetText(Item->DisplayName.IsEmpty() ? FText::FromName(Item->ItemId) : Item->DisplayName);
	}

	if (DetailDescriptionText)
	{
		const FString Desc = FString::Printf(
			TEXT("Qty: %d\n\n(No description yet)"),
			Qty
		);

		DetailDescriptionText->SetText(FText::FromString(Desc));
	}
}

void UPlayerMenuWidget::SetDetailsFromMerchantSlot(UInventorySlotWidget* SlotWidget, const FMerchantInventoryEntry& Entry)
{
	if (!SlotWidget || !Entry.Item)
	{
		ClearDetails();
		return;
	}

	UItemDataAsset* Item = Entry.Item;
	SetDetailsVisibility(true);

	if (DetailIconImage) DetailIconImage->SetBrushFromTexture(Item->Icon, true);

	if (DetailRarityImage)
	{
		UTexture2D* RarityIconTexture = GetRarityIcon(EItemRarity::Acceptable);
		DetailRarityImage->SetBrushFromTexture(RarityIconTexture, true);
		DetailRarityImage->SetColorAndOpacity(GetRarityTint(EItemRarity::Acceptable));
		DetailRarityImage->SetVisibility(RarityIconTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (DetailNameText)
	{
		DetailNameText->SetText(Item->DisplayName.IsEmpty() ? FText::FromName(Item->ItemId) : Item->DisplayName);
	}

	if (DetailDescriptionText)
	{
		const int32 Stock = Entry.bInfiniteStock ? -1 : Entry.Stock;
		const FString StockStr = Entry.bInfiniteStock ? TEXT("∞") : FString::FromInt(FMath::Max(0, Stock));

		const FString Desc = FString::Printf(
			TEXT("Stock: %s\n\n(No description yet)"),
			*StockStr
		);

		DetailDescriptionText->SetText(FText::FromString(Desc));
	}
}

void UPlayerMenuWidget::ApplyDetails()
{
	if (!IsInventoryTabActive())
	{
		ClearDetails();
		return;
	}

	// Hover always wins
	if (HoveredSlotWidget.IsValid())
	{
		const int32 MerchantIdx = VisibleMerchantSlots.IndexOfByPredicate([this](const TWeakObjectPtr<UInventorySlotWidget>& W)
		{
			return W.Get() == HoveredSlotWidget.Get();
		});

		if (ActiveMerchant.IsValid() && MerchantIdx != INDEX_NONE && CachedMerchantEntries.IsValidIndex(MerchantIdx))
		{
			SetDetailsFromMerchantSlot(HoveredSlotWidget.Get(), CachedMerchantEntries[MerchantIdx]);
			return;
		}

		SetDetailsFromPlayerSlot(HoveredSlotWidget.Get());
		return;
	}

	// In non-trade mode, details default to the selected player slot.
	if (!ActiveMerchant.IsValid() && SelectedPlayerSlotWidget.IsValid())
	{
		SetDetailsFromPlayerSlot(SelectedPlayerSlotWidget.Get());
		return;
	}

	// Trade mode stays hover-only.
	ClearDetails();
}

void UPlayerMenuWidget::HandleSlotHovered(UInventorySlotWidget* HoveredWidget)
{
	if (!HoveredWidget) return;
	HoveredSlotWidget = HoveredWidget;
	ApplyDetails();
}

void UPlayerMenuWidget::HandleSlotUnhovered(UInventorySlotWidget* UnhoveredWidget)
{
	if (!UnhoveredWidget) return;

	if (HoveredSlotWidget.Get() == UnhoveredWidget)
	{
		HoveredSlotWidget = nullptr;
	}

	ApplyDetails();
}

void UPlayerMenuWidget::HandlePlayerSlotClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (!ActiveMerchant.IsValid())
	{
		const bool bAlreadySelected = (SelectedPlayerSlotWidget.Get() == ClickedWidget);

		if (SelectedPlayerSlotWidget.IsValid() && SelectedPlayerSlotWidget.Get() != ClickedWidget)
		{
			SelectedPlayerSlotWidget->SetSelected(false);
		}

		if (bAlreadySelected)
		{
			ClickedWidget->SetSelected(false);
			SelectedPlayerSlotWidget = nullptr;
		}
		else
		{
			ClickedWidget->SetSelected(true);
			SelectedPlayerSlotWidget = ClickedWidget;
		}

		ApplyDetails();
		return;
	}

	// In merchant mode, click toggles selection; single-item stacks auto-add qty 1.
	const bool bUseQtyPicker = (ClickedWidget->GetQuantity() > 1);
	ClickedWidget->SetTradeQuantityPickerEnabled(bUseQtyPicker);
	ClickedWidget->SetTradeModeEnabled(true);

	const bool bShouldSelect = !ClickedWidget->IsSelected();
	FSellKey Key;
	Key.Item = ClickedWidget->GetItem();
	Key.Rarity = ClickedWidget->GetRarity();

	SellCart.Remove(Key);

	if (bShouldSelect)
	{
		ClickedWidget->SetSelectedTradeQuantity(0);
		ClickedWidget->SetSelected(true);

		// Single-item stacks bypass qty picker and immediately become qty 1 in cart.
		if (!bUseQtyPicker && !AdjustSellCartQuantity(ClickedWidget, +1))
		{
			ClickedWidget->SetSelected(false);
		}
	}
	else
	{
		ClickedWidget->SetSelected(false);
	}

	UpdateCurrencyUI();
	ApplyDetails();
}

void UPlayerMenuWidget::HandlePlayerSlotAddClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (AdjustSellCartQuantity(ClickedWidget, +1))
	{
		UpdateCurrencyUI();
		ApplyDetails();
	}
}

void UPlayerMenuWidget::HandlePlayerSlotSubClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (AdjustSellCartQuantity(ClickedWidget, -1))
	{
		UpdateCurrencyUI();
		ApplyDetails();
	}
}

void UPlayerMenuWidget::HandleMerchantSlotClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (ActiveMerchant.IsValid())
	{
		const int32 Idx = VisibleMerchantSlots.IndexOfByPredicate([ClickedWidget](const TWeakObjectPtr<UInventorySlotWidget>& W)
		{
			return W.Get() == ClickedWidget;
		});

		bool bUseQtyPicker = true;
		if (CachedMerchantEntries.IsValidIndex(Idx))
		{
			const FMerchantInventoryEntry& Entry = CachedMerchantEntries[Idx];
			bUseQtyPicker = (Entry.bInfiniteStock || Entry.Stock > 1);
		}

		ClickedWidget->SetTradeQuantityPickerEnabled(bUseQtyPicker);
		ClickedWidget->SetTradeModeEnabled(true);
		const bool bShouldSelect = !ClickedWidget->IsSelected();
		BuyCart.Remove(ClickedWidget->GetItem());

		if (bShouldSelect)
		{
			ClickedWidget->SetSelectedTradeQuantity(0);
			ClickedWidget->SetSelected(true);

			// Single-stock entries bypass qty picker and immediately become qty 1 in cart.
			if (!bUseQtyPicker && !AdjustBuyCartQuantity(ClickedWidget, +1))
			{
				ClickedWidget->SetSelected(false);
			}
		}
		else
		{
			ClickedWidget->SetSelected(false);
		}

		UpdateCurrencyUI();
	}

	ApplyDetails();
}

void UPlayerMenuWidget::HandleMerchantSlotAddClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (AdjustBuyCartQuantity(ClickedWidget, +1))
	{
		UpdateCurrencyUI();
		ApplyDetails();
	}
}

void UPlayerMenuWidget::HandleMerchantSlotSubClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	if (AdjustBuyCartQuantity(ClickedWidget, -1))
	{
		UpdateCurrencyUI();
		ApplyDetails();
	}
}

void UPlayerMenuWidget::RefreshPlayerInventoryGrid()
{
	bInventoryDirty = false;

	// If we rebuild, any hover pointer is stale.
	HoveredSlotWidget = nullptr;

	const bool bTradeModeEnabled = ActiveMerchant.IsValid();
	UItemDataAsset* SelectedItemToRestore = nullptr;
	EItemRarity SelectedRarityToRestore = EItemRarity::Garbage;
	if (!bTradeModeEnabled && SelectedPlayerSlotWidget.IsValid() && SelectedPlayerSlotWidget->GetItem())
	{
		SelectedItemToRestore = SelectedPlayerSlotWidget->GetItem();
		SelectedRarityToRestore = SelectedPlayerSlotWidget->GetRarity();
	}
	SelectedPlayerSlotWidget = nullptr;

	VisiblePlayerSlots.Reset();

	if (!InventoryGrid)
	{
		UE_LOG(LogTemp, Error, TEXT("PlayerMenuWidget: InventoryGrid is NULL. Must be named InventoryGrid and be 'Is Variable'."));
		return;
	}

	InventoryGrid->ClearChildren();
	InventoryGrid->SetSlotPadding(FMargin(SlotPadding));

	if (!Inventory || !InventorySlotWidgetClass)
	{
		SelectedPlayerSlotWidget = nullptr;
		ClearDetails();
		return;
	}

	const TArray<FItemStack>& Items = Inventory->GetItems();
	if (Items.Num() == 0)
	{
		SelectedPlayerSlotWidget = nullptr;
		ClearDetails();
		return;
	}

	const int32 Cols = FMath::Max(1, GridColumns);
	int32 VisibleIdx = 0;

	for (const FItemStack& Stack : Items)
	{
		if (!Stack.Item || Stack.Quantity <= 0) continue;

		int32 StackSellValue = GetRaritySellValue(Stack.Item, Stack.Quantity, Stack.Rarity);
		if (bTradeModeEnabled && ActiveMerchant.IsValid())
		{
			StackSellValue = ActiveMerchant->GetSellValueForItemRarity(Stack.Item, Stack.Quantity, Stack.Rarity);
			if (StackSellValue <= 0)
			{
				FSellKey UnsellableKey;
				UnsellableKey.Item = Stack.Item;
				UnsellableKey.Rarity = Stack.Rarity;
				SellCart.Remove(UnsellableKey);
				continue;
			}
		}

		UInventorySlotWidget* NewSlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!NewSlotWidget) continue;

		const int32 Row = VisibleIdx / Cols;
		const int32 Col = VisibleIdx % Cols;

		if (UUniformGridSlot* GridSlot = InventoryGrid->AddChildToUniformGrid(NewSlotWidget, Row, Col))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Center);
			GridSlot->SetVerticalAlignment(VAlign_Center);
		}

		NewSlotWidget->SetupSlot(Stack.Item, Stack.Quantity, Stack.Rarity, GetRarityIcon(Stack.Rarity), GetRarityTint(Stack.Rarity));
		NewSlotWidget->SetTradeQuantityPickerEnabled(Stack.Quantity > 1);
		NewSlotWidget->SetTradeModeEnabled(bTradeModeEnabled);
		NewSlotWidget->SetItemCostText(FText::AsNumber(FMath::Max(0, StackSellValue)));

		NewSlotWidget->OnSlotHovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotHovered);
		NewSlotWidget->OnSlotUnhovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotUnhovered);
		NewSlotWidget->OnSlotClicked.AddDynamic(this, &UPlayerMenuWidget::HandlePlayerSlotClicked);
		NewSlotWidget->OnSlotAddClicked.AddDynamic(this, &UPlayerMenuWidget::HandlePlayerSlotAddClicked);
		NewSlotWidget->OnSlotSubClicked.AddDynamic(this, &UPlayerMenuWidget::HandlePlayerSlotSubClicked);

		// Restore cart-backed selected state and selected quantity in trade mode.
		if (bTradeModeEnabled)
		{
			FSellKey Key;
			Key.Item = Stack.Item;
			Key.Rarity = Stack.Rarity;

			const FSellLine* ExistingLine = SellCart.Find(Key);
			const int32 RestoredQty = ExistingLine ? FMath::Max(0, ExistingLine->Quantity) : 0;

			NewSlotWidget->SetSelectedTradeQuantity(RestoredQty);
			NewSlotWidget->SetSelected(RestoredQty > 0);
		}
		else
		{
			const bool bIsSelectedInMenu = (Stack.Item == SelectedItemToRestore && Stack.Rarity == SelectedRarityToRestore);
			NewSlotWidget->SetSelected(bIsSelectedInMenu);
			if (bIsSelectedInMenu)
			{
				SelectedPlayerSlotWidget = NewSlotWidget;
			}
		}

		VisiblePlayerSlots.Add(NewSlotWidget);
		VisibleIdx++;
	}

	EnsureInventoryFocus();
	ApplyDetails();
}

void UPlayerMenuWidget::RefreshMerchantInventoryGrid()
{
	// If we rebuild, any hover pointer is stale.
	HoveredSlotWidget = nullptr;

	VisibleMerchantSlots.Reset();
	CachedMerchantEntries.Reset();

	if (!MerchantInventoryGrid)
	{
		return; // optional
	}

	MerchantInventoryGrid->ClearChildren();
	MerchantInventoryGrid->SetSlotPadding(FMargin(SlotPadding));

	if (!ActiveMerchant.IsValid() || !InventorySlotWidgetClass)
	{
		return;
	}

	CachedMerchantEntries = ActiveMerchant->GetUnlockedMerchantInventory();

	const int32 Cols = FMath::Max(1, GridColumns);
	int32 VisibleIdx = 0;
	const bool bTradeModeEnabled = ActiveMerchant.IsValid();

	for (const FMerchantInventoryEntry& Entry : CachedMerchantEntries)
	{
		if (!Entry.Item) continue;

		UInventorySlotWidget* NewMerchantSlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!NewMerchantSlotWidget) continue;

		const int32 Row = VisibleIdx / Cols;
		const int32 Col = VisibleIdx % Cols;

		if (UUniformGridSlot* GridSlot = MerchantInventoryGrid->AddChildToUniformGrid(NewMerchantSlotWidget, Row, Col))
		{
			GridSlot->SetHorizontalAlignment(HAlign_Center);
			GridSlot->SetVerticalAlignment(VAlign_Center);
		}

		// Show stock in QtyText (hide if 1 or infinite)
		const int32 DisplayQty = Entry.bInfiniteStock ? 1 : FMath::Max(0, Entry.Stock);

		NewMerchantSlotWidget->SetupSlot(
			Entry.Item,
			DisplayQty,
			EItemRarity::Acceptable,
			GetRarityIcon(EItemRarity::Acceptable),
			GetRarityTint(EItemRarity::Acceptable)
		);
		NewMerchantSlotWidget->SetTradeQuantityPickerEnabled(Entry.bInfiniteStock || Entry.Stock > 1);
		NewMerchantSlotWidget->SetTradeModeEnabled(bTradeModeEnabled);
		NewMerchantSlotWidget->SetItemCostText(FText::AsNumber(FMath::Max(0, Entry.BuyPrice)));

		NewMerchantSlotWidget->OnSlotHovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotHovered);
		NewMerchantSlotWidget->OnSlotUnhovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotUnhovered);
		NewMerchantSlotWidget->OnSlotClicked.AddDynamic(this, &UPlayerMenuWidget::HandleMerchantSlotClicked);
		NewMerchantSlotWidget->OnSlotAddClicked.AddDynamic(this, &UPlayerMenuWidget::HandleMerchantSlotAddClicked);
		NewMerchantSlotWidget->OnSlotSubClicked.AddDynamic(this, &UPlayerMenuWidget::HandleMerchantSlotSubClicked);

		// Restore cart-backed selected state and selected quantity in trade mode.
		const FBuyLine* ExistingLine = BuyCart.Find(Entry.Item);
		const int32 RestoredQty = ExistingLine ? FMath::Max(0, ExistingLine->Quantity) : 0;
		NewMerchantSlotWidget->SetSelectedTradeQuantity(RestoredQty);
		NewMerchantSlotWidget->SetSelected(RestoredQty > 0);

		VisibleMerchantSlots.Add(NewMerchantSlotWidget);
		VisibleIdx++;
	}
}

void UPlayerMenuWidget::RefreshMerchantPanel()
{
	if (!ActiveMerchant.IsValid())
	{
		return;
	}

	// Name
	if (MerchantName)
	{
		MerchantName->SetText(ActiveMerchant->GetMerchantDisplayName());
	}

	// Relationship UI
	if (TXT_RelLevelCurrent)
	{
		TXT_RelLevelCurrent->SetText(FText::AsNumber(ActiveMerchant->GetRelationshipLevel()));
	}

	if (TXT_RelLevelNext)
	{
		const int32 Cur = ActiveMerchant->GetRelationshipLevel();
		const int32 Next = FMath::Clamp(Cur + 1, 0, 5);
		TXT_RelLevelNext->SetText(FText::AsNumber(Next));
	}

	if (PB_Relationship)
	{
		PB_Relationship->SetPercent(ActiveMerchant->GetRelationshipProgress01());
	}
}

void UPlayerMenuWidget::UpdateMerchantModeVisibility()
{
	const bool bTrade = ActiveMerchant.IsValid();
	const bool bInventoryPage = IsInventoryTabActive();
	const bool bShowTradeUI = bTrade && bInventoryPage;

	if (SB_MerchantWindow)
	{
		SB_MerchantWindow->SetVisibility(bTrade ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// HARD guarantee: control the container
	if (HB_TradeButtons)
	{
		HB_TradeButtons->SetVisibility(bShowTradeUI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	const bool bHasCart = (SellCart.Num() > 0 || BuyCart.Num() > 0);

	// Confirm: only enabled when there is something to trade
	if (BTN_ConfirmTrade)
	{
		BTN_ConfirmTrade->SetVisibility(bShowTradeUI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		BTN_ConfirmTrade->SetIsEnabled(bShowTradeUI && bHasCart);
	}
	if (ConfirmTradeButton)
	{
		ConfirmTradeButton->SetVisibility(bShowTradeUI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		ConfirmTradeButton->SetIsEnabled(bShowTradeUI && bHasCart);
	}

	// Clear: always enabled in trade mode (even if empty, it still "works")
	if (BTN_ClearTrade)
	{
		BTN_ClearTrade->SetVisibility(bShowTradeUI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		BTN_ClearTrade->SetIsEnabled(bShowTradeUI);
	}
	if (ClearTradeButton)
	{
		ClearTradeButton->SetVisibility(bShowTradeUI ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		ClearTradeButton->SetIsEnabled(bShowTradeUI);
	}

	// PlayerCurrency should always be shown (parent widgets can still override this in BP)
	if (PlayerCurrency)
	{
		PlayerCurrency->SetVisibility(ESlateVisibility::Visible);
	}

	// Hide NavBar only in trade screen (your existing behavior)
	if (NavBar)
	{
		NavBar->SetVisibility(bTrade ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void UPlayerMenuWidget::RecomputeTradePreview(int32& OutSellTotal, int32& OutBuyTotal, int32& OutPlayerPreview, int32& OutMerchantPreview) const
{
	OutSellTotal = 0;
	OutBuyTotal = 0;

	const int32 PlayerStart = Stats ? Stats->GetCurrency() : 0;
	const int32 MerchantStart = ActiveMerchant.IsValid() ? ActiveMerchant->GetCurrentCurrency() : 0;

	for (const auto& Pair : SellCart)
	{
		OutSellTotal += FMath::Max(0, Pair.Value.Value);
	}

	for (const auto& Pair : BuyCart)
	{
		const int32 Cost = FMath::Max(0, Pair.Value.UnitPrice) * FMath::Max(0, Pair.Value.Quantity);
		OutBuyTotal += Cost;
	}

	OutPlayerPreview = PlayerStart + OutSellTotal - OutBuyTotal;
	OutMerchantPreview = MerchantStart - OutSellTotal + OutBuyTotal;
}

void UPlayerMenuWidget::UpdateCurrencyUI()
{
	// Player currency ALWAYS shown
	const bool bTrade = ActiveMerchant.IsValid();

	int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
	RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

	const int32 PlayerStart = Stats ? Stats->GetCurrency() : 0;
	const int32 MerchantStart = ActiveMerchant.IsValid() ? ActiveMerchant->GetCurrentCurrency() : 0;

	const int32 PlayerDisplay = bTrade ? PlayerPrev : PlayerStart;
	const int32 MerchantDisplay = bTrade ? MerchantPrev : MerchantStart;

	if (PlayerCurrency)
	{
		PlayerCurrency->SetText(FText::AsNumber(PlayerDisplay));

		if (!bTrade) SetTextTint(PlayerCurrency, CurrencyNeutralTint);
		else
		{
			if (PlayerDisplay > PlayerStart) SetTextTint(PlayerCurrency, CurrencyGainTint);
			else if (PlayerDisplay < PlayerStart) SetTextTint(PlayerCurrency, CurrencyLossTint);
			else SetTextTint(PlayerCurrency, CurrencyNeutralTint);
		}
	}

	if (MerchantCurrency)
	{
		MerchantCurrency->SetText(FText::AsNumber(MerchantDisplay));

		if (!bTrade) SetTextTint(MerchantCurrency, CurrencyNeutralTint);
		else
		{
			if (MerchantDisplay > MerchantStart) SetTextTint(MerchantCurrency, CurrencyGainTint);
			else if (MerchantDisplay < MerchantStart) SetTextTint(MerchantCurrency, CurrencyLossTint);
			else SetTextTint(MerchantCurrency, CurrencyNeutralTint);
		}
	}

	UpdateMerchantModeVisibility();
}

bool UPlayerMenuWidget::AdjustSellCartQuantity(UInventorySlotWidget* SlotWidget, int32 DeltaQty)
{
	if (!ActiveMerchant.IsValid() || !Stats || !SlotWidget || !SlotWidget->GetItem() || DeltaQty == 0) return false;
	if (!SlotWidget->IsSelected()) return false;

	UItemDataAsset* Item = SlotWidget->GetItem();
	const EItemRarity Rarity = SlotWidget->GetRarity();
	const int32 OwnedQty = FMath::Max(0, SlotWidget->GetQuantity());
	if (OwnedQty <= 0) return false;

	FSellKey Key;
	Key.Item = Item;
	Key.Rarity = Rarity;

	const FSellLine* ExistingLine = SellCart.Find(Key);
	const int32 CurrentQty = ExistingLine ? FMath::Max(0, ExistingLine->Quantity) : FMath::Max(0, SlotWidget->GetSelectedTradeQuantity());

	if (DeltaQty < 0 && CurrentQty <= 0)
	{
		SellCart.Remove(Key);
		SlotWidget->SetSelected(false);
		return true;
	}

	int32 ProposedQty = CurrentQty + DeltaQty;
	if (DeltaQty > 0)
	{
		ProposedQty = FMath::Min(ProposedQty, OwnedQty);
		if (ProposedQty == CurrentQty)
		{
			return false;
		}
	}
	else
	{
		ProposedQty = FMath::Max(0, ProposedQty);
	}

	if (ProposedQty <= 0)
	{
		SellCart.Remove(Key);
		SlotWidget->SetSelected(false);
		return true;
	}

	int32 ProposedValue = GetRaritySellValue(Item, ProposedQty, Rarity);
	if (ActiveMerchant.IsValid())
	{
		ProposedValue = ActiveMerchant->GetSellValueForItemRarity(Item, ProposedQty, Rarity);
	}

	if (ProposedValue <= 0)
	{
		SellCart.Remove(Key);
		SlotWidget->SetSelected(false);
		return true;
	}

	if (DeltaQty > 0)
	{
		// Keep merchant preview currency >= 0.
		int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
		RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

		const int32 CurrentLineValue = ExistingLine ? FMath::Max(0, ExistingLine->Value) : 0;
		const int32 SellWithoutThis = FMath::Max(0, SellTotal - CurrentLineValue);
		const int32 MerchantStart = ActiveMerchant->GetCurrentCurrency();
		const int32 ProspectiveMerchant = MerchantStart - (SellWithoutThis + ProposedValue) + BuyTotal;
		if (ProspectiveMerchant < 0)
		{
			return false;
		}
	}

	FSellLine NewLine;
	NewLine.Item = Item;
	NewLine.Quantity = ProposedQty;
	NewLine.Rarity = Rarity;
	NewLine.Value = ProposedValue;
	SellCart.Add(Key, NewLine);

	SlotWidget->SetSelectedTradeQuantity(ProposedQty);
	return ProposedQty != CurrentQty;
}

bool UPlayerMenuWidget::AdjustBuyCartQuantity(UInventorySlotWidget* SlotWidget, int32 DeltaQty)
{
	if (!ActiveMerchant.IsValid() || !Stats || !SlotWidget || !SlotWidget->GetItem() || DeltaQty == 0) return false;
	if (!SlotWidget->IsSelected()) return false;

	const int32 MerchantSlotIndex = VisibleMerchantSlots.IndexOfByPredicate([SlotWidget](const TWeakObjectPtr<UInventorySlotWidget>& W)
	{
		return W.Get() == SlotWidget;
	});
	if (!CachedMerchantEntries.IsValidIndex(MerchantSlotIndex)) return false;

	const FMerchantInventoryEntry& Entry = CachedMerchantEntries[MerchantSlotIndex];
	if (!Entry.Item) return false;

	const int32 UnitPrice = FMath::Max(0, Entry.BuyPrice);
	const int32 MaxQty = Entry.bInfiniteStock ? 99 : FMath::Max(0, Entry.Stock);
	if (MaxQty <= 0)
	{
		BuyCart.Remove(Entry.Item);
		SlotWidget->SetSelected(false);
		return true;
	}

	const FBuyLine* ExistingLine = BuyCart.Find(Entry.Item);
	const int32 CurrentQty = ExistingLine ? FMath::Max(0, ExistingLine->Quantity) : FMath::Max(0, SlotWidget->GetSelectedTradeQuantity());

	if (DeltaQty < 0 && CurrentQty <= 0)
	{
		BuyCart.Remove(Entry.Item);
		SlotWidget->SetSelected(false);
		return true;
	}

	int32 ProposedQty = CurrentQty + DeltaQty;
	if (DeltaQty > 0)
	{
		ProposedQty = FMath::Min(ProposedQty, MaxQty);
		if (ProposedQty == CurrentQty)
		{
			return false;
		}

		// Keep player preview currency >= 0.
		int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
		RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

		const int32 CurrentLineCost = ExistingLine ? FMath::Max(0, ExistingLine->UnitPrice) * FMath::Max(0, ExistingLine->Quantity) : 0;
		const int32 BuyWithoutThis = FMath::Max(0, BuyTotal - CurrentLineCost);
		const int32 ProposedBuyTotal = BuyWithoutThis + (UnitPrice * ProposedQty);

		const int32 PlayerStart = Stats->GetCurrency();
		const int32 ProspectivePlayer = PlayerStart + SellTotal - ProposedBuyTotal;
		if (ProspectivePlayer < 0)
		{
			return false;
		}
	}
	else
	{
		ProposedQty = FMath::Max(0, ProposedQty);
	}

	if (ProposedQty <= 0)
	{
		BuyCart.Remove(Entry.Item);
		SlotWidget->SetSelected(false);
		return true;
	}

	FBuyLine NewLine;
	NewLine.Item = Entry.Item;
	NewLine.Quantity = ProposedQty;
	NewLine.UnitPrice = UnitPrice;
	BuyCart.Add(Entry.Item, NewLine);

	SlotWidget->SetSelectedTradeQuantity(ProposedQty);
	return ProposedQty != CurrentQty;
}

void UPlayerMenuWidget::ClearTrade()
{
	UE_LOG(LogTemp, Warning, TEXT("ClearTrade CLICK: MerchantValid=%d SellCart=%d BuyCart=%d"),
		ActiveMerchant.IsValid() ? 1 : 0,
		SellCart.Num(),
		BuyCart.Num()
	);

	SellCart.Reset();
	BuyCart.Reset();

	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisiblePlayerSlots)
	{
		if (S.IsValid()) S->SetSelected(false);
	}
	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisibleMerchantSlots)
	{
		if (S.IsValid()) S->SetSelected(false);
	}

	UpdateCurrencyUI();
}

void UPlayerMenuWidget::ConfirmTrade()
{
	UE_LOG(LogTemp, Warning, TEXT("ConfirmTrade CLICK: MerchantValid=%d SellCart=%d BuyCart=%d"),
		ActiveMerchant.IsValid() ? 1 : 0,
		SellCart.Num(),
		BuyCart.Num()
	);

	if (!ActiveMerchant.IsValid() || !Inventory || !Stats) return;

	// Final preview validation
	int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
	RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

	if (PlayerPrev < 0 || MerchantPrev < 0)
	{
		return;
	}

	ANPCCharacter* Merchant = ActiveMerchant.Get();

	// 1) Sell selected player stacks to merchant (merchant pays player)
	for (const auto& Pair : SellCart)
	{
		const FSellLine& Line = Pair.Value;
		if (!Line.Item || Line.Quantity <= 0) continue;

		// Remove from player inventory first
		if (!Inventory->RemoveItemExact(Line.Item, Line.Quantity, Line.Rarity))
		{
			continue;
		}

		// Pay player + take merchant currency
		Stats->ModifyCurrency(Line.Value);
		Merchant->ModifyMerchantCurrency(-Line.Value);

		// Add resale stock so it shows up in merchant inventory
		Merchant->AddResaleStock(Line.Item, Line.Quantity);
		Merchant->AwardRelationshipForSale(Line.Item, Line.Quantity);
	}

	// 2) Buy selected merchant items (player pays merchant)
	for (const auto& Pair : BuyCart)
	{
		const FBuyLine& Line = Pair.Value;
		if (!Line.Item || Line.Quantity <= 0) continue;

		int32 Cost = 0;
		if (!Merchant->TrySellToPlayer(Line.Item, Line.Quantity, Cost))
		{
			continue;
		}

		if (!Stats->SpendCurrency(Cost))
		{
			continue;
		}

		// Commit merchant stock/currency only after player payment succeeds.
		if (!Merchant->CompleteSellToPlayer(Line.Item, Line.Quantity, Cost))
		{
			// Merchant state changed between preview and commit; refund player.
			Stats->ModifyCurrency(Cost);
			continue;
		}

		// Default rarity for purchases (you can change this later)
		Inventory->AddItem(Line.Item, Line.Quantity, EItemRarity::Acceptable);
	}

	// Clear carts + refresh UI
	ClearTrade();

	RefreshPlayerInventoryGrid();
	RefreshMerchantInventoryGrid();
	RefreshMerchantPanel();
	UpdateCurrencyUI();
	ApplyDetails();
}

bool UPlayerMenuWidget::IsInventoryTabActive() const
{
	return PageSwitcher && PageSwitcher->GetActiveWidgetIndex() == 0;
}

int32 UPlayerMenuWidget::FindPlayerSlotIndex(const UInventorySlotWidget* SlotWidget) const
{
	if (!SlotWidget) return INDEX_NONE;

	for (int32 i = 0; i < VisiblePlayerSlots.Num(); i++)
	{
		if (VisiblePlayerSlots[i].Get() == SlotWidget) return i;
	}

	return INDEX_NONE;
}

int32 UPlayerMenuWidget::GetFocusedPlayerSlotIndex() const
{
	if (HoveredSlotWidget.IsValid())
	{
		return FindPlayerSlotIndex(HoveredSlotWidget.Get());
	}
	return INDEX_NONE;
}

void UPlayerMenuWidget::FocusPlayerSlotIndex(int32 Index)
{
	if (Index < 0 || Index >= VisiblePlayerSlots.Num()) return;
	if (VisiblePlayerSlots[Index].IsValid()) VisiblePlayerSlots[Index]->FocusSlot();
}

bool UPlayerMenuWidget::HandleWrapHorizontal(bool bMoveRight)
{
	if (VisiblePlayerSlots.Num() <= 1) return false;

	const int32 Cols = FMath::Max(1, GridColumns);
	const int32 CurrentIndex = GetFocusedPlayerSlotIndex();
	if (CurrentIndex == INDEX_NONE) return false;

	const int32 Row = CurrentIndex / Cols;
	const int32 Col = CurrentIndex % Cols;

	if (bMoveRight)
	{
		if (Col == Cols - 1)
		{
			const int32 NextRowStart = (Row + 1) * Cols;
			if (NextRowStart < VisiblePlayerSlots.Num())
			{
				FocusPlayerSlotIndex(NextRowStart);
				return true;
			}
		}
	}
	else
	{
		if (Col == 0)
		{
			const int32 PrevRowEnd = (Row * Cols) - 1;
			if (PrevRowEnd >= 0)
			{
				FocusPlayerSlotIndex(PrevRowEnd);
				return true;
			}
		}
	}

	return false;
}
