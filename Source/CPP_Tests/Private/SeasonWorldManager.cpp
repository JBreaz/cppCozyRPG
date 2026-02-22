#include "SeasonWorldManager.h"

#include "CPP_Tests.h"
#include "SeasonRegionVolume.h"
#include "SeasonalStaticMeshActor.h"
#include "SeasonalVisualInterface.h"
#include "CollisionQueryParams.h"
#include "EngineUtils.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/RotationMatrix.h"

ASeasonWorldManager::ASeasonWorldManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASeasonWorldManager::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASeasonRegionVolume> It(World); It; ++It)
		{
			RegisterRegionVolume(*It);
		}
	}

	RefreshSeasonalActors();
	RefreshSeasonalFoliageComponents();
	RefreshGlobalFromDevice();

	ActiveSeason = GlobalSeason;
	ActiveTimeOfDayHours = GlobalTimeOfDayHours;
	MPCVisualSeason = ActiveSeason;
	PendingMPCVisualSeason = ActiveSeason;
	bPendingMPCVisualSeasonSwap = false;
	MPCObservationAnchorActor = nullptr;

	for (const TWeakObjectPtr<ASeasonRegionVolume>& WeakVolume : RegisteredVolumes)
	{
		if (ASeasonRegionVolume* Volume = WeakVolume.Get())
		{
			Volume->InitializeRuntimeClock(GlobalTimeOfDayHours);
		}
	}

	// On startup, initialize all seasonal actors immediately.
	for (TWeakObjectPtr<AActor>& WeakActor : SeasonalActors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			ApplySeasonToActor(Actor, ActiveSeason);
		}
	}

	if (bEnableFoliageInstanceSeasonSwap)
	{
		for (const TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>& WeakComponent : SeasonalFoliageComponents)
		{
			UHierarchicalInstancedStaticMeshComponent* Component = WeakComponent.Get();
			if (!IsValid(Component))
			{
				continue;
			}

			const int32 InstanceCount = Component->GetInstanceCount();
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				ApplySeasonToFoliageInstance(Component, InstanceIndex, ActiveSeason);
			}
		}
	}

	RecomputeActiveRules(false);
	UpdateSunPitchFromActiveTime(true);
	UpdateMaterialParameterCollection();
}

void ASeasonWorldManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickRegionRuntimeClocks(DeltaSeconds);

	DevicePollAccumulator += FMath::Max(0.0f, DeltaSeconds);

	if (DevicePollAccumulator >= DevicePollIntervalSeconds)
	{
		DevicePollAccumulator = 0.0f;
		RecomputeActiveRules(true);
	}

	ProcessDeferredSeasonTransitions();
	TickSunPitchInterpolation(DeltaSeconds);
}

void ASeasonWorldManager::RegisterRegionVolume(ASeasonRegionVolume* Volume)
{
	if (!IsValid(Volume))
	{
		return;
	}

	RegisteredVolumes.Add(Volume);

	if (HasActorBegunPlay())
	{
		Volume->InitializeRuntimeClock(GlobalTimeOfDayHours);
		RecomputeActiveRules(false);
	}
}

void ASeasonWorldManager::UnregisterRegionVolume(ASeasonRegionVolume* Volume)
{
	if (!IsValid(Volume))
	{
		return;
	}

	RegisteredVolumes.Remove(Volume);

	if (HasActorBegunPlay())
	{
		RecomputeActiveRules(false);
	}
}

void ASeasonWorldManager::NotifyRegionOverlapChanged(ASeasonRegionVolume* SourceVolume)
{
	(void)SourceVolume;
	RecomputeActiveRules(true);
}

void ASeasonWorldManager::ForceRecompute()
{
	RefreshGlobalFromDevice();
	RecomputeActiveRules(true);
	ProcessDeferredSeasonTransitions();
}

void ASeasonWorldManager::RefreshSeasonalActors()
{
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithInterface(this, USeasonalVisualInterface::StaticClass(), FoundActors);

	SeasonalActors.Reset(FoundActors.Num());
	for (AActor* FoundActor : FoundActors)
	{
		SeasonalActors.Add(FoundActor);
	}
}

