#include "CPP_TestsCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

#include "CPP_Tests.h"
#include "Interactable.h"
#include "Engine/Engine.h"

#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"
#include "InventoryComponent.h"
#include "EquipmentComponent.h"

#include "LockOnTargetable.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

ACPP_TestsCharacter::ACPP_TestsCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

    FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
    FirstPersonMesh->SetupAttachment(GetMesh());
    FirstPersonMesh->SetOnlyOwnerSee(true);
    FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
    FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

    FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
    FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
    FirstPersonCameraComponent->SetRelativeLocationAndRotation(
        FVector(-2.8f, 5.89f, 0.0f),
        FRotator(0.0f, 90.0f, -90.0f)
    );
    FirstPersonCameraComponent->bUsePawnControlRotation = true;
    FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
    FirstPersonCameraComponent->bEnableFirstPersonScale = true;
    FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
    FirstPersonCameraComponent->FirstPersonScale = 0.6f;

    GetMesh()->SetOwnerNoSee(true);
    GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

    GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

    GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
    GetCharacterMovement()->AirControl = 0.5f;

    Stats = CreateDefaultSubobject<UPlayerStatsComponent>(TEXT("Stats"));
    StatusEffects = CreateDefaultSubobject<UStatusEffectComponent>(TEXT("StatusEffects"));
    Inventory = CreateDefaultSubobject<UInventoryComponent>(TEXT("Inventory"));
    Equipment = CreateDefaultSubobject<UEquipmentComponent>(TEXT("Equipment"));

    AirLockedSpeed = WalkSpeed;
}

void ACPP_TestsCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (GetCharacterMovement())
    {
        GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
    }

    bWasFalling = false;
    AirLockedSpeed = WalkSpeed;

    LastLockOnSwitchTime = -10000.f;
    bLockOnSwitchStickNeutral = true;

    LockOnOutOfRangeStartTime = -1.f;

    LockOnDisengageHeldSeconds = 0.f;
    LastLockOnLookInputTime = -1.f;
}

void ACPP_TestsCharacter::Landed(const FHitResult& Hit)
{
    Super::Landed(Hit);
    bWasFalling = false;
}

float ACPP_TestsCharacter::GetLowHealthMoveMultiplier() const
{
    if (!Stats)
    {
        return 1.f;
    }

    const float Pct = Stats->GetHealthPercent();
    const float Threshold = FMath::Clamp(LowHealthSpeedThreshold, 0.f, 1.f);

    if (Pct <= Threshold)
    {
        return FMath::Max(0.f, LowHealthMoveSpeedMultiplier);
    }

    return 1.f;
}

float ACPP_TestsCharacter::GetEffectiveLockOnBreakDistance() const
{
    if (LockOnBreakDistance > 0.f)
    {
        return LockOnBreakDistance;
    }
    return FMath::Max(1.f, LockOnSearchRadius);
}

