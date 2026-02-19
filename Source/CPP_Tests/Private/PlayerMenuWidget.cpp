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

static float InferPreferredMultiplier(ANPCCharacter* Merchant, UItemDataAsset* Item, int32 Quantity)
{
	if (!Merchant || !Item || Quantity <= 0) return 1.0f;

	const int32 BaseRaw = Item->BaseSellValue * Quantity;
	if (BaseRaw <= 0) return 1.0f;

	const int32 MerchantValue = Merchant->GetSellValueForItem(Item, Quantity);
	if (MerchantValue <= 0) return 1.0f;

	return FMath::Max(0.0f, (float)MerchantValue / (float)BaseRaw);
}

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
	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisiblePlayerSlots)
	{
		if (S.IsValid()) S->SetSelected(false);
	}
	for (TWeakObjectPtr<UInventorySlotWidget>& S : VisibleMerchantSlots)
	{
		if (S.IsValid()) S->SetSelected(false);
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

	const int32 Base = Item->BaseSellValue * Quantity;
	if (Base <= 0) return 0;

	const float Mult = GetRarityMultiplier(Rarity);
	return FMath::Max(1, FMath::RoundToInt((float)Base * Mult));
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
		// Base * rarity multiplier
		int32 SellValue = GetRaritySellValue(Item, Qty, Rar);

		// If merchant active, also apply inferred preferred multiplier on top
		if (ActiveMerchant.IsValid())
		{
			const float PrefMult = InferPreferredMultiplier(ActiveMerchant.Get(), Item, Qty);
			SellValue = FMath::Max(1, FMath::RoundToInt((float)SellValue * PrefMult));
		}

		const FString Desc = FString::Printf(
			TEXT("Qty: %d\nSell Value: %d\n\n(No description yet)"),
			Qty,
			SellValue
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
		DetailRarityImage->SetBrushFromTexture(nullptr, true);
		DetailRarityImage->SetVisibility(ESlateVisibility::Hidden);
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
			TEXT("Buy Price: %d\nStock: %s\n\n(No description yet)"),
			Entry.BuyPrice,
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

	// If no hover, use detail-lock selection
	if (DetailLockedSlotWidget.IsValid())
	{
		const int32 MerchantIdx = VisibleMerchantSlots.IndexOfByPredicate([this](const TWeakObjectPtr<UInventorySlotWidget>& W)
		{
			return W.Get() == DetailLockedSlotWidget.Get();
		});

		if (ActiveMerchant.IsValid() && MerchantIdx != INDEX_NONE && CachedMerchantEntries.IsValidIndex(MerchantIdx))
		{
			SetDetailsFromMerchantSlot(DetailLockedSlotWidget.Get(), CachedMerchantEntries[MerchantIdx]);
			return;
		}

		SetDetailsFromPlayerSlot(DetailLockedSlotWidget.Get());
		return;
	}

	// Nothing selected, nothing hovered => hide details panel
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

	// Detail lock toggling: clicking the same locked item clears lock
	if (DetailLockedSlotWidget.Get() == ClickedWidget)
	{
		DetailLockedSlotWidget = nullptr;
	}
	else
	{
		DetailLockedSlotWidget = ClickedWidget;
	}

	// In merchant mode, clicking player slot toggles SELL cart selection
	if (ActiveMerchant.IsValid())
	{
		ToggleSellFromPlayerSlot(ClickedWidget);
		UpdateCurrencyUI();
	}

	ApplyDetails();
}

void UPlayerMenuWidget::HandleMerchantSlotClicked(UInventorySlotWidget* ClickedWidget)
{
	if (!ClickedWidget) return;

	// Detail lock toggling
	if (DetailLockedSlotWidget.Get() == ClickedWidget)
	{
		DetailLockedSlotWidget = nullptr;
	}
	else
	{
		DetailLockedSlotWidget = ClickedWidget;
	}

	if (ActiveMerchant.IsValid())
	{
		const int32 Idx = VisibleMerchantSlots.IndexOfByPredicate([ClickedWidget](const TWeakObjectPtr<UInventorySlotWidget>& W)
		{
			return W.Get() == ClickedWidget;
		});

		if (Idx != INDEX_NONE)
		{
			CycleBuyFromMerchantSlot(Idx);
			UpdateCurrencyUI();
		}
	}

	ApplyDetails();
}

void UPlayerMenuWidget::RefreshPlayerInventoryGrid()
{
	bInventoryDirty = false;

	// If we rebuild, any hover pointer is stale.
	HoveredSlotWidget = nullptr;

	// Cache locked selection so we can restore it to the newly created widget instance.
	UItemDataAsset* LockedItemToRestore = nullptr;
	EItemRarity LockedRarityToRestore = EItemRarity::Garbage;
	if (DetailLockedSlotWidget.IsValid() && DetailLockedSlotWidget->GetItem())
	{
		LockedItemToRestore = DetailLockedSlotWidget->GetItem();
		LockedRarityToRestore = DetailLockedSlotWidget->GetRarity();
	}

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
		ClearDetails();
		return;
	}

	const TArray<FItemStack>& Items = Inventory->GetItems();
	if (Items.Num() == 0)
	{
		ClearDetails();
		return;
	}

	const int32 Cols = FMath::Max(1, GridColumns);
	int32 VisibleIdx = 0;

	for (const FItemStack& Stack : Items)
	{
		if (!Stack.Item || Stack.Quantity <= 0) continue;

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
		NewSlotWidget->OnSlotHovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotHovered);
		NewSlotWidget->OnSlotUnhovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotUnhovered);
		NewSlotWidget->OnSlotClicked.AddDynamic(this, &UPlayerMenuWidget::HandlePlayerSlotClicked);

		// If in sell cart, keep selected visual
		if (ActiveMerchant.IsValid())
		{
			FSellKey Key;
			Key.Item = Stack.Item;
			Key.Rarity = Stack.Rarity;

			NewSlotWidget->SetSelected(SellCart.Contains(Key));
		}
		else
		{
			NewSlotWidget->SetSelected(false);
		}

		VisiblePlayerSlots.Add(NewSlotWidget);
		VisibleIdx++;
	}

	// Restore locked selection to the NEW widget instance if possible
	if (LockedItemToRestore)
	{
		DetailLockedSlotWidget = nullptr;
		for (const TWeakObjectPtr<UInventorySlotWidget>& W : VisiblePlayerSlots)
		{
			if (!W.IsValid()) continue;
			if (W->GetItem() == LockedItemToRestore && W->GetRarity() == LockedRarityToRestore)
			{
				DetailLockedSlotWidget = W.Get();
				break;
			}
		}
	}

	EnsureInventoryFocus();
	ApplyDetails();
}

