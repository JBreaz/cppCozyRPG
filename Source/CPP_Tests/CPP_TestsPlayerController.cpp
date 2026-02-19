#include "CPP_TestsPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"

#include "CPP_TestsCameraManager.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/Input/SVirtualJoystick.h"

#include "PlayerMenuWidget.h"
#include "CPP_TestsCharacter.h"
#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"
#include "InventoryComponent.h"
#include "NPCCharacter.h"

ACPP_TestsPlayerController::ACPP_TestsPlayerController()
{
	PlayerCameraManagerClass = ACPP_TestsCameraManager::StaticClass();
}

void ACPP_TestsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);
		if (MobileControlsWidget)
		{
			MobileControlsWidget->AddToPlayerScreen(0);
		}
	}
}

void ACPP_TestsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				if (CurrentContext)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}

			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					if (CurrentContext)
					{
						Subsystem->AddMappingContext(CurrentContext, 0);
					}
				}
			}
		}
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MenuAction)
		{
			EIC->BindAction(MenuAction, ETriggerEvent::Started, this, &ACPP_TestsPlayerController::ToggleMenu);
		}

		if (MenuNextTabAction)
		{
			EIC->BindAction(MenuNextTabAction, ETriggerEvent::Started, this, &ACPP_TestsPlayerController::MenuNextTab);
		}
		if (MenuPrevTabAction)
		{
			EIC->BindAction(MenuPrevTabAction, ETriggerEvent::Started, this, &ACPP_TestsPlayerController::MenuPrevTab);
		}

		if (LockOnAction)
		{
			EIC->BindAction(LockOnAction, ETriggerEvent::Started, this, &ACPP_TestsPlayerController::HandleLockOnPressed);
		}
	}
}

