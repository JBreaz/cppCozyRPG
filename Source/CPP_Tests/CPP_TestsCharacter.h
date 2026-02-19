#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InventoryComponent.h"
#include "Logging/LogMacros.h"
#include "CPP_TestsCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

class UPlayerStatsComponent;
class UStatusEffectComponent;
class UInventoryComponent;
class UEquipmentComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(abstract)
class ACPP_TestsCharacter : public ACharacter
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UCameraComponent> FirstPersonCameraComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UPlayerStatsComponent> Stats;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStatusEffectComponent> StatusEffects;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UInventoryComponent> Inventory;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UEquipmentComponent> Equipment;

protected:
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> JumpAction;
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> LookAction;      // gamepad/right stick
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> MouseLookAction; // mouse delta
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> InteractAction;
    UPROPERTY(EditAnywhere, Category="Input") TObjectPtr<UInputAction> SprintAction;

    UPROPERTY(EditAnywhere, Category="Interact", meta=(ClampMin=0, ClampMax=5000, Units="cm"))
    float InteractTraceDistance = 600.0f;

    UPROPERTY(EditAnywhere, Category="Movement")
    float WalkSpeed = 600.f;

    UPROPERTY(EditAnywhere, Category="Movement")
    float SprintSpeed = 900.f;

    // Low health movement penalty (player)
    UPROPERTY(EditAnywhere, Category="Movement|LowHealth", meta=(ClampMin="0.0", ClampMax="1.0"))
    float LowHealthSpeedThreshold = 0.25f;

    UPROPERTY(EditAnywhere, Category="Movement|LowHealth", meta=(ClampMin="0.0"))
    float LowHealthMoveSpeedMultiplier = 0.5f;

    UPROPERTY(EditAnywhere, Category="Movement|Stamina")
    float JumpStaminaCost = 15.f;

    // Lock-on settings
    UPROPERTY(EditAnywhere, Category="Combat|LockOn", meta=(ClampMin="0.0"))
    float LockOnSearchRadius = 2000.f;

    UPROPERTY(EditAnywhere, Category="Combat|LockOn", meta=(ClampMin="-1.0", ClampMax="1.0"))
    float LockOnFrontDotMin = 0.25f;

    // How quickly the camera rotates toward target while locked-on
    UPROPERTY(EditAnywhere, Category="Combat|LockOn", meta=(ClampMin="0.0"))
    float LockOnRotationInterpSpeed = 18.f;

    // Break lock if target is beyond this distance. 0 = use LockOnSearchRadius
    UPROPERTY(EditAnywhere, Category="Combat|LockOn", meta=(ClampMin="0.0", Units="cm"))
    float LockOnBreakDistance = 0.f;

    // Small grace period before breaking (prevents jittery unlocks)
    UPROPERTY(EditAnywhere, Category="Combat|LockOn", meta=(ClampMin="0.0", Units="s"))
    float LockOnBreakDelaySeconds = 0.15f;

    // --- Lock-on switching (Souls-style) ---
    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Switching", meta=(ClampMin="0.0", ClampMax="1.0"))
    float LockOnSwitchStickThreshold = 0.60f;

    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Switching", meta=(ClampMin="0.0", ClampMax="1.0"))
    float LockOnSwitchStickResetThreshold = 0.25f;

    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Switching", meta=(ClampMin="0.0", Units="s"))
    float LockOnSwitchCooldownSeconds = 0.22f;

    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Switching", meta=(ClampMin="-1.0", ClampMax="1.0"))
    float LockOnSwitchDirectionDotMin = 0.35f;

    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Switching")
    bool bInvertLockOnSwitchY = false;

    // Mouse -> “virtual stick” scaling for switching while locked-on
    UPROPERTY(EditAnywhere, Category="Combat|LockOn|MouseSwitching", meta=(ClampMin="0.01"))
    float LockOnMouseDeltaToStickScale = 8.0f;

    // Disengage settings
    // Gamepad threshold: must push stick hard (near edge)
    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Disengage", meta=(ClampMin="0.0", ClampMax="1.0"))
    float LockOnDisengageInputThreshold = 0.90f;

    // Mouse threshold: lower, because mouse is delta-based (not a held axis)
    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Disengage", meta=(ClampMin="0.0", ClampMax="1.0"))
    float LockOnMouseDisengageInputThreshold = 0.35f;

    // How long you must sustain the disengage intent.
    UPROPERTY(EditAnywhere, Category="Combat|LockOn|Disengage", meta=(ClampMin="0.0", Units="s"))
    float LockOnDisengageHoldSeconds = 0.60f;

public:
    ACPP_TestsCharacter();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Landed(const FHitResult& Hit) override;

    void MoveInput(const FInputActionValue& Value);

    void LookGamepadInput(const FInputActionValue& Value);
    void LookMouseInput(const FInputActionValue& Value);

    void SprintStart();
    void SprintEnd();

    bool bSprintHeld = false;
    bool bWasFalling = false;

    float AirLockedSpeed = 600.f;

    UFUNCTION(BlueprintCallable, Category="Input")
    virtual void DoAim(float Yaw, float Pitch);

    UFUNCTION(BlueprintCallable, Category="Input")
    virtual void DoMove(float Right, float Forward);

    UFUNCTION(BlueprintCallable, Category="Input")
    virtual void DoJumpStart();

    UFUNCTION(BlueprintCallable, Category="Input")
    virtual void DoJumpEnd();

    UFUNCTION(BlueprintCallable, Category="Input")
    virtual void DoInteract();

    virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:
    UFUNCTION(BlueprintCallable, Category="Combat|LockOn")
    void ToggleLockOn();

    UFUNCTION(BlueprintCallable, Category="Combat|LockOn")
    void ClearLockOn();

    UFUNCTION(BlueprintCallable, Category="Combat|LockOn")
    bool IsLockedOn() const { return bLockedOn; }

    UFUNCTION(BlueprintCallable, Category="Combat|LockOn")
    AActor* GetLockOnTarget() const { return LockOnTarget.Get(); }

    USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
    UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

    UInventoryComponent* GetInventory() const { return Inventory; }
    UEquipmentComponent* GetEquipment() const { return Equipment; }

private:
    bool bLockedOn = false;
    TWeakObjectPtr<AActor> LockOnTarget;

    FVector2D LastLookStick = FVector2D::ZeroVector;

    // Switching runtime
    float LastLockOnSwitchTime = -10000.f;
    bool bLockOnSwitchStickNeutral = true;

    // Break runtime
    float LockOnOutOfRangeStartTime = -1.f;

    // Disengage runtime (accumulate only while input events are happening)
    float LockOnDisengageHeldSeconds = 0.f;
    float LastLockOnLookInputTime = -1.f;

    AActor* FindBestLockOnTarget() const;
    AActor* FindLockOnTargetInDirection(const FVector2D& StickAxis) const;

    FVector GetLockOnAimPoint(AActor* Target) const;

    void UpdateLockOnFacing(float DeltaSeconds);
    void SnapControlRotationToTarget(AActor* Target);

    float GetLowHealthMoveMultiplier() const;

    float GetEffectiveLockOnBreakDistance() const;
    void ValidateAndMaybeBreakLockOn(float NowSeconds);

    void HandleLockOnLookInput(const FVector2D& RawAxis, bool bIsMouse);
};