void ASeasonWorldManager::RefreshGlobalFromDevice()
{
	const FDateTime DeviceDateTime = FDateTime::Now();
	GlobalSeason = SeasonFromMonth(DeviceDateTime.GetMonth());
	GlobalTimeOfDayHours = TimeOfDayHoursFromDateTime(DeviceDateTime);
}

void ASeasonWorldManager::RecomputeActiveRules(bool bBroadcastChanges)
{
	if (!GetWorld())
	{
		return;
	}

	RefreshGlobalFromDevice();

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	ASeasonRegionVolume* HighestPriorityVolume = ChooseHighestPriorityVolume(PlayerPawn);
	ActiveVolume = HighestPriorityVolume;

	EWorldSeason NewActiveSeason = GlobalSeason;
	float NewActiveTimeOfDay = GlobalTimeOfDayHours;

	if (IsValid(HighestPriorityVolume))
	{
		HighestPriorityVolume->BuildOverride(GlobalTimeOfDayHours, NewActiveSeason, NewActiveTimeOfDay);
	}

	NewActiveTimeOfDay = WrapTimeOfDayHours(NewActiveTimeOfDay);

	const bool bSeasonChanged = (NewActiveSeason != ActiveSeason);
	const bool bTimeChanged = !FMath::IsNearlyEqual(NewActiveTimeOfDay, ActiveTimeOfDayHours, 0.001f);

	if (bSeasonChanged)
	{
		ActiveSeason = NewActiveSeason;
		HandleSeasonTransition(ActiveSeason);
		QueueOrApplyMPCSeasonVisual(ActiveSeason);

		if (bBroadcastChanges)
		{
			OnActiveSeasonChanged.Broadcast(ActiveSeason);
		}
	}

	if (bTimeChanged)
	{
		ActiveTimeOfDayHours = NewActiveTimeOfDay;
		UpdateSunPitchFromActiveTime(false);

		if (bBroadcastChanges)
		{
			OnActiveTimeOfDayChanged.Broadcast(ActiveTimeOfDayHours);
		}
	}

	UpdateMaterialParameterCollection();
}

ASeasonRegionVolume* ASeasonWorldManager::ChooseHighestPriorityVolume(APawn* PlayerPawn) const
{
	if (!IsValid(PlayerPawn))
	{
		return nullptr;
	}

	ASeasonRegionVolume* BestVolume = nullptr;
	int32 BestPriority = TNumericLimits<int32>::Lowest();

	for (const TWeakObjectPtr<ASeasonRegionVolume>& WeakVolume : RegisteredVolumes)
	{
		ASeasonRegionVolume* Volume = WeakVolume.Get();
		if (!IsValid(Volume))
		{
			continue;
		}

		if (!Volume->IsActorInside(PlayerPawn))
		{
			continue;
		}

		const int32 VolumePriority = Volume->GetPriority();
		if (!IsValid(BestVolume) || VolumePriority > BestPriority)
		{
			BestVolume = Volume;
			BestPriority = VolumePriority;
		}
	}

	return BestVolume;
}

void ASeasonWorldManager::HandleSeasonTransition(EWorldSeason NewSeason)
{
	RefreshSeasonalActors();
	RefreshSeasonalFoliageComponents();

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

	for (const TWeakObjectPtr<AActor>& WeakActor : SeasonalActors)
	{
		AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		const bool bObserved = IsActorObserved(Actor, PlayerController);
		if (bObserved)
		{
			PendingSeasonByActor.Add(TWeakObjectPtr<AActor>(Actor), NewSeason);

			if (ASeasonalStaticMeshActor* SeasonalActor = Cast<ASeasonalStaticMeshActor>(Actor))
			{
				SeasonalActor->SetPendingSeason(NewSeason);
			}
		}
		else
		{
			ApplySeasonToActor(Actor, NewSeason);
		}
	}

	HandleFoliageSeasonTransition(NewSeason, PlayerController);
}

