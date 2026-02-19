#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CPP_TestsPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UInputAction;
class UPlayerMenuWidget;
class ANPCCharacter;

UCLASS(abstract, config="Game")
class CPP_TESTS_API ACPP_TestsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACPP_TestsPlayerController();

	// NEW: open menu pre-bound to a merchant (used by NPCCharacter interact)
	UFUNCTION(BlueprintCallable, Category="UI|Menu")
	void OpenMenuWithMerchant(ANPCCharacter* Merchant);

protected:
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	UPROPERTY(EditAnywhere, Category="Input|Menu")
	UInputAction* MenuNextTabAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input|Menu")
	UInputAction* MenuPrevTabAction = nullptr;

	UPROPERTY(EditAnywhere, Category="Input|Combat")
	UInputAction* LockOnAction = nullptr;

	FTimerHandle MenuRefreshTimerHandle;

	void MenuNextTab();
	void MenuPrevTab();
	void MenuRefreshTick();

	void HandleLockOnPressed();

	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	UPROPERTY(EditAnywhere, Config, Category="Input|Touch Controls")
	bool bForceTouchControls = false;

	UPROPERTY(EditAnywhere, Category="Input|Menu")
	UInputAction* MenuAction = nullptr;

	UPROPERTY(EditAnywhere, Category="UI|Menu")
	TSubclassOf<UPlayerMenuWidget> PlayerMenuWidgetClass;

	UPROPERTY()
	TObjectPtr<UPlayerMenuWidget> PlayerMenuWidget;

	bool bMenuOpen = false;

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	bool ShouldUseTouchControls() const;

	void ToggleMenu();
	void OpenMenu();
	void CloseMenu();

private:
	bool bLockOnAssumedActive = false;

	bool TryQueryPawnLockOnActive(APawn* InPawn, bool& OutActive) const;
	bool TryCallPawnVoidFunc(APawn* InPawn, const FName& FuncName) const;
	void ForceDisengageLockOn();

	// NEW: if we opened menu via merchant interact
	TWeakObjectPtr<ANPCCharacter> PendingMerchant;
};
