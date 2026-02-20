#include "InventoryComponent.h"
#include "ItemDataAsset.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UInventoryComponent::FindStackIndex(UItemDataAsset* Item, EItemRarity Rarity) const
{
	if (!Item) return INDEX_NONE;

	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i].Item == Item && Items[i].Rarity == Rarity)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool UInventoryComponent::AddItem(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity)
{
	if (!Item || Quantity <= 0) return false;

	const int32 Idx = FindStackIndex(Item, Rarity);
	if (Idx != INDEX_NONE)
	{
		Items[Idx].Quantity += Quantity;
	}
	else
	{
		FItemStack NewStack;
		NewStack.Item = Item;
		NewStack.Quantity = Quantity;
		NewStack.Rarity = Rarity;
		Items.Add(NewStack);
	}

	OnInventoryChanged.Broadcast();
	return true;
}

bool UInventoryComponent::RemoveItemExact(UItemDataAsset* Item, int32 Quantity, EItemRarity Rarity)
{
	if (!Item || Quantity <= 0) return false;

	const int32 Idx = FindStackIndex(Item, Rarity);
	if (Idx == INDEX_NONE) return false;
	if (Items[Idx].Quantity < Quantity) return false;

	Items[Idx].Quantity -= Quantity;
	if (Items[Idx].Quantity <= 0)
	{
		Items.RemoveAt(Idx);
	}

	OnInventoryChanged.Broadcast();
	return true;
}

bool UInventoryComponent::RemoveItem(UItemDataAsset* Item, int32 Quantity)
{
	if (!Item || Quantity <= 0) return false;

	// Remove from the first matching stack that can satisfy the quantity.
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i].Item == Item && Items[i].Quantity >= Quantity)
		{
			const EItemRarity FoundRarity = Items[i].Rarity;
			return RemoveItemExact(Item, Quantity, FoundRarity);
		}
	}

	return false;
}

void UInventoryComponent::ClearInventory()
{
	if (Items.Num() == 0) return;
	Items.Reset();
	OnInventoryChanged.Broadcast();
}

bool UInventoryComponent::HasItem(UItemDataAsset* Item, int32 MinQuantity) const
{
	if (!Item) return false;
	if (MinQuantity <= 0) return true;

	int32 Total = 0;
	for (const FItemStack& S : Items)
	{
		if (S.Item == Item)
		{
			Total += S.Quantity;
			if (Total >= MinQuantity)
			{
				return true;
			}
		}
	}
	return false;
}