void ASeasonWorldManager::ProcessDeferredSeasonTransitions()
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

	if (PendingSeasonByActor.Num() > 0)
	{
		for (auto It = PendingSeasonByActor.CreateIterator(); It; ++It)
		{
			AActor* Actor = It.Key().Get();
			if (!IsValid(Actor))
			{
				It.RemoveCurrent();
				continue;
			}

			if (!IsActorObserved(Actor, PlayerController))
			{
				ISeasonalVisualInterface::Execute_ApplySeasonVisual(Actor, It.Value());
				It.RemoveCurrent();
			}
		}
	}

	ProcessDeferredFoliageTransitions(PlayerController);
	ProcessDeferredMPCSeasonVisual();
}

void ASeasonWorldManager::ApplySeasonToActor(AActor* Actor, EWorldSeason Season)
{
	if (!IsValid(Actor))
	{
		return;
	}

	if (Actor->GetClass()->ImplementsInterface(USeasonalVisualInterface::StaticClass()))
	{
		ISeasonalVisualInterface::Execute_ApplySeasonVisual(Actor, Season);
	}

	PendingSeasonByActor.Remove(TWeakObjectPtr<AActor>(Actor));
}

bool ASeasonWorldManager::IsActorObserved(const AActor* Actor, APlayerController* PlayerController) const
{
	if (!IsValid(Actor) || !IsValid(PlayerController))
	{
		return false;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	FVector ActorOrigin = FVector::ZeroVector;
	FVector ActorExtent = FVector::ZeroVector;
	Actor->GetActorBounds(true, ActorOrigin, ActorExtent);

	const FVector ToActor = ActorOrigin - CameraLocation;
	if (ToActor.IsNearlyZero())
	{
		return true;
	}

	const float FacingDot = FVector::DotProduct(CameraRotation.Vector(), ToActor.GetSafeNormal());
	if (FacingDot < ObservedFrontDotThreshold)
	{
		return false;
	}

	FVector2D ScreenLocation = FVector2D::ZeroVector;
	if (!PlayerController->ProjectWorldLocationToScreen(ActorOrigin, ScreenLocation, false))
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return false;
	}

	if (ScreenLocation.X < 0.0f || ScreenLocation.Y < 0.0f
		|| ScreenLocation.X > static_cast<float>(ViewportX)
		|| ScreenLocation.Y > static_cast<float>(ViewportY))
	{
		return false;
	}

	if (!bUseOcclusionTrace)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SeasonObservedTrace), true);
	QueryParams.AddIgnoredActor(this);

	if (APawn* PlayerPawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		CameraLocation,
		ActorOrigin,
		ObservationTraceChannel,
		QueryParams);

	if (!bHit)
	{
		return true;
	}

	const AActor* HitActor = Hit.GetActor();
	if (!IsValid(HitActor))
	{
		return false;
	}

	return (HitActor == Actor) || HitActor->IsOwnedBy(Actor) || Actor->IsOwnedBy(HitActor);
}

void ASeasonWorldManager::QueueOrApplyMPCSeasonVisual(EWorldSeason NewSeason)
{
	if (!bDeferMPCSeasonVisualSwap)
	{
		MPCVisualSeason = NewSeason;
		PendingMPCVisualSeason = NewSeason;
		bPendingMPCVisualSeasonSwap = false;
		MPCObservationAnchorActor = nullptr;
		UpdateMaterialParameterCollection();
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

	// If any seasonal actor is observed, defer global visual season swap.
	if (HasAnyObservedSeasonalActor(PlayerController))
	{
		PendingMPCVisualSeason = NewSeason;
		bPendingMPCVisualSeasonSwap = true;
		MPCObservationAnchorActor = nullptr;
		UpdateMaterialParameterCollection();
		return;
	}

	// Fallback for MPC-only setups: keep current color until the currently viewed anchor is no longer observed.
	AActor* AnchorActor = GetCurrentViewAnchorActor(PlayerController);
	if (IsValid(AnchorActor) && IsActorObserved(AnchorActor, PlayerController))
	{
		PendingMPCVisualSeason = NewSeason;
		bPendingMPCVisualSeasonSwap = true;
		MPCObservationAnchorActor = AnchorActor;
		UpdateMaterialParameterCollection();
		return;
	}

	MPCVisualSeason = NewSeason;
	PendingMPCVisualSeason = NewSeason;
	bPendingMPCVisualSeasonSwap = false;
	MPCObservationAnchorActor = nullptr;
	UpdateMaterialParameterCollection();
}

void ASeasonWorldManager::ProcessDeferredMPCSeasonVisual()
{
	if (!bPendingMPCVisualSeasonSwap)
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);

	if (PendingSeasonByActor.Num() > 0)
	{
		return;
	}

	if (AActor* AnchorActor = MPCObservationAnchorActor.Get())
	{
		if (IsActorObserved(AnchorActor, PlayerController))
		{
			return;
		}
	}
	else if (HasAnyObservedSeasonalActor(PlayerController))
	{
		return;
	}

	MPCVisualSeason = PendingMPCVisualSeason;
	bPendingMPCVisualSeasonSwap = false;
	MPCObservationAnchorActor = nullptr;
	UpdateMaterialParameterCollection();
}

