#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryComponent.h" // EItemRarity, FItemStack
#include "MerchantInventoryDataAsset.h" // FMerchantInventoryEntry
#include "PlayerMenuWidget.generated.h"

class UButton;
class UWidgetSwitcher;
class UTextBlock;
class UUniformGridPanel;
class UImage;
class UTexture2D;
class UWidget;
class UProgressBar;
class UHorizontalBox;

class UPlayerStatsComponent;
class UStatusEffectComponent;
class UInventoryComponent;

class UInventorySlotWidget;
class UItemDataAsset;

class ANPCCharacter;

UCLASS()
class CPP_TESTS_API UPlayerMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Menu")
	void InitializeFromComponents(UPlayerStatsComponent* InStats, UStatusEffectComponent* InEffects);

	UFUNCTION(BlueprintCallable, Category="Menu")
	void InitializeInventory(UInventoryComponent* InInventory);

	UFUNCTION(BlueprintCallable, Category="Menu")
	void ShowInventoryTab();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void ShowStatsTab();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void NextTab();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void PrevTab();

	UFUNCTION(BlueprintCallable, Category="Menu")
	void ForceRefresh();

	UFUNCTION(BlueprintCallable, Category="Menu|Focus")
	void EnsureInventoryFocus();

	UFUNCTION(BlueprintCallable, Category="Menu|Merchant")
	void SetActiveMerchant(ANPCCharacter* InMerchant);

	UFUNCTION(BlueprintCallable, Category="Menu|Merchant")
	ANPCCharacter* GetActiveMerchant() const { return ActiveMerchant.Get(); }

	UFUNCTION(BlueprintCallable, Category="Menu|Merchant")
	bool HasActiveMerchant() const { return ActiveMerchant.IsValid(); }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// Tabs
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> InventoryTabButton = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UButton> StatsTabButton = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UWidgetSwitcher> PageSwitcher = nullptr;

	// Stats blocks
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValHealth = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValStamina = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValMagic = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValBaseDamageOutput = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValBaseDamageReduction = nullptr;

	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValStrength = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValEndurance = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValWillpower = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValLuck = nullptr;

	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValPoison = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValFear = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValBurn = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValFrost = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> ValBleed = nullptr;

	// Player inventory grid
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UUniformGridPanel> InventoryGrid = nullptr;

	UPROPERTY(EditAnywhere, Category="Inventory|UI")
	TSubclassOf<UInventorySlotWidget> InventorySlotWidgetClass;

	UPROPERTY(EditAnywhere, Category="Inventory|UI", meta=(ClampMin="1"))
	int32 GridColumns = 5;

	UPROPERTY(EditAnywhere, Category="Inventory|UI", meta=(ClampMin="0.0"))
	float SlotPadding = 0.f;

	// Details panel
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> DetailIconImage = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UImage> DetailRarityImage = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> DetailNameText = nullptr;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> DetailDescriptionText = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> VB_ItemDetails = nullptr;

	// Rarity visuals
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") TObjectPtr<UTexture2D> Rarity_Common = nullptr;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") TObjectPtr<UTexture2D> Rarity_Uncommon = nullptr;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") TObjectPtr<UTexture2D> Rarity_Rare = nullptr;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") TObjectPtr<UTexture2D> Rarity_Epic = nullptr;

	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") FLinearColor RarityTint_Garbage = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") FLinearColor RarityTint_Acceptable = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") FLinearColor RarityTint_Fair = FLinearColor::White;
	UPROPERTY(EditAnywhere, Category="Inventory|UI|Rarity") FLinearColor RarityTint_Perfect = FLinearColor::White;

	// ---- Merchant UI (optional bindings)
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> SB_MerchantWindow = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> NavBar = nullptr;

	// Your UMG shows these names:
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UHorizontalBox> HB_TradeButtons = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> BTN_ConfirmTrade = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> BTN_ClearTrade = nullptr;

	// Backward-compat (if you ever had these names):
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConfirmTradeButton = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ClearTradeButton = nullptr;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> PlayerCurrency = nullptr;     // ALWAYS visible
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MerchantCurrency = nullptr;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MerchantName = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TXT_RelLevelCurrent = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TXT_RelLevelNext = nullptr;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> PB_Relationship = nullptr;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UUniformGridPanel> MerchantInventoryGrid = nullptr;

	// ---- Currency tint config
	UPROPERTY(EditAnywhere, Category="Merchant|UI")
	FLinearColor CurrencyGainTint = FLinearColor(0.2f, 1.0f, 0.2f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Merchant|UI")
	FLinearColor CurrencyLossTint = FLinearColor(1.0f, 0.25f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, Category="Merchant|UI")
	FLinearColor CurrencyNeutralTint = FLinearColor::White;

	// ---- Rarity economy multipliers (sell value baseline)
	UPROPERTY(EditAnywhere, Category="Economy|Rarity")
	float SellMultiplier_Garbage = 0.5f;

	UPROPERTY(EditAnywhere, Category="Economy|Rarity")
	float SellMultiplier_Acceptable = 1.0f;

	UPROPERTY(EditAnywhere, Category="Economy|Rarity")
	float SellMultiplier_Fair = 1.5f;

	UPROPERTY(EditAnywhere, Category="Economy|Rarity")
	float SellMultiplier_Perfect = 2.0f;

private:
	// Components
	UPROPERTY() TObjectPtr<UPlayerStatsComponent> Stats = nullptr;
	UPROPERTY() TObjectPtr<UStatusEffectComponent> Effects = nullptr;
	UPROPERTY() TObjectPtr<UInventoryComponent> Inventory = nullptr;

	UPROPERTY() bool bInventoryDirty = true;

	// Visible slots
	UPROPERTY() TArray<TWeakObjectPtr<UInventorySlotWidget>> VisiblePlayerSlots;
	UPROPERTY() TArray<TWeakObjectPtr<UInventorySlotWidget>> VisibleMerchantSlots;

	// Hover-driven details
	UPROPERTY() TWeakObjectPtr<UInventorySlotWidget> HoveredSlotWidget;

	// Merchant context
	TWeakObjectPtr<ANPCCharacter> ActiveMerchant;

	// Cached merchant entries currently displayed
	UPROPERTY() TArray<FMerchantInventoryEntry> CachedMerchantEntries;

	// Trade carts
	struct FSellKey
	{
		TObjectPtr<UItemDataAsset> Item = nullptr;
		EItemRarity Rarity = EItemRarity::Garbage;

		bool operator==(const FSellKey& Other) const { return Item == Other.Item && Rarity == Other.Rarity; }
	};

	friend uint32 GetTypeHash(const FSellKey& K)
	{
		return HashCombine(::GetTypeHash(K.Item), ::GetTypeHash((uint8)K.Rarity));
	}

	struct FSellLine
	{
		TObjectPtr<UItemDataAsset> Item = nullptr;
		int32 Quantity = 0;
		EItemRarity Rarity = EItemRarity::Garbage;
		int32 Value = 0;
	};

	struct FBuyLine
	{
		TObjectPtr<UItemDataAsset> Item = nullptr;
		int32 Quantity = 0;
		int32 UnitPrice = 0;
	};

	TMap<FSellKey, FSellLine> SellCart;
	TMap<TObjectPtr<UItemDataAsset>, FBuyLine> BuyCart;

	// Events
	UFUNCTION() void HandleStatsChanged();
	UFUNCTION() void HandleEffectsChanged();
	UFUNCTION() void HandleInventoryChanged();

	UFUNCTION() void HandleSlotHovered(UInventorySlotWidget* HoveredWidget);
	UFUNCTION() void HandleSlotUnhovered(UInventorySlotWidget* UnhoveredWidget);
	UFUNCTION() void HandlePlayerSlotClicked(UInventorySlotWidget* ClickedWidget);
	UFUNCTION() void HandlePlayerSlotAddClicked(UInventorySlotWidget* ClickedWidget);
	UFUNCTION() void HandlePlayerSlotSubClicked(UInventorySlotWidget* ClickedWidget);
	UFUNCTION() void HandleMerchantSlotClicked(UInventorySlotWidget* ClickedWidget);
	UFUNCTION() void HandleMerchantSlotAddClicked(UInventorySlotWidget* ClickedWidget);
	UFUNCTION() void HandleMerchantSlotSubClicked(UInventorySlotWidget* ClickedWidget);

	// UI refresh
	void RefreshStatsText();
	void RefreshEffectsText();
	void RefreshPlayerInventoryGrid();
	void RefreshMerchantInventoryGrid();
	void RefreshMerchantPanel();

	// Details panel
	void ClearDetails();
	void SetDetailsVisibility(bool bVisible);
	void ApplyDetails();

	void SetDetailsFromPlayerSlot(UInventorySlotWidget* SlotWidget);
	void SetDetailsFromMerchantSlot(UInventorySlotWidget* SlotWidget, const FMerchantInventoryEntry& Entry);

	// Trade
	void UpdateMerchantModeVisibility();
	void RecomputeTradePreview(int32& OutSellTotal, int32& OutBuyTotal, int32& OutPlayerPreview, int32& OutMerchantPreview) const;
	void UpdateCurrencyUI();

	bool AdjustSellCartQuantity(UInventorySlotWidget* SlotWidget, int32 DeltaQty);
	bool AdjustBuyCartQuantity(UInventorySlotWidget* SlotWidget, int32 DeltaQty);

	int32 GetRaritySellValue(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity) const;
	float GetRarityMultiplier(EItemRarity Rarity) const;

	UFUNCTION() void ClearTrade();
	UFUNCTION() void ConfirmTrade();

	// Helpers
	static void SetValueText(UTextBlock* TB, const FString& ValueOnly);
	void SetTextTint(UTextBlock* TB, const FLinearColor& Tint);

	UTexture2D* GetRarityIcon(EItemRarity InRarity) const;
	FLinearColor GetRarityTint(EItemRarity InRarity) const;

	bool IsInventoryTabActive() const;
	int32 GetFocusedPlayerSlotIndex() const;
	int32 FindPlayerSlotIndex(const UInventorySlotWidget* SlotWidget) const;
	void FocusPlayerSlotIndex(int32 Index);
	bool HandleWrapHorizontal(bool bMoveRight);
};
