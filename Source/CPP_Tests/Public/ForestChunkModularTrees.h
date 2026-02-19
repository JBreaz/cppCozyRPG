#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForestChunkModularTrees.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;

USTRUCT()
struct FForestInstanceSphere
{
	GENERATED_BODY()

	UPROPERTY() FVector Center = FVector::ZeroVector;
	UPROPERTY() float Radius = 0.0f;
	UPROPERTY() int32 InstanceIndex = INDEX_NONE;
};

USTRUCT()
struct FForestSocketInfo
{
	GENERATED_BODY()

	UPROPERTY() FName SocketName = NAME_None;

	// Trunk mesh component space (local space of SM_TrunkSocketReader)
	UPROPERTY() FTransform SocketLocal = FTransform::Identity;

	UPROPERTY() FVector SocketLocalPos = FVector::ZeroVector;

	// 0..1 along trunk height
	UPROPERTY() float HeightNormalized = 0.0f;
};

UCLASS(Blueprintable)
class CPP_TESTS_API AForestChunkModularTrees : public AActor
{
	GENERATED_BODY()

public:
	AForestChunkModularTrees();

	UFUNCTION(CallInEditor, Category="Forest|Build")
	void RebuildForest();

	UFUNCTION(CallInEditor, Category="Forest|Build")
	void ClearForest();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// ===== Components =====
	UPROPERTY(VisibleAnywhere, Category="Forest|Components")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Forest|Components")
	UHierarchicalInstancedStaticMeshComponent* HISM_Trunks = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Forest|Components")
	UHierarchicalInstancedStaticMeshComponent* HISM_Branches = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Forest|Components")
	UStaticMeshComponent* SM_TrunkSocketReader = nullptr;

	// ===== Meshes =====
	UPROPERTY(EditAnywhere, Category="Forest|Meshes")
	UStaticMesh* TrunkMesh = nullptr;

	UPROPERTY(EditAnywhere, Category="Forest|Meshes")
	UStaticMesh* BranchMesh = nullptr;

	// ===== Layout =====
	UPROPERTY(EditAnywhere, Category="Forest|Layout")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, Category="Forest|Layout", meta=(ClampMin="100.0"))
	FVector2D ChunkSize = FVector2D(10000.0f, 10000.0f);

	UPROPERTY(EditAnywhere, Category="Forest|Layout", meta=(ClampMin="10.0"))
	float GridSpacing = 450.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Layout", meta=(ClampMin="0.0"))
	float JitterRadius = 160.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Layout")
	bool bCenterChunkOnActor = true;

	UPROPERTY(EditAnywhere, Category="Forest|Layout")
	bool bAutoRebuildInEditor = false;

	UPROPERTY(EditAnywhere, Category="Forest|Layout")
	bool bRebuildOnBeginPlay = false;

	UPROPERTY(EditAnywhere, Category="Forest|Layout", meta=(ClampMin="0.0"))
	float TrunkYawRandomDegrees = 180.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Layout", meta=(ClampMin="0.01"))
	FVector2D TrunkUniformScaleRange = FVector2D(0.9f, 1.15f);

	// ===== Branch rules =====
	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0"))
	int32 MinBranchesPerTree = 8;

	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0"))
	int32 MaxBranchesPerTree = 14;

	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0.01"))
	float ScaleBottom = 1.10f;

	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0.01"))
	float ScaleTop = 0.70f;

	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BranchScaleRandomPct = 0.12f;

	UPROPERTY(EditAnywhere, Category="Forest|Branches", meta=(ClampMin="0.0"))
	float BranchTwistRandomDegrees = 180.0f;

	// ===== Overlap / pruning =====
	UPROPERTY(EditAnywhere, Category="Forest|Overlap")
	bool bRejectTrunkOverlap = true;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap")
	bool bPruneBranchOverlap = true;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap")
	bool bBranchCollidesWithTrunks = true;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap")
	bool bBranchCollidesWithBranches = true;

	// If 0, trunk uses mesh XY footprint radius (recommended). If >0, uses this.
	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.0"))
	float TrunkCollisionRadiusOverride = 0.0f;

	// If 0, branch uses mesh sphere radius. If >0, uses this.
	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.0"))
	float BranchCollisionRadiusOverride = 0.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.1"))
	float TrunkCollisionRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.1"))
	float BranchCollisionRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.0"))
	float TrunkCellSizeOverride = 0.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Overlap", meta=(ClampMin="0.0"))
	float BranchCellSizeOverride = 0.0f;

	// ===== Rendering / collision =====
	UPROPERTY(EditAnywhere, Category="Forest|Rendering")
	bool bEnableTrunkCollision = false;

	// ===== Debug =====
	UPROPERTY(EditAnywhere, Category="Forest|Debug")
	bool bDebugDraw = false;

	UPROPERTY(EditAnywhere, Category="Forest|Debug", meta=(ClampMin="0.0"))
	float DebugDrawDuration = 10.0f;

	UPROPERTY(EditAnywhere, Category="Forest|Debug")
	bool bDebugDrawSockets = false;

	UPROPERTY(EditAnywhere, Category="Forest|Debug")
	bool bDebugDrawBranchPoints = true;

private:
	// Cached socket data
	UPROPERTY(Transient)
	TArray<FForestSocketInfo> CachedSockets;

	// Spheres used for overlap checks
	UPROPERTY(Transient)
	TArray<FForestInstanceSphere> TrunkSpheres;

	UPROPERTY(Transient)
	TArray<FForestInstanceSphere> BranchSpheres;

	// IMPORTANT: Not UPROPERTY. UHT does not support nested containers (TMap value = TArray).
	TMap<FIntPoint, TArray<int32>> TrunkCellMap;
	TMap<FIntPoint, TArray<int32>> BranchCellMap;

	float CachedTrunkRadius = 0.0f;
	float CachedBranchRadius = 0.0f;
	float TrunkCellSize = 0.0f;
	float BranchCellSize = 0.0f;

private:
	void ResetRuntimeState();
	void ConfigureComponentsForMeshes();

	bool CacheSocketData();

	// Spatial hash helpers
	FIntPoint ToCell2D(const FVector& WorldPos, float CellSize) const;
	void AddSphereToCellMap(const FForestInstanceSphere& Sphere, int32 SphereIndex, float CellSize, TMap<FIntPoint, TArray<int32>>& CellMap);

	// Overlap tests
	bool HasTrunkOverlap2D(const FVector& CandidateCenter, float CandidateRadius) const;
	bool BranchOverlapsAnyTrunk2D(const FVector& CandidateCenter, float CandidateRadius) const;
	bool HasBranchOverlap(const FVector& CandidateCenter, float CandidateRadius) const;

	// Sphere builders
	FForestInstanceSphere MakeTrunkSphere(const FTransform& WorldXform, int32 InstanceIndex) const;
	FForestInstanceSphere MakeBranchSphere(const FTransform& WorldXform, int32 InstanceIndex) const;
};