AActor* ASeasonWorldManager::GetCurrentViewAnchorActor(APlayerController* PlayerController) const
{
	if (!IsValid(PlayerController))
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const FVector TraceStart = CameraLocation;
	const FVector TraceEnd = CameraLocation + (CameraRotation.Vector() * 100000.0f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SeasonMPCAnchorTrace), true);
	QueryParams.AddIgnoredActor(this);
	if (APawn* PlayerPawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		TraceStart,
		TraceEnd,
		ObservationTraceChannel,
		QueryParams);

	return bHit ? Hit.GetActor() : nullptr;
}

bool ASeasonWorldManager::HasAnyObservedSeasonalActor(APlayerController* PlayerController) const
{
	for (const TWeakObjectPtr<AActor>& WeakActor : SeasonalActors)
	{
		AActor* Actor = WeakActor.Get();
		if (!IsValid(Actor))
		{
			continue;
		}

		if (IsActorObserved(Actor, PlayerController))
		{
			return true;
		}
	}

	return false;
}

void ASeasonWorldManager::RefreshSeasonalFoliageComponents()
{
	SeasonalFoliageComponents.Reset();

	if (!bEnableFoliageInstanceSeasonSwap)
	{
		PendingSeasonByFoliageInstance.Reset();
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* OwnerActor = *It;
		if (!IsValid(OwnerActor))
		{
			continue;
		}

		TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> HISMComponents;
		OwnerActor->GetComponents(HISMComponents);

		for (UHierarchicalInstancedStaticMeshComponent* Component : HISMComponents)
		{
			if (!IsValid(Component))
			{
				continue;
			}

			if (Component->GetInstanceCount() <= 0)
			{
				continue;
			}

			if (!bTreatAllHISMAsSeasonalFoliage && !Component->ComponentHasTag(SeasonalFoliageComponentTag))
			{
				continue;
			}

			if (Component->NumCustomDataFloats <= FoliageObservedSeasonCustomDataIndex)
			{
				Component->SetNumCustomDataFloats(FoliageObservedSeasonCustomDataIndex + 1);
			}

			SeasonalFoliageComponents.Add(Component);
		}
	}
}

void ASeasonWorldManager::HandleFoliageSeasonTransition(EWorldSeason NewSeason, APlayerController* PlayerController)
{
	if (!bEnableFoliageInstanceSeasonSwap)
	{
		PendingSeasonByFoliageInstance.Reset();
		return;
	}

	for (auto It = SeasonalFoliageComponents.CreateIterator(); It; ++It)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = It->Get();
		if (!IsValid(Component))
		{
			It.RemoveCurrent();
			continue;
		}

		const int32 InstanceCount = Component->GetInstanceCount();
		for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
		{
			FFoliageSeasonInstanceKey Key;
			Key.Component = Component;
			Key.InstanceIndex = InstanceIndex;

			if (IsFoliageInstanceObserved(Component, InstanceIndex, PlayerController))
			{
				PendingSeasonByFoliageInstance.Add(Key, NewSeason);
			}
			else
			{
				ApplySeasonToFoliageInstance(Component, InstanceIndex, NewSeason);
				PendingSeasonByFoliageInstance.Remove(Key);
			}
		}
	}
}

