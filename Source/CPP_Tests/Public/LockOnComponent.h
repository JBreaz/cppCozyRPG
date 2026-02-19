#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LockOnComponent.generated.h"

class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLockOnStateChanged, bool, bIsActive, AActor*, NewTarget);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CPP_TESTS_API ULockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULockOnComponent();

	UFUNCTION(BlueprintCallable, Category="LockOn")
	void ToggleLockOn();

	UFUNCTION(BlueprintCallable, Category="LockOn")
	void ClearLockOn();

	UFUNCTION(BlueprintCallable, Category="LockOn")
	bool IsLockOnActive() const { return IsValid(CurrentTarget); }

	UFUNCTION(BlueprintCallable, Category="LockOn")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	UPROPERTY(BlueprintAssignable, Category="LockOn")
	FOnLockOnStateChanged OnLockOnStateChanged;

	// How far we search for lock-on candidates (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LockOn", meta=(ClampMin="0.0", Units="cm"))
	float LockOnSearchRadius = 1200.0f;

	// Require candidates to be somewhat in front of the camera (dot -1..1)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LockOn", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float MinViewDot = 0.15f;

	// Optional LOS check from camera to target point
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LockOn")
	bool bRequireLineOfSight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LockOn")
	TEnumAsByte<ECollisionChannel> LineOfSightTraceChannel = ECC_Visibility;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<AActor> CurrentTarget = nullptr;

	AActor* FindBestTarget() const;

	bool GetViewPoint(FVector& OutLoc, FRotator& OutRot) const;
	bool HasLineOfSightTo(AActor* Candidate, const FVector& ViewLoc) const;
};
