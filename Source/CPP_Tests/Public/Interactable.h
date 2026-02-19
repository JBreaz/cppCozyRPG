#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

UINTERFACE(BlueprintType)
class CPP_TESTS_API UInteractable : public UInterface
{
    GENERATED_BODY()
};

class CPP_TESTS_API IInteractable
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interact")
    void Interact(AActor* Interactor);
};
