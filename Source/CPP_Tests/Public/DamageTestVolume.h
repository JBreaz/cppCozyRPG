#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StatusEffectComponent.h" // for EStatusEffectType
#include "DamageTestVolume.generated.h"

class UBoxComponent;
class ANPCCharacter;


UCLASS()
class CPP_TESTS_API ADamageTestVolume : public AActor
{
	GENERATED_BODY()

public:
	ADamageTestVolume();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UBoxComponent* Box;

	// What the volume applies while the player is inside it
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard")
	EStatusEffectType EffectType = EStatusEffectType::None;

	// For direct damage + poison DoT tuning
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard", meta=(ClampMin="0.0"))
	float DamagePerSecond = 10.f;

	// For point-based effects while inside the volume
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard", meta=(ClampMin="0.0"))
	float PointsPerSecond = 5.f;

	// Burn is special: it “sticks” until healed. This controls whether burn is applied on entry.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hazard")
	bool bApplyBurnOnEnter = true;

	// Debug
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	bool bPrintDebug = false;

private:
	UPROPERTY()
	TSet<TWeakObjectPtr<class ACPP_TestsCharacter>> OverlappingPlayers;

	UPROPERTY()
	TSet<TWeakObjectPtr<class ANPCCharacter>> OverlappingNPCs;


	UFUNCTION()
	void OnBoxBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnBoxEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void ApplyToPlayer(class ACPP_TestsCharacter* Player, float DeltaSeconds);

	void ApplyToNPC(class ANPCCharacter* NPC, float DeltaSeconds);

};
