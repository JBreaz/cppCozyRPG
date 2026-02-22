#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "SeasonTypes.h"
#include "SeasonWorldManager.generated.h"

class APlayerController;
class APawn;
class ADirectionalLight;
class ASeasonRegionVolume;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialParameterCollection;

struct FFoliageSeasonInstanceKey
{
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;
	int32 InstanceIndex = INDEX_NONE;

	bool operator==(const FFoliageSeasonInstanceKey& Other) const
	{
		return Component == Other.Component && InstanceIndex == Other.InstanceIndex;
	}
};

FORCEINLINE uint32 GetTypeHash(const FFoliageSeasonInstanceKey& Key)
{
	return HashCombine(GetTypeHash(Key.Component), GetTypeHash(Key.InstanceIndex));
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveSeasonChangedSignature, EWorldSeason, NewSeason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveTimeOfDayChangedSignature, float, NewTimeOfDayHours);

UCLASS()
class CPP_TESTS_API ASeasonWorldManager : public AActor
{
	GENERATED_BODY()

public:
	ASeasonWorldManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Season")
	EWorldSeason GetGlobalSeason() const { return GlobalSeason; }

	UFUNCTION(BlueprintPure, Category = "Season")
	float GetGlobalTimeOfDayHours() const { return GlobalTimeOfDayHours; }

	UFUNCTION(BlueprintPure, Category = "Season")
	EWorldSeason GetActiveSeason() const { return ActiveSeason; }

	UFUNCTION(BlueprintPure, Category = "Season")
	float GetActiveTimeOfDayHours() const { return ActiveTimeOfDayHours; }

	UFUNCTION(BlueprintPure, Category = "Season")
	ASeasonRegionVolume* GetActiveVolume() const { return ActiveVolume.Get(); }

	UFUNCTION(BlueprintCallable, Category = "Season")
	void RegisterRegionVolume(ASeasonRegionVolume* Volume);

	UFUNCTION(BlueprintCallable, Category = "Season")
	void UnregisterRegionVolume(ASeasonRegionVolume* Volume);

	UFUNCTION(BlueprintCallable, Category = "Season")
	void NotifyRegionOverlapChanged(ASeasonRegionVolume* SourceVolume);

	UFUNCTION(BlueprintCallable, Category = "Season")
	void ForceRecompute();

	UFUNCTION(BlueprintCallable, Category = "Season|Visual")
	void RefreshSeasonalActors();

	UPROPERTY(BlueprintAssignable, Category = "Season")
	FOnActiveSeasonChangedSignature OnActiveSeasonChanged;

	UPROPERTY(BlueprintAssignable, Category = "Season")
	FOnActiveTimeOfDayChangedSignature OnActiveTimeOfDayChanged;

protected:
	UPROPERTY(EditAnywhere, Category = "Season|Clock", meta = (ClampMin = "0.05", ToolTip = "How often (seconds) to poll device date/time and recompute global season/time."))
	float DevicePollIntervalSeconds = 1.0f;

	UPROPERTY(EditInstanceOnly, Category = "Season|Lighting", meta = (ToolTip = "Directional Light actor used as the sun for time-of-day pitch updates."))
	TObjectPtr<ADirectionalLight> SunDirectionalLight = nullptr;

	UPROPERTY(EditAnywhere, Category = "Season|Lighting", meta = (ClampMin = "0.0", ToolTip = "Blend duration (seconds) when sun pitch changes between times/zones."))
	float SunPitchLerpSeconds = 2.0f;

