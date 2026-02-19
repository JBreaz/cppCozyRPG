#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "PlayerHUD.generated.h"

class UPlayerHUDWidget;

UCLASS()
class CPP_TESTS_API APlayerHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

protected:
	// Set this in a BP child of PlayerHUD, or set it on the CDO if you want code-only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UPlayerHUDWidget> HUDWidgetClass;

private:
	UPROPERTY()
	UPlayerHUDWidget *HUDWidget = nullptr;
};
