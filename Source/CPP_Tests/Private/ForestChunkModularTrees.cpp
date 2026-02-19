#include "ForestChunkModularTrees.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"

static bool ParseSocketNumber(const FName& SocketName, int32& OutNumber)
{
	const FString S = SocketName.ToString();
	if (!S.StartsWith(TEXT("Socket_")))
	{
		return false;
	}
	const FString NumStr = S.RightChop(7);
	if (!NumStr.IsNumeric())
	{
		return false;
	}
	OutNumber = FCString::Atoi(*NumStr);
	return true;
}

AForestChunkModularTrees::AForestChunkModularTrees()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	HISM_Trunks = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HISM_Trunks"));
	HISM_Trunks->SetupAttachment(Root);
	HISM_Trunks->SetMobility(EComponentMobility::Static);
	HISM_Trunks->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HISM_Branches = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("HISM_Branches"));
	HISM_Branches->SetupAttachment(Root);
	HISM_Branches->SetMobility(EComponentMobility::Static);
	HISM_Branches->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SM_TrunkSocketReader = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SM_TrunkSocketReader"));
	SM_TrunkSocketReader->SetupAttachment(Root);
	SM_TrunkSocketReader->SetMobility(EComponentMobility::Static);
	SM_TrunkSocketReader->SetVisibility(false, true);
	SM_TrunkSocketReader->SetHiddenInGame(true);
	SM_TrunkSocketReader->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SM_TrunkSocketReader->SetGenerateOverlapEvents(false);
	SM_TrunkSocketReader->bCastDynamicShadow = false;
	SM_TrunkSocketReader->bCastStaticShadow = false;
}

void AForestChunkModularTrees::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	if (bAutoRebuildInEditor)
	{
		if (UWorld* W = GetWorld())
		{
			if (!W->IsGameWorld())
			{
				RebuildForest();
			}
		}
	}
#endif
}

void AForestChunkModularTrees::BeginPlay()
{
	Super::BeginPlay();

	if (bRebuildOnBeginPlay)
	{
		RebuildForest();
	}
}

#if WITH_EDITOR
void AForestChunkModularTrees::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (bAutoRebuildInEditor)
	{
		if (UWorld* W = GetWorld())
		{
			if (!W->IsGameWorld())
			{
				RebuildForest();
			}
		}
	}
}
#endif

void AForestChunkModularTrees::ResetRuntimeState()
{
	CachedSockets.Reset();
	TrunkSpheres.Reset();
	BranchSpheres.Reset();
	TrunkCellMap.Reset();
	BranchCellMap.Reset();

	CachedTrunkRadius = 0.0f;
	CachedBranchRadius = 0.0f;
	TrunkCellSize = 0.0f;
	BranchCellSize = 0.0f;
}