void ASeasonWorldManager::ProcessDeferredFoliageTransitions(APlayerController* PlayerController)
{
	if (!bEnableFoliageInstanceSeasonSwap || PendingSeasonByFoliageInstance.Num() <= 0)
	{
		return;
	}

	int32 ProcessedCount = 0;
	for (auto It = PendingSeasonByFoliageInstance.CreateIterator(); It; ++It)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = It.Key().Component.Get();
		const int32 InstanceIndex = It.Key().InstanceIndex;

		if (!IsValid(Component) || InstanceIndex < 0 || InstanceIndex >= Component->GetInstanceCount())
		{
			It.RemoveCurrent();
			continue;
		}

		++ProcessedCount;

		if (!IsFoliageInstanceObserved(Component, InstanceIndex, PlayerController))
		{
			ApplySeasonToFoliageInstance(Component, InstanceIndex, It.Value());
			It.RemoveCurrent();
		}

		if (ProcessedCount >= FoliageDeferredApplyBatchSize)
		{
			break;
		}
	}
}

bool ASeasonWorldManager::IsFoliageInstanceObserved(UHierarchicalInstancedStaticMeshComponent* Component, int32 InstanceIndex, APlayerController* PlayerController) const
{
	if (!IsValid(Component) || !IsValid(PlayerController))
	{
		return false;
	}

	FTransform InstanceTransform = FTransform::Identity;
	if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
	{
		return false;
	}

	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	FVector LocalBoundsOrigin = FVector::ZeroVector;
	float LocalSphereRadius = 50.0f;
	if (IsValid(StaticMesh))
	{
		const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
		LocalBoundsOrigin = MeshBounds.Origin;
		LocalSphereRadius = FMath::Max(1.0f, MeshBounds.SphereRadius);
	}

	const FVector InstanceLocation = InstanceTransform.TransformPosition(LocalBoundsOrigin);
	const float InstanceScale = InstanceTransform.GetScale3D().GetAbsMax();
	const float WorldSphereRadius = FMath::Max(1.0f, LocalSphereRadius * InstanceScale);

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const FVector ToInstance = InstanceLocation - CameraLocation;
	const float DistanceToInstance = ToInstance.Size();
	const float CloseObservedDistance = FMath::Max(WorldSphereRadius, FoliageAlwaysObservedDistanceCm);
	const bool bWithinCloseObservedDistance = DistanceToInstance <= CloseObservedDistance;

	if (ToInstance.IsNearlyZero())
	{
		return true;
	}

	const float FacingDot = FVector::DotProduct(CameraRotation.Vector(), ToInstance / DistanceToInstance);
	const float AngularSlack = FMath::Clamp(WorldSphereRadius / FMath::Max(1.0f, DistanceToInstance), 0.0f, 1.0f);
	float EffectiveFrontDotThreshold = ObservedFrontDotThreshold - AngularSlack;
	if (bWithinCloseObservedDistance)
	{
		// Relax front-angle check when very close so visible near-edge foliage doesn't pop,
		// while still allowing behind-the-player instances to be considered unobserved.
		EffectiveFrontDotThreshold -= 0.15f;
	}
	if (FacingDot < EffectiveFrontDotThreshold)
	{
		return false;
	}

	FVector2D ScreenCenter = FVector2D::ZeroVector;
	if (!PlayerController->ProjectWorldLocationToScreen(InstanceLocation, ScreenCenter, false))
	{
		return false;
	}

	int32 ViewportX = 0;
	int32 ViewportY = 0;
	PlayerController->GetViewportSize(ViewportX, ViewportY);

	if (ViewportX <= 0 || ViewportY <= 0)
	{
		return false;
	}

	const FVector CameraRight = FRotationMatrix(CameraRotation).GetUnitAxis(EAxis::Y);
	FVector2D ScreenRightOffset = FVector2D::ZeroVector;
	float ProjectedRadiusPixels = 0.0f;
	if (PlayerController->ProjectWorldLocationToScreen(InstanceLocation + (CameraRight * WorldSphereRadius), ScreenRightOffset, false))
	{
		ProjectedRadiusPixels = FMath::Abs(ScreenRightOffset.X - ScreenCenter.X);
	}
	ProjectedRadiusPixels = FMath::Max(ProjectedRadiusPixels, FoliageMinProjectedRadiusPixels);

	const float NearAlpha = bWithinCloseObservedDistance
		? (1.0f - FMath::Clamp(DistanceToInstance / FMath::Max(KINDA_SMALL_NUMBER, CloseObservedDistance), 0.0f, 1.0f))
		: 0.0f;
	const float ScreenPadding = (ProjectedRadiusPixels + FoliageScreenEdgePaddingPixels)
		+ (NearAlpha * (ProjectedRadiusPixels + FoliageScreenEdgePaddingPixels));

	if (ScreenCenter.X < -ScreenPadding || ScreenCenter.Y < -ScreenPadding
		|| ScreenCenter.X > static_cast<float>(ViewportX) + ScreenPadding
		|| ScreenCenter.Y > static_cast<float>(ViewportY) + ScreenPadding)
	{
		return false;
	}

	if (!bUseOcclusionTrace)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return true;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SeasonFoliageObservedTrace), true);
	QueryParams.AddIgnoredActor(this);
	if (APawn* PlayerPawn = PlayerController->GetPawn())
	{
		QueryParams.AddIgnoredActor(PlayerPawn);
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		CameraLocation,
		InstanceLocation,
		ObservationTraceChannel,
		QueryParams);

	if (!bHit)
	{
		return true;
	}

	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	return HitComponent == Component;
}