	// Design rule: hour 12.0 (noon) maps to this pitch.
	UPROPERTY(EditAnywhere, Category = "Season|Lighting", meta = (ToolTip = "Sun pitch at 12:00 (noon). Other hours are offset from this by 15 degrees per hour."))
	float SunPitchAtNoon = -90.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Debug", meta = (ToolTip = "Log warnings when MPC assignment/parameter writes fail."))
	bool bLogMPCWriteFailures = true;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ClampMin = "-1.0", ClampMax = "1.0", ToolTip = "Dot-product threshold for considering something in front of the camera (higher = stricter)."))
	float ObservedFrontDotThreshold = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ClampMin = "0.0", Units = "cm", ToolTip = "Distance where near foliage gets extra observation tolerance to reduce visible popping."))
	float FoliageAlwaysObservedDistanceCm = 250.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ClampMin = "0.0", ToolTip = "Extra screen-edge margin (pixels) where foliage is still considered observed."))
	float FoliageScreenEdgePaddingPixels = 24.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ClampMin = "0.0", ToolTip = "Minimum projected foliage radius (pixels) used in edge-observation checks."))
	float FoliageMinProjectedRadiusPixels = 16.0f;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ToolTip = "If enabled, observation requires a visibility trace from camera to target."))
	bool bUseOcclusionTrace = false;

	UPROPERTY(EditAnywhere, Category = "Season|Observation", meta = (ToolTip = "Trace channel used for optional occlusion checks and anchor traces."))
	TEnumAsByte<ECollisionChannel> ObservationTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "Material Parameter Collection that receives season/time globals at runtime."))
	TObjectPtr<UMaterialParameterCollection> SeasonParameterCollection = nullptr;

	// If true, the season/color values written to MPC are held until the player looks away.
	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "Delay observed season/color MPC swap until the player is no longer observing relevant visuals."))
	bool bDeferMPCSeasonVisualSwap = true;

	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "MPC scalar parameter name for the global (device/active rule) season index."))
	FName MPC_GlobalSeasonIndexParam = TEXT("GlobalSeasonIndex");

	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "MPC scalar parameter name for deferred observed season index used by materials."))
	FName MPC_ObservedSeasonIndexParam = TEXT("ObservedSeasonIndex");

	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "MPC scalar parameter name for active time-of-day hours (0-24)."))
	FName MPC_ActiveTimeOfDayParam = TEXT("ActiveTimeOfDayHours");

	UPROPERTY(EditAnywhere, Category = "Season|Materials", meta = (ToolTip = "MPC vector parameter name for deferred observed season color."))
	FName MPC_ObservedSeasonColorParam = TEXT("ObservedSeasonColor");

	UPROPERTY(EditAnywhere, Category = "Season|Foliage", meta = (ToolTip = "Enable per-instance deferred season swaps for HISM/Foliage Tool instances."))
	bool bEnableFoliageInstanceSeasonSwap = true;

	// Custom data float slot used by foliage/HISM materials for observed season index.
	UPROPERTY(EditAnywhere, Category = "Season|Foliage", meta = (ClampMin = "0", ToolTip = "PerInstanceCustomData slot index where observed season index is written for foliage instances."))
	int32 FoliageObservedSeasonCustomDataIndex = 0;

	// If true, any HISM component in the world is treated as seasonal foliage.
	UPROPERTY(EditAnywhere, Category = "Season|Foliage", meta = (ToolTip = "If true, all HISM components are treated as seasonal foliage; if false, only tagged components are used."))
	bool bTreatAllHISMAsSeasonalFoliage = true;

	// Used only when bTreatAllHISMAsSeasonalFoliage=false.
	UPROPERTY(EditAnywhere, Category = "Season|Foliage", meta = (ToolTip = "Component tag required for seasonal foliage when 'Treat All HISM As Seasonal Foliage' is false."))
	FName SeasonalFoliageComponentTag = TEXT("SeasonalFoliage");

	// Max pending foliage instances processed per tick.
	UPROPERTY(EditAnywhere, Category = "Season|Foliage", meta = (ClampMin = "1", ToolTip = "Maximum pending foliage instances applied per tick (higher = faster catch-up, more CPU)."))
	int32 FoliageDeferredApplyBatchSize = 512;

	UPROPERTY(EditAnywhere, Category = "Season|Colors", meta = (ToolTip = "Color used for Spring in observed season color outputs."))
	FLinearColor SpringColor = FLinearColor(0.14f, 0.72f, 0.18f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Colors", meta = (ToolTip = "Color used for Summer in observed season color outputs."))
	FLinearColor SummerColor = FLinearColor(0.95f, 0.85f, 0.20f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Colors", meta = (ToolTip = "Color used for Fall in observed season color outputs."))
	FLinearColor FallColor = FLinearColor(0.95f, 0.45f, 0.08f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Colors", meta = (ToolTip = "Color used for Winter in observed season color outputs."))
	FLinearColor WinterColor = FLinearColor(0.15f, 0.45f, 1.0f, 1.0f);

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	EWorldSeason GlobalSeason = EWorldSeason::Spring;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	float GlobalTimeOfDayHours = 12.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	EWorldSeason ActiveSeason = EWorldSeason::Spring;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime")
	float ActiveTimeOfDayHours = 12.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	bool bLastMPCWriteSucceeded = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	float LastWrittenGlobalSeasonIndex = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	float LastWrittenObservedSeasonIndex = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	float LastWrittenTimeOfDayHours = 12.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	FLinearColor LastWrittenSeasonColor = FLinearColor::White;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	EWorldSeason MPCVisualSeason = EWorldSeason::Spring;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	bool bPendingMPCVisualSeasonSwap = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Season|Runtime|MPC")
	EWorldSeason PendingMPCVisualSeason = EWorldSeason::Spring;

	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<ASeasonRegionVolume>> RegisteredVolumes;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SeasonalActors;

	UPROPERTY(Transient)
	TMap<TWeakObjectPtr<AActor>, EWorldSeason> PendingSeasonByActor;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>> SeasonalFoliageComponents;

	TMap<FFoliageSeasonInstanceKey, EWorldSeason> PendingSeasonByFoliageInstance;

	TWeakObjectPtr<ASeasonRegionVolume> ActiveVolume;
	float DevicePollAccumulator = 0.0f;

	void RefreshGlobalFromDevice();
	void RecomputeActiveRules(bool bBroadcastChanges);
	ASeasonRegionVolume* ChooseHighestPriorityVolume(APawn* PlayerPawn) const;
	void HandleSeasonTransition(EWorldSeason NewSeason);
	void ProcessDeferredSeasonTransitions();
	void QueueOrApplyMPCSeasonVisual(EWorldSeason NewSeason);
	void ProcessDeferredMPCSeasonVisual();
	AActor* GetCurrentViewAnchorActor(APlayerController* PlayerController) const;
	bool HasAnyObservedSeasonalActor(APlayerController* PlayerController) const;
	void RefreshSeasonalFoliageComponents();
	void HandleFoliageSeasonTransition(EWorldSeason NewSeason, APlayerController* PlayerController);
	void ProcessDeferredFoliageTransitions(APlayerController* PlayerController);
	bool IsFoliageInstanceObserved(UHierarchicalInstancedStaticMeshComponent* Component, int32 InstanceIndex, APlayerController* PlayerController) const;
	void ApplySeasonToFoliageInstance(UHierarchicalInstancedStaticMeshComponent* Component, int32 InstanceIndex, EWorldSeason Season);
	void TickRegionRuntimeClocks(float DeltaSeconds);
	void UpdateSunPitchFromActiveTime(bool bImmediate);
	void TickSunPitchInterpolation(float DeltaSeconds);
	float ComputeSunPitchFromTimeOfDay(float TimeOfDayHours) const;
	float GetSunCurrentPitch() const;
	float MakeEquivalentPitchNearReference(float ReferencePitch, float CandidatePitch) const;
	void ApplySeasonToActor(AActor* Actor, EWorldSeason Season);
	bool IsActorObserved(const AActor* Actor, APlayerController* PlayerController) const;
	void UpdateMaterialParameterCollection();

	static EWorldSeason SeasonFromMonth(int32 Month);
	static float TimeOfDayHoursFromDateTime(const FDateTime& DateTime);
	static float WrapTimeOfDayHours(float InHours);

	FLinearColor GetColorForSeason(EWorldSeason Season) const;

	bool bLoggedMissingCollection = false;
	bool bLoggedMissingCollectionInstance = false;
	TWeakObjectPtr<AActor> MPCObservationAnchorActor;

	bool bSunPitchInterpolationActive = false;
	float SunPitchInterpElapsed = 0.0f;
	float SunPitchInterpStart = 0.0f;
	float SunPitchInterpTarget = 0.0f;
	bool bSunPitchUnwrappedInitialized = false;
	float SunPitchUnwrappedCurrent = 0.0f;
	bool bSunBaselineRotationInitialized = false;
	float SunBaselineYaw = 0.0f;
	float SunBaselineRoll = 0.0f;
};