void AForestChunkModularTrees::ConfigureComponentsForMeshes()
{
	HISM_Trunks->ClearInstances();
	HISM_Branches->ClearInstances();

	HISM_Trunks->SetStaticMesh(TrunkMesh);
	HISM_Branches->SetStaticMesh(BranchMesh);
	SM_TrunkSocketReader->SetStaticMesh(TrunkMesh);

	if (bEnableTrunkCollision)
	{
		HISM_Trunks->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		HISM_Trunks->SetCollisionObjectType(ECC_WorldStatic);
		HISM_Trunks->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		HISM_Trunks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	HISM_Branches->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AForestChunkModularTrees::ClearForest()
{
	ResetRuntimeState();

	if (HISM_Trunks)
	{
		HISM_Trunks->ClearInstances();
	}
	if (HISM_Branches)
	{
		HISM_Branches->ClearInstances();
	}

	UE_LOG(LogTemp, Log, TEXT("ForestChunk: Cleared forest."));
}

bool AForestChunkModularTrees::CacheSocketData()
{
	CachedSockets.Reset();

	if (!TrunkMesh || !SM_TrunkSocketReader)
	{
		UE_LOG(LogTemp, Warning, TEXT("ForestChunk: Missing TrunkMesh or SM_TrunkSocketReader."));
		return false;
	}

	TArray<FName> AllSocketNames = SM_TrunkSocketReader->GetAllSocketNames();
	if (AllSocketNames.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("ForestChunk: Trunk mesh has 0 sockets. Trunks will spawn, branches will not."));
		return true;
	}

	// Bounds for height-normalization
	const FBoxSphereBounds B = TrunkMesh->GetBounds();
	const float MinZ = B.Origin.Z - B.BoxExtent.Z;
	const float MaxZ = B.Origin.Z + B.BoxExtent.Z;
	const float Height = FMath::Max(1.0f, MaxZ - MinZ);

	// Prefer Socket_# if present
	struct FNamedSock
	{
		FName Name;
		int32 Num = INDEX_NONE;
		bool bHasNum = false;
	};

	TArray<FNamedSock> Candidates;
	Candidates.Reserve(AllSocketNames.Num());

	int32 ParsedCount = 0;
	for (const FName& N : AllSocketNames)
	{
		int32 Num = 0;
		const bool bParsed = ParseSocketNumber(N, Num);
		ParsedCount += bParsed ? 1 : 0;
		Candidates.Add({ N, bParsed ? Num : INDEX_NONE, bParsed });
	}

	// If we found any Socket_# then only use those. Otherwise, use all sockets.
	TArray<FNamedSock> FinalList;
	FinalList.Reserve(AllSocketNames.Num());

	if (ParsedCount > 0)
	{
		for (const FNamedSock& S : Candidates)
		{
			if (S.bHasNum)
			{
				FinalList.Add(S);
			}
		}

		FinalList.Sort([](const FNamedSock& A, const FNamedSock& B)
		{
			return A.Num < B.Num;
		});
	}
	else
	{
		FinalList = Candidates;
		// Sort by Z (nice fallback)
		FinalList.Sort([this](const FNamedSock& A, const FNamedSock& B)
		{
			const FVector LA = SM_TrunkSocketReader->GetSocketTransform(A.Name, RTS_Component).GetLocation();
			const FVector LB = SM_TrunkSocketReader->GetSocketTransform(B.Name, RTS_Component).GetLocation();
			return LA.Z < LB.Z;
		});
	}

	for (const FNamedSock& S : FinalList)
	{
		FForestSocketInfo Info;
		Info.SocketName = S.Name;
		Info.SocketLocal = SM_TrunkSocketReader->GetSocketTransform(S.Name, RTS_Component);

		const FVector LocalPos = Info.SocketLocal.GetLocation();
		Info.SocketLocalPos = LocalPos;

		const float Hn = (LocalPos.Z - MinZ) / Height;
		Info.HeightNormalized = FMath::Clamp(Hn, 0.0f, 1.0f);

		CachedSockets.Add(Info);
	}

	UE_LOG(LogTemp, Log, TEXT("ForestChunk: Cached %d sockets (Parsed Socket_#: %d)."), CachedSockets.Num(), ParsedCount);
	return true;
}

FIntPoint AForestChunkModularTrees::ToCell2D(const FVector& WorldPos, float CellSize) const
{
	return FIntPoint(
		FMath::FloorToInt(WorldPos.X / CellSize),
		FMath::FloorToInt(WorldPos.Y / CellSize)
	);
}

void AForestChunkModularTrees::AddSphereToCellMap(const FForestInstanceSphere& Sphere, int32 SphereIndex, float CellSize, TMap<FIntPoint, TArray<int32>>& CellMap)
{
	const FIntPoint C = ToCell2D(Sphere.Center, CellSize);
	CellMap.FindOrAdd(C).Add(SphereIndex);
}

bool AForestChunkModularTrees::HasTrunkOverlap2D(const FVector& CandidateCenter, float CandidateRadius) const
{
	if (!bRejectTrunkOverlap)
	{
		return false;
	}

	const FIntPoint CenterCell = ToCell2D(CandidateCenter, TrunkCellSize);
	const float SearchRadius = CandidateRadius + CachedTrunkRadius;
	const int32 CellRange = FMath::CeilToInt(SearchRadius / TrunkCellSize);

	for (int32 dx = -CellRange; dx <= CellRange; ++dx)
	{
		for (int32 dy = -CellRange; dy <= CellRange; ++dy)
		{
			const FIntPoint Cell = CenterCell + FIntPoint(dx, dy);
			const TArray<int32>* List = TrunkCellMap.Find(Cell);
			if (!List) continue;

			for (int32 SphereIdx : *List)
			{
				if (!TrunkSpheres.IsValidIndex(SphereIdx)) continue;

				const FForestInstanceSphere& Other = TrunkSpheres[SphereIdx];
				const float Dist2D = FVector2D::Distance(
					FVector2D(CandidateCenter.X, CandidateCenter.Y),
					FVector2D(Other.Center.X, Other.Center.Y)
				);

				if (Dist2D < (CandidateRadius + Other.Radius))
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool AForestChunkModularTrees::BranchOverlapsAnyTrunk2D(const FVector& CandidateCenter, float CandidateRadius) const
{
	if (!bBranchCollidesWithTrunks)
	{
		return false;
	}

	const FIntPoint CenterCell = ToCell2D(CandidateCenter, TrunkCellSize);
	const float SearchRadius = CandidateRadius + CachedTrunkRadius;
	const int32 CellRange = FMath::CeilToInt(SearchRadius / TrunkCellSize);

	for (int32 dx = -CellRange; dx <= CellRange; ++dx)
	{
		for (int32 dy = -CellRange; dy <= CellRange; ++dy)
		{
			const FIntPoint Cell = CenterCell + FIntPoint(dx, dy);
			const TArray<int32>* List = TrunkCellMap.Find(Cell);
			if (!List) continue;

			for (int32 SphereIdx : *List)
			{
				if (!TrunkSpheres.IsValidIndex(SphereIdx)) continue;

				const FForestInstanceSphere& Other = TrunkSpheres[SphereIdx];
				const float Dist2D = FVector2D::Distance(
					FVector2D(CandidateCenter.X, CandidateCenter.Y),
					FVector2D(Other.Center.X, Other.Center.Y)
				);

				if (Dist2D < (CandidateRadius + Other.Radius))
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool AForestChunkModularTrees::HasBranchOverlap(const FVector& CandidateCenter, float CandidateRadius) const
{
	if (!bPruneBranchOverlap)
	{
		return false;
	}

	if (bBranchCollidesWithTrunks && BranchOverlapsAnyTrunk2D(CandidateCenter, CandidateRadius))
	{
		return true;
	}

	if (!bBranchCollidesWithBranches)
	{
		return false;
	}

	const FIntPoint CenterCell = ToCell2D(CandidateCenter, BranchCellSize);
	const float SearchRadius = CandidateRadius + CachedBranchRadius;
	const int32 CellRange = FMath::CeilToInt(SearchRadius / BranchCellSize);

	for (int32 dx = -CellRange; dx <= CellRange; ++dx)
	{
		for (int32 dy = -CellRange; dy <= CellRange; ++dy)
		{
			const FIntPoint Cell = CenterCell + FIntPoint(dx, dy);
			const TArray<int32>* List = BranchCellMap.Find(Cell);
			if (!List) continue;

			for (int32 SphereIdx : *List)
			{
				if (!BranchSpheres.IsValidIndex(SphereIdx)) continue;

				const FForestInstanceSphere& Other = BranchSpheres[SphereIdx];
				const float Dist3D = FVector::Distance(CandidateCenter, Other.Center);

				if (Dist3D < (CandidateRadius + Other.Radius))
				{
					return true;
				}
			}
		}
	}

	return false;
}

FForestInstanceSphere AForestChunkModularTrees::MakeTrunkSphere(const FTransform& WorldXform, int32 InstanceIndex) const
{
	FForestInstanceSphere S;
	S.InstanceIndex = InstanceIndex;

	if (!TrunkMesh) return S;

	const FBoxSphereBounds B = TrunkMesh->GetBounds();
	const FVector WorldCenter = WorldXform.TransformPosition(B.Origin);

	const float Scale = WorldXform.GetScale3D().GetAbsMax();

	// Key fix: trunk uses XY footprint by default (height does NOT inflate radius)
	const float XYFootprint = FMath::Max(B.BoxExtent.X, B.BoxExtent.Y);
	const float BaseRadius = (TrunkCollisionRadiusOverride > 0.0f) ? TrunkCollisionRadiusOverride : XYFootprint;

	S.Center = WorldCenter;
	S.Radius = BaseRadius * Scale * TrunkCollisionRadiusScale;
	return S;
}

FForestInstanceSphere AForestChunkModularTrees::MakeBranchSphere(const FTransform& WorldXform, int32 InstanceIndex) const
{
	FForestInstanceSphere S;
	S.InstanceIndex = InstanceIndex;

	if (!BranchMesh) return S;

	const FBoxSphereBounds B = BranchMesh->GetBounds();
	const FVector WorldCenter = WorldXform.TransformPosition(B.Origin);

	const float Scale = WorldXform.GetScale3D().GetAbsMax();

	// Branch uses full bounds sphere by default (reasonable for 3D prune checks)
	const float BaseRadius = (BranchCollisionRadiusOverride > 0.0f) ? BranchCollisionRadiusOverride : B.SphereRadius;

	S.Center = WorldCenter;
	S.Radius = BaseRadius * Scale * BranchCollisionRadiusScale;
	return S;
}

void AForestChunkModularTrees::RebuildForest()
{
	ClearForest();

	if (!TrunkMesh || !BranchMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("ForestChunk: TrunkMesh or BranchMesh is None. Assign both in BP defaults/instance."));
		return;
	}

	ConfigureComponentsForMeshes();

	// Cache sockets (never hard-fail spawning)
	CacheSocketData();

	// Radii + cell sizes
	{
		const FBoxSphereBounds TB = TrunkMesh->GetBounds();
		const FBoxSphereBounds BB = BranchMesh->GetBounds();

		const float TrunkXY = FMath::Max(TB.BoxExtent.X, TB.BoxExtent.Y);
		const float TrunkBase = (TrunkCollisionRadiusOverride > 0.0f) ? TrunkCollisionRadiusOverride : TrunkXY;

		const float BranchBase = (BranchCollisionRadiusOverride > 0.0f) ? BranchCollisionRadiusOverride : BB.SphereRadius;

		const float TrunkMaxScale = FMath::Max(TrunkUniformScaleRange.X, TrunkUniformScaleRange.Y);
		const float BranchMaxScale = FMath::Max(ScaleBottom, ScaleTop) * (1.0f + BranchScaleRandomPct);

		CachedTrunkRadius = TrunkBase * TrunkMaxScale * TrunkCollisionRadiusScale;
		CachedBranchRadius = BranchBase * BranchMaxScale * BranchCollisionRadiusScale;

		TrunkCellSize = (TrunkCellSizeOverride > 0.0f) ? TrunkCellSizeOverride : FMath::Max(100.0f, CachedTrunkRadius * 2.0f);
		BranchCellSize = (BranchCellSizeOverride > 0.0f) ? BranchCellSizeOverride : FMath::Max(100.0f, CachedBranchRadius * 2.0f);
	}

	FRandomStream Rng(Seed);

	const int32 CountX = FMath::Max(1, FMath::FloorToInt(ChunkSize.X / GridSpacing));
	const int32 CountY = FMath::Max(1, FMath::FloorToInt(ChunkSize.Y / GridSpacing));

	const FVector ActorLoc = GetActorLocation();
	const float StartX = bCenterChunkOnActor ? (-ChunkSize.X * 0.5f) : 0.0f;
	const float StartY = bCenterChunkOnActor ? (-ChunkSize.Y * 0.5f) : 0.0f;

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("ForestChunk: No world."));
		return;
	}

	int32 SpawnedTrunks = 0;
	int32 SpawnedBranches = 0;

	for (int32 ix = 0; ix < CountX; ++ix)
	{
		for (int32 iy = 0; iy < CountY; ++iy)
		{
			// Grid + jitter (jitter BEFORE checks)
			const float BaseX = StartX + (ix + 0.5f) * GridSpacing;
			const float BaseY = StartY + (iy + 0.5f) * GridSpacing;

			const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
			const float Rad = Rng.FRandRange(0.0f, JitterRadius);
			const FVector2D Jitter = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Rad;

			const FVector TrunkPos = ActorLoc + FVector(BaseX + Jitter.X, BaseY + Jitter.Y, 0.0f);

			const float Yaw = Rng.FRandRange(-TrunkYawRandomDegrees, TrunkYawRandomDegrees);
			const float TrunkScale = Rng.FRandRange(TrunkUniformScaleRange.X, TrunkUniformScaleRange.Y);

			FTransform TrunkWorld;
			TrunkWorld.SetLocation(TrunkPos);
			TrunkWorld.SetRotation(FQuat(FRotator(0.0f, Yaw, 0.0f)));
			TrunkWorld.SetScale3D(FVector(TrunkScale));

			const FForestInstanceSphere TrunkSphereTemp = MakeTrunkSphere(TrunkWorld, INDEX_NONE);

			if (HasTrunkOverlap2D(TrunkSphereTemp.Center, TrunkSphereTemp.Radius))
			{
				continue; // whole-tree rejection
			}

			// Key fix: world-space add
			const int32 TrunkInstanceIndex = HISM_Trunks->AddInstance(TrunkWorld, /*bWorldSpace=*/true);
			SpawnedTrunks++;

			const FForestInstanceSphere TrunkSphere = MakeTrunkSphere(TrunkWorld, TrunkInstanceIndex);
			const int32 TrunkSphereIndex = TrunkSpheres.Add(TrunkSphere);
			AddSphereToCellMap(TrunkSphere, TrunkSphereIndex, TrunkCellSize, TrunkCellMap);

			if (bDebugDraw)
			{
				DrawDebugSphere(World, TrunkSphere.Center, TrunkSphere.Radius, 12, FColor::Green, false, DebugDrawDuration);
			}

			// If no sockets, skip branches but keep trunks
			if (CachedSockets.Num() == 0)
			{
				continue;
			}

			const int32 BranchCount = Rng.RandRange(MinBranchesPerTree, MaxBranchesPerTree);

			// Pick random socket indices by shuffling (fast enough for ~45 sockets)
			TArray<int32> Indices;
			Indices.Reserve(CachedSockets.Num());
			for (int32 i = 0; i < CachedSockets.Num(); ++i) Indices.Add(i);

			for (int32 i = Indices.Num() - 1; i > 0; --i)
			{
				const int32 j = Rng.RandRange(0, i);
				Indices.Swap(i, j);
			}

			Indices.SetNum(FMath::Min(BranchCount, Indices.Num()));

			for (int32 SocketIdx : Indices)
			{
				const FForestSocketInfo& Sock = CachedSockets[SocketIdx];

				// Start from socket local (trunk space)
				FTransform BranchRel = Sock.SocketLocal;

				// Scale by height (bottom larger, top smaller)
				float Scale = FMath::Lerp(ScaleBottom, ScaleTop, Sock.HeightNormalized);

				// Random scale variation
				const float ScaleJitter = Rng.FRandRange(-BranchScaleRandomPct, BranchScaleRandomPct);
				Scale *= (1.0f + ScaleJitter);

				BranchRel.SetScale3D(BranchRel.GetScale3D() * FVector(Scale));

				// Twist around socket local Z axis
				const float TwistDeg = Rng.FRandRange(-BranchTwistRandomDegrees, BranchTwistRandomDegrees);
				const FVector AxisZ = BranchRel.GetRotation().GetAxisZ();
				const FQuat Twist(AxisZ, FMath::DegreesToRadians(TwistDeg));
				BranchRel.SetRotation((Twist * BranchRel.GetRotation()).GetNormalized());

				// World = local * trunkWorld
				const FTransform BranchWorld = BranchRel * TrunkWorld;

				const FForestInstanceSphere BranchTemp = MakeBranchSphere(BranchWorld, INDEX_NONE);
				if (HasBranchOverlap(BranchTemp.Center, BranchTemp.Radius))
				{
					continue; // prune this branch
				}

				const int32 BranchInstanceIndex = HISM_Branches->AddInstance(BranchWorld, /*bWorldSpace=*/true);
				SpawnedBranches++;

				const FForestInstanceSphere BranchSphere = MakeBranchSphere(BranchWorld, BranchInstanceIndex);
				const int32 BranchSphereIndex = BranchSpheres.Add(BranchSphere);
				AddSphereToCellMap(BranchSphere, BranchSphereIndex, BranchCellSize, BranchCellMap);

				if (bDebugDraw)
				{
					if (bDebugDrawBranchPoints)
					{
						DrawDebugSphere(World, BranchSphere.Center, BranchSphere.Radius, 10, FColor::Cyan, false, DebugDrawDuration);
					}
					if (bDebugDrawSockets)
					{
						const FVector SocketWorldPos = TrunkWorld.TransformPosition(Sock.SocketLocalPos);
						DrawDebugPoint(World, SocketWorldPos, 8.0f, FColor::Yellow, false, DebugDrawDuration);
					}
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("ForestChunk: Rebuild complete. Trunks=%d, Branches=%d, Grid=%dx%d"),
		SpawnedTrunks, SpawnedBranches, CountX, CountY);

	// If you still see nothing, this message is your clue in Output Log.
}
