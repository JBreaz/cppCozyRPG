#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

class UItemDataAsset;

UENUM(BlueprintType)
enum class EItemRarity : uint8
{
	Garbage     UMETA(DisplayName="Garbage"),
	Acceptable  UMETA(DisplayName="Acceptable"),
	Fair        UMETA(DisplayName="Fair"),
	Perfect     UMETA(DisplayName="Perfect"),
};

USTRUCT(BlueprintType)
struct FItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UItemDataAsset> Item = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Quantity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EItemRarity Rarity = EItemRarity::Garbage;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CPP_TESTS_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	UPROPERTY(BlueprintAssignable, Category="Inventory")
	FOnInventoryChanged OnInventoryChanged;

	UFUNCTION(BlueprintCallable, Category="Inventory")
	const TArray<FItemStack>& GetItems() const { return Items; }

	// Core API
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool AddItem(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity = EItemRarity::Garbage);

	// MVP: remove from any rarity stack
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool RemoveItem(UItemDataAsset* Item, int32 Quantity);

	// NEW: remove from a specific rarity stack (needed for merchant + clean UI)
	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool RemoveItemExact(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity);

	UFUNCTION(BlueprintCallable, Category="Inventory")
	void ClearInventory();

	UFUNCTION(BlueprintCallable, Category="Inventory")
	bool HasItem(UItemDataAsset* Item, int32 MinQuantity = 1) const;

private:
	UPROPERTY(EditAnywhere, Category="Inventory")
	TArray<FItemStack> Items;

	int32 FindStackIndex(UItemDataAsset* Item, EItemRarity Rarity) const;
};