void ASeasonWorldManager::ApplySeasonToFoliageInstance(UHierarchicalInstancedStaticMeshComponent* Component, int32 InstanceIndex, EWorldSeason Season)
{
	if (!IsValid(Component) || InstanceIndex < 0 || InstanceIndex >= Component->GetInstanceCount())
	{
		return;
	}

	if (Component->NumCustomDataFloats <= FoliageObservedSeasonCustomDataIndex)
	{
		Component->SetNumCustomDataFloats(FoliageObservedSeasonCustomDataIndex + 1);
	}

	const float SeasonIndex = static_cast<float>(static_cast<uint8>(Season));
	Component->SetCustomDataValue(InstanceIndex, FoliageObservedSeasonCustomDataIndex, SeasonIndex, true);
}

void ASeasonWorldManager::TickRegionRuntimeClocks(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}

	for (auto It = RegisteredVolumes.CreateIterator(); It; ++It)
	{
		ASeasonRegionVolume* Volume = It->Get();
		if (!IsValid(Volume))
		{
			It.RemoveCurrent();
			continue;
		}

		Volume->TickRuntimeClock(DeltaSeconds);
	}
}

void ASeasonWorldManager::UpdateSunPitchFromActiveTime(bool bImmediate)
{
	if (!IsValid(SunDirectionalLight))
	{
		bSunPitchInterpolationActive = false;
		bSunPitchUnwrappedInitialized = false;
		bSunBaselineRotationInitialized = false;
		return;
	}

	if (!bSunBaselineRotationInitialized)
	{
		const FRotator CurrentRotation = SunDirectionalLight->GetActorRotation();
		SunBaselineYaw = CurrentRotation.Yaw;
		SunBaselineRoll = CurrentRotation.Roll;
		bSunBaselineRotationInitialized = true;
	}

	const float RawTargetPitch = ComputeSunPitchFromTimeOfDay(ActiveTimeOfDayHours);
	if (!bSunPitchUnwrappedInitialized)
	{
		SunPitchUnwrappedCurrent = GetSunCurrentPitch();
		bSunPitchUnwrappedInitialized = true;
	}

	const float TargetPitch = MakeEquivalentPitchNearReference(SunPitchUnwrappedCurrent, RawTargetPitch);

	if (bImmediate || SunPitchLerpSeconds <= KINDA_SMALL_NUMBER)
	{
		FRotator SunRotation(FRotator::NormalizeAxis(TargetPitch), SunBaselineYaw, SunBaselineRoll);
		SunDirectionalLight->SetActorRotation(SunRotation);
		SunPitchUnwrappedCurrent = TargetPitch;
		bSunPitchInterpolationActive = false;
		return;
	}

	const float StartPitch = SunPitchUnwrappedCurrent;
	const float DeltaToTarget = FMath::Abs(TargetPitch - StartPitch);
	if (DeltaToTarget <= 0.01f)
	{
		SunPitchUnwrappedCurrent = TargetPitch;
		bSunPitchInterpolationActive = false;
		return;
	}

	SunPitchInterpStart = StartPitch;
	SunPitchInterpTarget = TargetPitch;
	SunPitchInterpElapsed = 0.0f;
	bSunPitchInterpolationActive = true;
}