bool ACPP_TestsPlayerController::ShouldUseTouchControls() const
{
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void ACPP_TestsPlayerController::ToggleMenu()
{
	if (bMenuOpen)
	{
		CloseMenu();
		return;
	}

	// Opening via menu key should always be "normal" (no merchant)
	PendingMerchant = nullptr;
	OpenMenu();
}

void ACPP_TestsPlayerController::OpenMenuWithMerchant(ANPCCharacter* Merchant)
{
	PendingMerchant = Merchant;

	// If already open, just update merchant context and refresh.
	if (bMenuOpen)
	{
		if (PlayerMenuWidget)
		{
			PlayerMenuWidget->SetActiveMerchant(PendingMerchant.Get());
			PlayerMenuWidget->ShowInventoryTab();
			PlayerMenuWidget->ForceRefresh();
			PlayerMenuWidget->EnsureInventoryFocus();
		}

		PendingMerchant = nullptr;
		return;
	}

	OpenMenu();
}

bool ACPP_TestsPlayerController::TryQueryPawnLockOnActive(APawn* InPawn, bool& OutActive) const
{
	OutActive = false;
	if (!InPawn) return false;

	static const FName Candidates[] =
	{
		FName(TEXT("IsLockOnActive")),
		FName(TEXT("IsLockedOn")),
		FName(TEXT("GetIsLockOnActive"))
	};

	for (const FName& Name : Candidates)
	{
		if (UFunction* Fn = InPawn->FindFunction(Name))
		{
			struct FParams { bool ReturnValue = false; };
			FParams Params;
			InPawn->ProcessEvent(Fn, &Params);
			OutActive = Params.ReturnValue;
			return true;
		}
	}

	return false;
}

bool ACPP_TestsPlayerController::TryCallPawnVoidFunc(APawn* InPawn, const FName& FuncName) const
{
	if (!InPawn) return false;

	if (UFunction* Fn = InPawn->FindFunction(FuncName))
	{
		InPawn->ProcessEvent(Fn, nullptr);
		return true;
	}

	return false;
}

void ACPP_TestsPlayerController::ForceDisengageLockOn()
{
	APawn* P = GetPawn();
	if (!P) return;

	bool bActive = false;
	const bool bHasQuery = TryQueryPawnLockOnActive(P, bActive);

	if (bHasQuery && !bActive)
	{
		bLockOnAssumedActive = false;
		return;
	}

	static const FName ClearCandidates[] =
	{
		FName(TEXT("ClearLockOn")),
		FName(TEXT("StopLockOn")),
		FName(TEXT("EndLockOn")),
		FName(TEXT("CancelLockOn")),
		FName(TEXT("DisableLockOn"))
	};

	for (const FName& Name : ClearCandidates)
	{
		if (TryCallPawnVoidFunc(P, Name))
		{
			bLockOnAssumedActive = false;
			return;
		}
	}

	const bool bShouldToggleOff = (bHasQuery && bActive) || (!bHasQuery && bLockOnAssumedActive);

	if (bShouldToggleOff)
	{
		if (ACPP_TestsCharacter* Char = Cast<ACPP_TestsCharacter>(P))
		{
			Char->ToggleLockOn();
			bLockOnAssumedActive = false;
		}
	}
}

void ACPP_TestsPlayerController::OpenMenu()
{
	if (bMenuOpen) return;
	bMenuOpen = true;

	ForceDisengageLockOn();

	if (!PlayerMenuWidget && PlayerMenuWidgetClass)
	{
		PlayerMenuWidget = CreateWidget<UPlayerMenuWidget>(this, PlayerMenuWidgetClass);
	}

	if (PlayerMenuWidget)
	{
		// IMPORTANT: clear merchant state FIRST to prevent stale trade UI from appearing
		PlayerMenuWidget->SetActiveMerchant(nullptr);

		if (ACPP_TestsCharacter* P = Cast<ACPP_TestsCharacter>(GetPawn()))
		{
			UPlayerStatsComponent* Stats = P->FindComponentByClass<UPlayerStatsComponent>();
			UStatusEffectComponent* Effects = P->FindComponentByClass<UStatusEffectComponent>();
			UInventoryComponent* Inv = P->FindComponentByClass<UInventoryComponent>();

			PlayerMenuWidget->InitializeFromComponents(Stats, Effects);
			PlayerMenuWidget->InitializeInventory(Inv);
		}

		// Apply merchant context (if any) AFTER init
		PlayerMenuWidget->SetActiveMerchant(PendingMerchant.Get());
		PendingMerchant = nullptr;

		if (!PlayerMenuWidget->IsInViewport())
		{
			PlayerMenuWidget->AddToViewport(50);
		}

		PlayerMenuWidget->ForceRefresh();
		PlayerMenuWidget->EnsureInventoryFocus();
	}

	if (APawn* P = GetPawn())
	{
		P->DisableInput(this);
	}

	if (GetWorld())
	{
		GetWorldTimerManager().SetTimer(
			MenuRefreshTimerHandle,
			this,
			&ACPP_TestsPlayerController::MenuRefreshTick,
			0.2f,
			true
		);
	}

	bShowMouseCursor = true;

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	if (PlayerMenuWidget)
	{
		Mode.SetWidgetToFocus(PlayerMenuWidget->TakeWidget());
	}
	SetInputMode(Mode);

	SetIgnoreLookInput(true);
	SetIgnoreMoveInput(true);
}

void ACPP_TestsPlayerController::CloseMenu()
{
	if (!bMenuOpen) return;
	bMenuOpen = false;

	GetWorldTimerManager().ClearTimer(MenuRefreshTimerHandle);

	// Clear pending merchant so it can't leak into next open
	PendingMerchant = nullptr;

	if (PlayerMenuWidget)
	{
		PlayerMenuWidget->SetActiveMerchant(nullptr);
		PlayerMenuWidget->RemoveFromParent();
	}

	bShowMouseCursor = false;

	FInputModeGameOnly Mode;
	SetInputMode(Mode);

	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);

	if (APawn* P = GetPawn())
	{
		P->EnableInput(this);
	}
}

void ACPP_TestsPlayerController::MenuNextTab()
{
	if (!bMenuOpen || !PlayerMenuWidget) return;
	PlayerMenuWidget->NextTab();
	PlayerMenuWidget->ForceRefresh();
}

void ACPP_TestsPlayerController::MenuPrevTab()
{
	if (!bMenuOpen || !PlayerMenuWidget) return;
	PlayerMenuWidget->PrevTab();
	PlayerMenuWidget->ForceRefresh();
}

void ACPP_TestsPlayerController::MenuRefreshTick()
{
	if (bMenuOpen && PlayerMenuWidget)
	{
		PlayerMenuWidget->ForceRefresh();
	}
}

void ACPP_TestsPlayerController::HandleLockOnPressed()
{
	if (bMenuOpen) return;

	if (ACPP_TestsCharacter* P = Cast<ACPP_TestsCharacter>(GetPawn()))
	{
		P->ToggleLockOn();

		bool bActive = false;
		if (TryQueryPawnLockOnActive(P, bActive))
		{
			bLockOnAssumedActive = bActive;
		}
		else
		{
			bLockOnAssumedActive = !bLockOnAssumedActive;
		}
	}
}
