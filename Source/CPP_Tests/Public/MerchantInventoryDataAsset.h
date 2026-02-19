#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MerchantInventoryDataAsset.generated.h"

class UItemDataAsset;

USTRUCT(BlueprintType)
struct FMerchantInventoryEntry
{
	GENERATED_BODY()

	// Item offered for sale by this merchant
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant")
	TObjectPtr<UItemDataAsset> Item = nullptr;

	// Stock available (ignored if bInfiniteStock)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant", meta=(ClampMin="0"))
	int32 Stock = 0;

	// Cost to the player to buy ONE unit of this item
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant", meta=(ClampMin="0"))
	int32 BuyPrice = 0;

	// Relationship required (0-5). Note: relationship 0 refuses service anyway.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant", meta=(ClampMin="0", ClampMax="5"))
	int32 MinRelationship = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant")
	bool bInfiniteStock = false;
};

UCLASS(BlueprintType)
class CPP_TESTS_API UMerchantInventoryDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Merchant")
	TArray<FMerchantInventoryEntry> Entries;
};
