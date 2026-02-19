#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CPP_TestProbe.generated.h"

UCLASS()
class CPP_TESTS_API ACPP_TestProbe : public AActor
{
	GENERATED_BODY()

public:
	ACPP_TestProbe();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	// Root mesh that we rotate.
	UPROPERTY(VisibleAnywhere, Category="TestProbe")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	// Degrees per second (Yaw / Z axis).
	UPROPERTY(EditAnywhere, Category="TestProbe")
	float RotationSpeedDegPerSec = 90.0f;

	// Timer that prints "Working" every second.
	FTimerHandle WorkingTimerHandle;

	// Called by the timer.
	void PrintWorking();
};