void ACPP_TestsCharacter::ValidateAndMaybeBreakLockOn(float NowSeconds)
{
    if (!bLockedOn)
    {
        return;
    }

    if (!LockOnTarget.IsValid())
    {
        ClearLockOn();
        return;
    }

    AActor* Target = LockOnTarget.Get();
    if (!IsValid(Target))
    {
        ClearLockOn();
        return;
    }

    // In your game: ONLY LockOnTargetable actors should ever be lock-on targets.
    if (!Target->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
    {
        ClearLockOn();
        return;
    }

    // If target says lock-on no longer allowed (death, etc), break immediately
    if (!ILockOnTargetable::Execute_IsLockOnAllowed(Target))
    {
        ClearLockOn();
        return;
    }

    // Break if too far
    const FVector Aim = GetLockOnAimPoint(Target);
    const float Dist2D = FVector::Dist2D(GetActorLocation(), Aim);
    const float BreakDist = GetEffectiveLockOnBreakDistance();

    if (Dist2D > BreakDist)
    {
        if (LockOnOutOfRangeStartTime < 0.f)
        {
            LockOnOutOfRangeStartTime = NowSeconds;
        }

        if ((NowSeconds - LockOnOutOfRangeStartTime) >= FMath::Max(0.f, LockOnBreakDelaySeconds))
        {
            ClearLockOn();
            return;
        }
    }
    else
    {
        LockOnOutOfRangeStartTime = -1.f;
    }
}

void ACPP_TestsCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    UWorld* World = GetWorld();
    const float Now = World ? World->GetTimeSeconds() : 0.f;

    if (bLockedOn)
    {
        ValidateAndMaybeBreakLockOn(Now);

        if (bLockedOn)
        {
            UpdateLockOnFacing(DeltaSeconds);
        }
    }

    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    const bool bIsFalling = MoveComp ? MoveComp->IsFalling() : false;

    const FVector Vel = GetVelocity();
    const bool bIsMoving = Vel.SizeSquared2D() > 5.f;

    if (StatusEffects && Stats)
    {
        StatusEffects->TickEffects(DeltaSeconds, Stats, bIsMoving);
    }

    float MoveMult = StatusEffects ? StatusEffects->GetMoveSpeedMultiplier() : 1.f;
    MoveMult *= GetLowHealthMoveMultiplier();

    if (MoveComp)
    {
        float TargetBaseSpeed = WalkSpeed;
        const bool bCanSprint = (Stats && Stats->Stamina > 0.1f);

        if (bSprintHeld && bCanSprint)
        {
            TargetBaseSpeed = SprintSpeed;
        }

        if (!bWasFalling && bIsFalling)
        {
            AirLockedSpeed = TargetBaseSpeed;
            bWasFalling = true;
        }

        if (bIsFalling)
        {
            MoveComp->MaxWalkSpeed = AirLockedSpeed * MoveMult;
        }
        else
        {
            MoveComp->MaxWalkSpeed = TargetBaseSpeed * MoveMult;
        }
    }

    if (Stats)
    {
        const float MagicMult = StatusEffects ? StatusEffects->GetMagicRegenMultiplier() : 1.f;
        Stats->TickMagic(DeltaSeconds, MagicMult);

        if (!bIsFalling)
        {
            const float RegenMult = StatusEffects ? StatusEffects->GetStaminaRegenMultiplier() : 1.f;
            Stats->TickStamina(DeltaSeconds, bSprintHeld, bIsMoving, RegenMult);
        }
    }
}

void ACPP_TestsCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (JumpAction)
        {
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACPP_TestsCharacter::DoJumpStart);
            EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACPP_TestsCharacter::DoJumpEnd);
        }

        if (MoveAction)
        {
            EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACPP_TestsCharacter::MoveInput);
        }

        if (LookAction)
        {
            EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACPP_TestsCharacter::LookGamepadInput);
        }
        if (MouseLookAction)
        {
            EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ACPP_TestsCharacter::LookMouseInput);
        }

        if (SprintAction)
        {
            EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &ACPP_TestsCharacter::SprintStart);
            EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &ACPP_TestsCharacter::SprintEnd);
        }
        else
        {
            UE_LOG(LogTemplateCharacter, Warning, TEXT("SprintAction is NULL. Assign IA_Sprint in BP_FirstPersonCharacter Class Defaults."));
        }

        if (InteractAction)
        {
            EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ACPP_TestsCharacter::DoInteract);
        }
        else
        {
            UE_LOG(LogTemplateCharacter, Error, TEXT("InteractAction is NULL. Assign IA_Interact in your Character BP Class Defaults."));
        }
    }
    else
    {
        UE_LOG(LogCPP_Tests, Error, TEXT("'%s' Failed to find an Enhanced Input Component!"), *GetNameSafe(this));
    }
}

void ACPP_TestsCharacter::SprintStart()
{
    bSprintHeld = true;
}

void ACPP_TestsCharacter::SprintEnd()
{
    bSprintHeld = false;
}

void ACPP_TestsCharacter::MoveInput(const FInputActionValue& Value)
{
    const FVector2D MovementVector = Value.Get<FVector2D>();
    DoMove(MovementVector.X, MovementVector.Y);
}