void ASeasonWorldManager::TickSunPitchInterpolation(float DeltaSeconds)
{
	if (!bSunPitchInterpolationActive || !IsValid(SunDirectionalLight))
	{
		return;
	}

	if (SunPitchLerpSeconds <= KINDA_SMALL_NUMBER)
	{
		FRotator SunRotation(FRotator::NormalizeAxis(SunPitchInterpTarget), SunBaselineYaw, SunBaselineRoll);
		SunDirectionalLight->SetActorRotation(SunRotation);
		SunPitchUnwrappedCurrent = SunPitchInterpTarget;
		bSunPitchInterpolationActive = false;
		return;
	}

	SunPitchInterpElapsed += FMath::Max(0.0f, DeltaSeconds);
	const float Alpha = FMath::Clamp(SunPitchInterpElapsed / SunPitchLerpSeconds, 0.0f, 1.0f);
	const float NewPitch = FMath::Lerp(SunPitchInterpStart, SunPitchInterpTarget, Alpha);
	SunPitchUnwrappedCurrent = NewPitch;

	FRotator SunRotation(FRotator::NormalizeAxis(NewPitch), SunBaselineYaw, SunBaselineRoll);
	SunDirectionalLight->SetActorRotation(SunRotation);

	if (Alpha >= 1.0f)
	{
		SunPitchUnwrappedCurrent = SunPitchInterpTarget;
		bSunPitchInterpolationActive = false;
	}
}

float ASeasonWorldManager::ComputeSunPitchFromTimeOfDay(float TimeOfDayHours) const
{
	// 15 degrees per hour, anchored so 12:00 -> SunPitchAtNoon.
	// Kept intentionally unwrapped to avoid +/-180 seam oscillation.
	return SunPitchAtNoon + ((TimeOfDayHours - 12.0f) * 15.0f);
}

float ASeasonWorldManager::GetSunCurrentPitch() const
{
	if (!IsValid(SunDirectionalLight))
	{
		return 0.0f;
	}

	return SunDirectionalLight->GetActorRotation().Pitch;
}

float ASeasonWorldManager::MakeEquivalentPitchNearReference(float ReferencePitch, float CandidatePitch) const
{
	// Convert CandidatePitch to the equivalent representation nearest ReferencePitch.
	// This keeps interpolation stable when crossing 180/-180.
	const float Delta = FMath::FindDeltaAngleDegrees(ReferencePitch, CandidatePitch);
	return ReferencePitch + Delta;
}

