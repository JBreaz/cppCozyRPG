#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "LockOnTargetable.generated.h"

UINTERFACE(BlueprintType)
class CPP_TESTS_API ULockOnTargetable : public UInterface
{
	GENERATED_BODY()
};

class CPP_TESTS_API ILockOnTargetable
{
	GENERATED_BODY()

public:
	// Where the camera should aim when locked-on (chest/head/etc).
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat|LockOn")
	FVector GetLockOnWorldLocation() const;

	// Return false if target should not be lock-on-able (dead, hidden, etc).
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat|LockOn")
	bool IsLockOnAllowed() const;
};