void ACPP_TestsCharacter::HandleLockOnLookInput(const FVector2D& RawAxis, bool bIsMouse)
{
    if (!bLockedOn)
    {
        return;
    }

    UWorld* World = GetWorld();
    const float Now = World ? World->GetTimeSeconds() : 0.f;

    float Dt = 0.f;
    if (LastLockOnLookInputTime >= 0.f)
    {
        Dt = Now - LastLockOnLookInputTime;
        Dt = FMath::Clamp(Dt, 0.f, 0.10f);
    }
    LastLockOnLookInputTime = Now;

    FVector2D Stick = RawAxis;

    if (bIsMouse)
    {
        const float Scale = FMath::Max(0.01f, LockOnMouseDeltaToStickScale);
        Stick.X = FMath::Clamp(Stick.X / Scale, -1.f, 1.f);
        Stick.Y = FMath::Clamp(Stick.Y / Scale, -1.f, 1.f);
    }

    if (bInvertLockOnSwitchY)
    {
        Stick.Y *= -1.f;
    }

    const float Mag = Stick.Size();

    if (Mag <= LockOnSwitchStickResetThreshold)
    {
        bLockOnSwitchStickNeutral = true;
    }

    const float DisengageThreshold = bIsMouse ? LockOnMouseDisengageInputThreshold : LockOnDisengageInputThreshold;

    if (Mag >= DisengageThreshold)
    {
        LockOnDisengageHeldSeconds += Dt;

        if (LockOnDisengageHeldSeconds >= FMath::Max(0.f, LockOnDisengageHoldSeconds))
        {
            ClearLockOn();
            return;
        }
    }
    else
    {
        LockOnDisengageHeldSeconds = 0.f;
    }

    if (Mag <= LockOnSwitchStickResetThreshold)
    {
        return;
    }

    if (bLockOnSwitchStickNeutral && Mag >= LockOnSwitchStickThreshold)
    {
        if ((Now - LastLockOnSwitchTime) >= LockOnSwitchCooldownSeconds)
        {
            if (AActor* NewTarget = FindLockOnTargetInDirection(Stick))
            {
                LockOnTarget = NewTarget;
                SnapControlRotationToTarget(NewTarget);
            }

            LastLockOnSwitchTime = Now;
        }

        bLockOnSwitchStickNeutral = false;
    }
}

