#pragma once

#include "CoreMinimal.h"
#include "SeasonTypes.h"
#include "UObject/Interface.h"
#include "SeasonalVisualInterface.generated.h"

UINTERFACE(BlueprintType)
class CPP_TESTS_API USeasonalVisualInterface : public UInterface
{
	GENERATED_BODY()
};

class CPP_TESTS_API ISeasonalVisualInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Season|Visual")
	void ApplySeasonVisual(EWorldSeason Season);
};
