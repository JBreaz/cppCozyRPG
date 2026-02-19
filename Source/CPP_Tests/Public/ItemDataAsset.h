#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ItemDataAsset.generated.h"

class UTexture2D;

UCLASS(BlueprintType)
class CPP_TESTS_API UItemDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// A stable ID you can reference in saves, loot tables, etc.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Item")
	FName ItemId = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Item")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Item")
	TObjectPtr<UTexture2D> Icon = nullptr;

	// If true, we try to merge into existing stacks up to MaxStackSize.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Item")
	bool bStackable = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Item", meta=(ClampMin="1"))
	int32 MaxStackSize = 99;

	// Used by merchants later
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Economy", meta=(ClampMin="0"))
	int32 BaseSellValue = 1;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		// Lets you use Primary Asset workflows later if you want (optional).
		return FPrimaryAssetId(TEXT("Item"), ItemId);
	}
};