void ACPP_TestsCharacter::LookGamepadInput(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();
    LastLookStick = LookAxisVector;

    if (bLockedOn)
    {
        HandleLockOnLookInput(LookAxisVector, false);
        return;
    }

    DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void ACPP_TestsCharacter::LookMouseInput(const FInputActionValue& Value)
{
    const FVector2D LookAxisVector = Value.Get<FVector2D>();

    if (bLockedOn)
    {
        HandleLockOnLookInput(LookAxisVector, true);
        return;
    }

    DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void ACPP_TestsCharacter::DoAim(float Yaw, float Pitch)
{
    if (!GetController())
    {
        return;
    }

    if (bLockedOn)
    {
        return;
    }

    AddControllerYawInput(Yaw);
    AddControllerPitchInput(Pitch);
}

void ACPP_TestsCharacter::DoMove(float Right, float Forward)
{
    if (!GetController())
    {
        return;
    }

    if (!bLockedOn || !LockOnTarget.IsValid())
    {
        AddMovementInput(GetActorRightVector(), Right);
        AddMovementInput(GetActorForwardVector(), Forward);
        return;
    }

    const FVector ToTarget = (LockOnTarget->GetActorLocation() - GetActorLocation());
    FVector ForwardDir = FVector(ToTarget.X, ToTarget.Y, 0.f);
    ForwardDir.Normalize();

    FVector RightDir = FVector::CrossProduct(FVector::UpVector, ForwardDir);
    RightDir.Normalize();

    AddMovementInput(RightDir, Right);
    AddMovementInput(ForwardDir, Forward);
}

void ACPP_TestsCharacter::DoJumpStart()
{
    UCharacterMovementComponent* MoveComp = GetCharacterMovement();
    const bool bIsFalling = MoveComp ? MoveComp->IsFalling() : true;

    if (bIsFalling) return;
    if (Stats && Stats->Stamina < JumpStaminaCost) return;

    if (Stats)
    {
        Stats->ModifyStamina(-JumpStaminaCost);
    }

    float Base = WalkSpeed;
    const bool bCanSprint = (Stats && Stats->Stamina > 0.1f);
    if (bSprintHeld && bCanSprint)
    {
        Base = SprintSpeed;
    }
    AirLockedSpeed = Base;

    Jump();
}

void ACPP_TestsCharacter::DoJumpEnd()
{
    StopJumping();
}

void ACPP_TestsCharacter::DoInteract()
{
    if (!FirstPersonCameraComponent)
    {
        UE_LOG(LogTemplateCharacter, Error, TEXT("FirstPersonCameraComponent is null."));
        return;
    }

    const FVector Start = FirstPersonCameraComponent->GetComponentLocation();
    const FVector End = Start + (FirstPersonCameraComponent->GetForwardVector() * InteractTraceDistance);

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractTrace), true, this);

    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    if (!bHit)
    {
        return;
    }

    AActor* HitActor = Hit.GetActor();
    if (!HitActor) return;

    if (HitActor->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
    {
        IInteractable::Execute_Interact(HitActor, this);
    }
}

// ======================
// Lock-on
// ======================

void ACPP_TestsCharacter::ToggleLockOn()
{
    if (bLockedOn)
    {
        ClearLockOn();
        return;
    }

    AActor* Best = FindBestLockOnTarget();
    if (Best)
    {
        bLockedOn = true;
        LockOnTarget = Best;

        LastLockOnSwitchTime = -10000.f;
        bLockOnSwitchStickNeutral = true;

        LockOnOutOfRangeStartTime = -1.f;

        LockOnDisengageHeldSeconds = 0.f;
        LastLockOnLookInputTime = -1.f;

        SnapControlRotationToTarget(Best);
    }
}

void ACPP_TestsCharacter::ClearLockOn()
{
    bLockedOn = false;
    LockOnTarget = nullptr;

    bLockOnSwitchStickNeutral = true;
    LastLockOnSwitchTime = -10000.f;

    LockOnOutOfRangeStartTime = -1.f;

    LockOnDisengageHeldSeconds = 0.f;
    LastLockOnLookInputTime = -1.f;
}

FVector ACPP_TestsCharacter::GetLockOnAimPoint(AActor* Target) const
{
    if (!IsValid(Target))
    {
        return FVector::ZeroVector;
    }

    if (Target->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
    {
        return ILockOnTargetable::Execute_GetLockOnWorldLocation(Target);
    }

    return Target->GetActorLocation();
}

AActor* ACPP_TestsCharacter::FindBestLockOnTarget() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    const FVector Origin = GetActorLocation();

    TArray<AActor*> Overlaps;
    TArray<TEnumAsByte<EObjectTypeQuery>> ObjTypes;
    ObjTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

    TArray<AActor*> Ignore;
    Ignore.Add(const_cast<ACPP_TestsCharacter*>(this));

    // IMPORTANT: do NOT filter by ANPCCharacter anymore.
    const bool bAny = UKismetSystemLibrary::SphereOverlapActors(
        World,
        Origin,
        LockOnSearchRadius,
        ObjTypes,
        APawn::StaticClass(),
        Ignore,
        Overlaps
    );

    if (!bAny) return nullptr;

    FVector ViewForward = GetActorForwardVector();
    FVector ViewOrigin = Origin;

    if (FirstPersonCameraComponent)
    {
        ViewForward = FirstPersonCameraComponent->GetForwardVector();
        ViewOrigin = FirstPersonCameraComponent->GetComponentLocation();
    }
    else if (const AController* C = GetController())
    {
        ViewForward = C->GetControlRotation().Vector();
    }

    AActor* Best = nullptr;
    float BestScore = -FLT_MAX;

    for (AActor* A : Overlaps)
    {
        if (!IsValid(A) || A == this) continue;

        // THE IMPORTANT PART: only LockOnTargetable
        if (!A->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
        {
            continue;
        }

        if (!ILockOnTargetable::Execute_IsLockOnAllowed(A))
        {
            continue;
        }

        const FVector Aim = GetLockOnAimPoint(A);
        FVector To = Aim - ViewOrigin;
        To.Z = 0.f;

        const float DistSq = To.SizeSquared();
        if (DistSq <= KINDA_SMALL_NUMBER) continue;

        const FVector Dir = To.GetSafeNormal();

        const float Dot = FVector::DotProduct(ViewForward.GetSafeNormal2D(), Dir);
        if (Dot < LockOnFrontDotMin) continue;

        const float Dist = FMath::Sqrt(DistSq);
        const float DistNorm = FMath::Clamp(Dist / FMath::Max(1.f, LockOnSearchRadius), 0.f, 1.f);

        const float Score = (Dot * 2.0f) - (DistNorm * 1.0f);
        if (Score > BestScore)
        {
            BestScore = Score;
            Best = A;
        }
    }

    return Best;
}

AActor* ACPP_TestsCharacter::FindLockOnTargetInDirection(const FVector2D& StickAxis) const
{
    if (!bLockedOn || !LockOnTarget.IsValid())
    {
        return nullptr;
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC)
    {
        return nullptr;
    }

    const FVector2D StickDir = StickAxis.GetSafeNormal();
    if (StickDir.IsNearlyZero())
    {
        return nullptr;
    }

    int32 SizeX = 0, SizeY = 0;
    PC->GetViewportSize(SizeX, SizeY);
    if (SizeX <= 0 || SizeY <= 0)
    {
        return nullptr;
    }

    const FVector2D ScreenCenter((float)SizeX * 0.5f, (float)SizeY * 0.5f);
    const float MaxCenterDist = FVector2D((float)SizeX * 0.5f, (float)SizeY * 0.5f).Size();

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    const FVector Origin = GetActorLocation();

    TArray<AActor*> Overlaps;
    TArray<TEnumAsByte<EObjectTypeQuery>> ObjTypes;
    ObjTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

    TArray<AActor*> Ignore;
    Ignore.Add(const_cast<ACPP_TestsCharacter*>(this));

    // IMPORTANT: do NOT filter by ANPCCharacter anymore.
    const bool bAny = UKismetSystemLibrary::SphereOverlapActors(
        World,
        Origin,
        LockOnSearchRadius,
        ObjTypes,
        APawn::StaticClass(),
        Ignore,
        Overlaps
    );

    if (!bAny)
    {
        return nullptr;
    }

    AActor* Best = nullptr;
    float BestScore = -FLT_MAX;

    const FVector ViewOrigin = FirstPersonCameraComponent ? FirstPersonCameraComponent->GetComponentLocation() : GetActorLocation();

    for (AActor* A : Overlaps)
    {
        if (!IsValid(A) || A == this) continue;
        if (A == LockOnTarget.Get()) continue;

        // THE IMPORTANT PART: only LockOnTargetable
        if (!A->GetClass()->ImplementsInterface(ULockOnTargetable::StaticClass()))
        {
            continue;
        }

        if (!ILockOnTargetable::Execute_IsLockOnAllowed(A))
        {
            continue;
        }

        const FVector Aim = GetLockOnAimPoint(A);

        FVector2D ScreenPos;
        if (!PC->ProjectWorldLocationToScreen(Aim, ScreenPos, true))
        {
            continue;
        }

        FVector2D Delta = ScreenPos - ScreenCenter;
        Delta.Y *= -1.f;

        const float DeltaMag = Delta.Size();
        if (DeltaMag <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const FVector2D DeltaDir = Delta / DeltaMag;
        const float DirDot = FVector2D::DotProduct(DeltaDir, StickDir);
        if (DirDot < LockOnSwitchDirectionDotMin)
        {
            continue;
        }

        const float CenterNorm = (MaxCenterDist > 0.f) ? FMath::Clamp(DeltaMag / MaxCenterDist, 0.f, 1.f) : 1.f;

        const float WorldDist = FVector::Dist2D(ViewOrigin, Aim);
        const float WorldNorm = FMath::Clamp(WorldDist / FMath::Max(1.f, LockOnSearchRadius), 0.f, 1.f);

        const float Score =
            (DirDot * 2.25f) -
            (CenterNorm * 0.35f) -
            (WorldNorm * 0.25f);

        if (Score > BestScore)
        {
            BestScore = Score;
            Best = A;
        }
    }

    return Best;
}

void ACPP_TestsCharacter::SnapControlRotationToTarget(AActor* Target)
{
    if (!IsValid(Target) || !GetController()) return;

    const FVector CamLoc = FirstPersonCameraComponent ? FirstPersonCameraComponent->GetComponentLocation() : GetActorLocation();
    const FVector Aim = GetLockOnAimPoint(Target);

    FRotator Desired = (Aim - CamLoc).Rotation();
    Desired.Roll = 0.f;

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->PlayerCameraManager)
        {
            Desired.Pitch = FMath::Clamp(Desired.Pitch, PC->PlayerCameraManager->ViewPitchMin, PC->PlayerCameraManager->ViewPitchMax);
        }
    }

    GetController()->SetControlRotation(Desired);
}

void ACPP_TestsCharacter::UpdateLockOnFacing(float DeltaSeconds)
{
    if (!LockOnTarget.IsValid()) return;
    if (!GetController()) return;

    const FVector CamLoc = FirstPersonCameraComponent ? FirstPersonCameraComponent->GetComponentLocation() : GetActorLocation();
    const FVector Aim = GetLockOnAimPoint(LockOnTarget.Get());

    FRotator Desired = (Aim - CamLoc).Rotation();
    Desired.Roll = 0.f;

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->PlayerCameraManager)
        {
            Desired.Pitch = FMath::Clamp(Desired.Pitch, PC->PlayerCameraManager->ViewPitchMin, PC->PlayerCameraManager->ViewPitchMax);
        }
    }

    const FRotator Current = GetController()->GetControlRotation();
    const FRotator NewRot = FMath::RInterpTo(Current, Desired, DeltaSeconds, FMath::Max(0.0f, LockOnRotationInterpSpeed));

    FRotator Final = NewRot;
    Final.Roll = 0.f;
    GetController()->SetControlRotation(Final);
}
