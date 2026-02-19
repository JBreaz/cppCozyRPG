#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCSafeZone.generated.h"

class USphereComponent;
class ANPCCharacter;

UCLASS()
class CPP_TESTS_API ANPCSafeZone : public AActor
{
	GENERATED_BODY()

public:
	ANPCSafeZone();

	UFUNCTION(BlueprintCallable, Category="SafeZone")
	FVector GetRandomPointInZone() const;

	UFUNCTION(BlueprintCallable, Category="SafeZone")
	bool GetRandomReachablePointInZone(FVector& OutLocation, float RadiusOverride = -1.0f) const;

	UFUNCTION(BlueprintCallable, Category="SafeZone")
	float GetZoneRadius() const { return ZoneRadius; }

	void RegisterNPC(ANPCCharacter* NPC);
	void UnregisterNPC(ANPCCharacter* NPC);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category="SafeZone")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category="SafeZone")
	TObjectPtr<USphereComponent> ZoneSphere;

	UPROPERTY(VisibleInstanceOnly, Category="SafeZone")
	TArray<TObjectPtr<ANPCCharacter>> BoundNPCs;

	UPROPERTY(EditAnywhere, Category="SafeZone", meta=(ClampMin="100.0", Units="cm"))
	float ZoneRadius = 1200.0f;
};