void UPlayerMenuWidget::RefreshMerchantInventoryGrid()
{
	// If we rebuild, any hover pointer is stale.
	HoveredSlotWidget = nullptr;

	// Cache if the locked widget was a merchant slot so we can restore it.
	UItemDataAsset* LockedMerchantItemToRestore = nullptr;
	if (DetailLockedSlotWidget.IsValid())
	{
		const bool bLockedWasMerchant = VisibleMerchantSlots.ContainsByPredicate([this](const TWeakObjectPtr<UInventorySlotWidget>& W)
		{
			return W.Get() == DetailLockedSlotWidget.Get();
		});

		if (bLockedWasMerchant)
		{
			LockedMerchantItemToRestore = DetailLockedSlotWidget->GetItem();
		}
	}

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

		NewMerchantSlotWidget->SetupSlot(Entry.Item, DisplayQty, EItemRarity::Acceptable, nullptr, FLinearColor::White);

		NewMerchantSlotWidget->OnSlotHovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotHovered);
		NewMerchantSlotWidget->OnSlotUnhovered.AddDynamic(this, &UPlayerMenuWidget::HandleSlotUnhovered);
		NewMerchantSlotWidget->OnSlotClicked.AddDynamic(this, &UPlayerMenuWidget::HandleMerchantSlotClicked);

		// Selected visual if in buy cart
		const bool bInBuy = BuyCart.Contains(Entry.Item);
		NewMerchantSlotWidget->SetSelected(bInBuy);

		VisibleMerchantSlots.Add(NewMerchantSlotWidget);
		VisibleIdx++;
	}

	// Restore locked selection to the NEW merchant widget instance if possible
	if (LockedMerchantItemToRestore)
	{
		DetailLockedSlotWidget = nullptr;
		for (const TWeakObjectPtr<UInventorySlotWidget>& W : VisibleMerchantSlots)
		{
			if (!W.IsValid()) continue;
			if (W->GetItem() == LockedMerchantItemToRestore)
			{
				DetailLockedSlotWidget = W.Get();
				break;
			}
		}
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

bool UPlayerMenuWidget::ToggleSellFromPlayerSlot(UInventorySlotWidget* SlotWidget)
{
	if (!ActiveMerchant.IsValid() || !Stats || !SlotWidget || !SlotWidget->GetItem()) return false;

	UItemDataAsset* Item = SlotWidget->GetItem();
	const int32 Qty = SlotWidget->GetQuantity();
	const EItemRarity Rar = SlotWidget->GetRarity();

	FSellKey Key; Key.Item = Item; Key.Rarity = Rar;

	// If already selected => remove
	if (SellCart.Contains(Key))
	{
		SellCart.Remove(Key);
		SlotWidget->SetSelected(false);
		return true;
	}

	// Base rarity value
	int32 Value = GetRaritySellValue(Item, Qty, Rar);

	// Apply preferred multiplier on top (inferred from merchant’s base calc)
	const float PrefMult = InferPreferredMultiplier(ActiveMerchant.Get(), Item, Qty);
	Value = FMath::Max(1, FMath::RoundToInt((float)Value * PrefMult));

	if (Value <= 0) return false;

	// Affordability check against preview (merchant must not go below 0)
	int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
	RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

	const int32 MerchantStart = ActiveMerchant->GetCurrentCurrency();
	const int32 ProspectiveMerchant = MerchantStart - (SellTotal + Value) + BuyTotal;
	if (ProspectiveMerchant < 0)
	{
		return false;
	}

	FSellLine Line;
	Line.Item = Item;
	Line.Quantity = Qty;
	Line.Rarity = Rar;
	Line.Value = Value;

	SellCart.Add(Key, Line);
	SlotWidget->SetSelected(true);
	return true;
}

bool UPlayerMenuWidget::CycleBuyFromMerchantSlot(int32 MerchantSlotIndex)
{
	if (!ActiveMerchant.IsValid() || !Stats) return false;
	if (!CachedMerchantEntries.IsValidIndex(MerchantSlotIndex)) return false;

	const FMerchantInventoryEntry& Entry = CachedMerchantEntries[MerchantSlotIndex];
	if (!Entry.Item) return false;

	const int32 UnitPrice = FMath::Max(0, Entry.BuyPrice);

	const int32 MaxQty = Entry.bInfiniteStock ? 99 : FMath::Max(0, Entry.Stock);
	if (MaxQty <= 0 && !Entry.bInfiniteStock) return false;

	FBuyLine* Existing = BuyCart.Find(Entry.Item);
	const int32 CurQty = Existing ? Existing->Quantity : 0;

	int32 NextQty = CurQty + 1;
	if (!Entry.bInfiniteStock && NextQty > MaxQty)
	{
		// cycle off
		BuyCart.Remove(Entry.Item);
		if (VisibleMerchantSlots.IsValidIndex(MerchantSlotIndex) && VisibleMerchantSlots[MerchantSlotIndex].IsValid())
		{
			VisibleMerchantSlots[MerchantSlotIndex]->SetSelected(false);
		}
		return true;
	}

	// affordability check using preview (player must not go below 0)
	int32 SellTotal = 0, BuyTotal = 0, PlayerPrev = 0, MerchantPrev = 0;
	RecomputeTradePreview(SellTotal, BuyTotal, PlayerPrev, MerchantPrev);

	const int32 PlayerStart = Stats->GetCurrency();
	const int32 ProspectiveBuyTotal = BuyTotal + UnitPrice;
	const int32 ProspectivePlayer = PlayerStart + SellTotal - ProspectiveBuyTotal;
	if (ProspectivePlayer < 0)
	{
		return false;
	}

	FBuyLine NewLine;
	NewLine.Item = Entry.Item;
	NewLine.Quantity = NextQty;
	NewLine.UnitPrice = UnitPrice;

	BuyCart.Add(Entry.Item, NewLine);

	if (VisibleMerchantSlots.IsValidIndex(MerchantSlotIndex) && VisibleMerchantSlots[MerchantSlotIndex].IsValid())
	{
		VisibleMerchantSlots[MerchantSlotIndex]->SetSelected(true);
	}

	return true;
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

		const int32 Cost = Line.UnitPrice * Line.Quantity;
		if (!Stats->SpendCurrency(Cost))
		{
			continue;
		}

		Merchant->ModifyMerchantCurrency(+Cost);
		Merchant->ConsumeMerchantStock(Line.Item, Line.Quantity);

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