void ASeasonWorldManager::UpdateMaterialParameterCollection()
{
	if (!IsValid(SeasonParameterCollection))
	{
		bLastMPCWriteSucceeded = false;
		if (bLogMPCWriteFailures && !bLoggedMissingCollection)
		{
			UE_LOG(LogCPP_Tests, Warning,
				TEXT("SeasonWorldManager '%s': SeasonParameterCollection is not assigned. MPC updates are skipped."),
				*GetName());
			bLoggedMissingCollection = true;
		}
		return;
	}
	bLoggedMissingCollection = false;

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		bLastMPCWriteSucceeded = false;
		return;
	}

	UMaterialParameterCollectionInstance* Collection = World->GetParameterCollectionInstance(SeasonParameterCollection);
	if (!IsValid(Collection))
	{
		bLastMPCWriteSucceeded = false;
		if (bLogMPCWriteFailures && !bLoggedMissingCollectionInstance)
		{
			UE_LOG(LogCPP_Tests, Warning,
				TEXT("SeasonWorldManager '%s': could not get collection instance for '%s'."),
				*GetName(),
				*GetNameSafe(SeasonParameterCollection));
			bLoggedMissingCollectionInstance = true;
		}
		return;
	}
	bLoggedMissingCollectionInstance = false;

	const float GlobalSeasonIndex = static_cast<float>(static_cast<uint8>(GlobalSeason));
	const float ObservedSeasonIndex = static_cast<float>(static_cast<uint8>(MPCVisualSeason));
	const FLinearColor ObservedSeasonColor = GetColorForSeason(MPCVisualSeason);

	const bool bGlobalSeasonIndexSet = Collection->SetScalarParameterValue(MPC_GlobalSeasonIndexParam, GlobalSeasonIndex);
	const bool bObservedSeasonIndexSet = Collection->SetScalarParameterValue(MPC_ObservedSeasonIndexParam, ObservedSeasonIndex);
	const bool bTimeSet = Collection->SetScalarParameterValue(MPC_ActiveTimeOfDayParam, ActiveTimeOfDayHours);
	const bool bColorSet = Collection->SetVectorParameterValue(MPC_ObservedSeasonColorParam, ObservedSeasonColor);

	bLastMPCWriteSucceeded = bGlobalSeasonIndexSet && bObservedSeasonIndexSet && bTimeSet && bColorSet;
	LastWrittenGlobalSeasonIndex = GlobalSeasonIndex;
	LastWrittenObservedSeasonIndex = ObservedSeasonIndex;
	LastWrittenTimeOfDayHours = ActiveTimeOfDayHours;
	LastWrittenSeasonColor = ObservedSeasonColor;

	if (!bLastMPCWriteSucceeded && bLogMPCWriteFailures)
	{
		UE_LOG(LogCPP_Tests, Warning,
			TEXT("SeasonWorldManager '%s': MPC write failed. Missing params? Expected Scalar='%s','%s','%s' Vector='%s' on collection '%s'."),
			*GetName(),
			*MPC_GlobalSeasonIndexParam.ToString(),
			*MPC_ObservedSeasonIndexParam.ToString(),
			*MPC_ActiveTimeOfDayParam.ToString(),
			*MPC_ObservedSeasonColorParam.ToString(),
			*GetNameSafe(SeasonParameterCollection));
	}
}

EWorldSeason ASeasonWorldManager::SeasonFromMonth(int32 Month)
{
	if (Month >= 3 && Month <= 5)
	{
		return EWorldSeason::Spring;
	}

	if (Month >= 6 && Month <= 8)
	{
		return EWorldSeason::Summer;
	}

	if (Month >= 9 && Month <= 11)
	{
		return EWorldSeason::Fall;
	}

	return EWorldSeason::Winter;
}

float ASeasonWorldManager::TimeOfDayHoursFromDateTime(const FDateTime& DateTime)
{
	const float Hours = static_cast<float>(DateTime.GetHour());
	const float Minutes = static_cast<float>(DateTime.GetMinute()) / 60.0f;
	const float Seconds = static_cast<float>(DateTime.GetSecond()) / 3600.0f;
	return WrapTimeOfDayHours(Hours + Minutes + Seconds);
}

float ASeasonWorldManager::WrapTimeOfDayHours(float InHours)
{
	float Wrapped = FMath::Fmod(InHours, 24.0f);
	if (Wrapped < 0.0f)
	{
		Wrapped += 24.0f;
	}
	return Wrapped;
}

FLinearColor ASeasonWorldManager::GetColorForSeason(EWorldSeason Season) const
{
	switch (Season)
	{
	case EWorldSeason::Spring:
		return SpringColor;
	case EWorldSeason::Summer:
		return SummerColor;
	case EWorldSeason::Fall:
		return FallColor;
	case EWorldSeason::Winter:
	default:
		return WinterColor;
	}
}
