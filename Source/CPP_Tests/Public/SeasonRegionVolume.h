#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SeasonTypes.h"
#include "SeasonRegionVolume.generated.h"

class ASeasonWorldManager;
class UBoxComponent;
class UPrimitiveComponent;
struct FHitResult;

UCLASS()
class CPP_TESTS_API ASeasonRegionVolume : public AActor
{
	GENERATED_BODY()

public:
	ASeasonRegionVolume();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Season|Region")
	bool IsActorInside(const AActor* Actor) const;

	UFUNCTION(BlueprintPure, Category = "Season|Region")
	int32 GetPriority() const { return Priority; }

	UFUNCTION(BlueprintCallable, Category = "Season|Region")
	void BuildOverride(float GlobalTimeOfDayHours, EWorldSeason& OutSeason, float& OutTimeOfDayHours);

	UFUNCTION(BlueprintCallable, Category = "Season|Region|Time")
	void InitializeRuntimeClock(float GlobalTimeOfDayHours);

	UFUNCTION(BlueprintCallable, Category = "Season|Region|Time")
	void TickRuntimeClock(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Season|Region|Time")
	float GetRuntimeTimeOfDayHours() const { return RuntimeTimeOfDayHours; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> Box = nullptr;

protected:
	UPROPERTY(EditAnywhere, Category = "Season|Region")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, Category = "Season|Region")
	ESeasonRegionMode Mode = ESeasonRegionMode::Procedural;

	UPROPERTY(EditAnywhere, Category = "Season|Region")
	EWorldSeason OverrideSeason = EWorldSeason::Winter;

	UPROPERTY(EditAnywhere, Category = "Season|Region",
		meta = (EditCondition = "Mode == ESeasonRegionMode::Locked"))
	ESeasonLockedTimeRule LockedTimeRule = ESeasonLockedTimeRule::FixedTime;

	UPROPERTY(EditAnywhere, Category = "Season|Region",
		meta = (EditCondition = "Mode == ESeasonRegionMode::Locked", ClampMin = "0.0", ClampMax = "24.0", UIMin = "0.0", UIMax = "24.0"))
	float LockedTimeOfDayHours = 12.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Region",
		meta = (EditCondition = "Mode == ESeasonRegionMode::Locked", ClampMin = "-12.0", ClampMax = "12.0", UIMin = "-12.0", UIMax = "12.0"))
	float LockedTimeOffsetHours = 0.0f;

	// How long an in-zone day lasts. 1440 = real-time day.
	UPROPERTY(EditAnywhere, Category = "Season|Region|Time",
		meta = (ClampMin = "0.1", UIMin = "0.1"))
	float DayLengthMinutes = 1440.0f;

private:
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> OverlappingPawns;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	float RuntimeTimeOfDayHours = 12.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	bool bRuntimeClockInitialized = false;

	TWeakObjectPtr<ASeasonWorldManager> CachedManager;

	UFUNCTION()
	void OnBoxBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnBoxEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void ResolveManagerIfNeeded();
	void NotifyManagerOverlapChanged();

	static float WrapTimeOfDayHours(float InHours);
};
