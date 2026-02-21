#include "SeasonalStaticMeshActor.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ASeasonalStaticMeshActor::ASeasonalStaticMeshActor()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);
	MeshComponent->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}
}

void ASeasonalStaticMeshActor::BeginPlay()
{
	Super::BeginPlay();

	EnsureDynamicMaterials();
	ApplySeasonToMaterialParameters(CurrentVisualSeason);
}

void ASeasonalStaticMeshActor::ApplySeasonVisual_Implementation(EWorldSeason Season)
{
	TargetSeason = Season;
	CurrentVisualSeason = Season;
	bPendingSwap = false;

	EnsureDynamicMaterials();
	ApplySeasonToMaterialParameters(Season);
}

void ASeasonalStaticMeshActor::SetPendingSeason(EWorldSeason InTargetSeason)
{
	TargetSeason = InTargetSeason;
	bPendingSwap = true;
}

void ASeasonalStaticMeshActor::EnsureDynamicMaterials()
{
	if (!IsValid(MeshComponent))
	{
		return;
	}

	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	if (MaterialCount <= 0)
	{
		DynamicMaterials.Reset();
		return;
	}

	DynamicMaterials.SetNum(MaterialCount);

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		if (IsValid(DynamicMaterials[MaterialIndex]))
		{
			continue;
		}

		UMaterialInterface* BaseMaterial = MeshComponent->GetMaterial(MaterialIndex);
		if (!IsValid(BaseMaterial))
		{
			continue;
		}

		UMaterialInstanceDynamic* DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(MaterialIndex, BaseMaterial);
		DynamicMaterials[MaterialIndex] = DynamicMaterial;
	}
}

void ASeasonalStaticMeshActor::ApplySeasonToMaterialParameters(EWorldSeason Season)
{
	const FLinearColor SeasonColor = GetColorForSeason(Season);
	const float SeasonIndex = static_cast<float>(static_cast<uint8>(Season));

	for (UMaterialInstanceDynamic* DynamicMaterial : DynamicMaterials)
	{
		if (!IsValid(DynamicMaterial))
		{
			continue;
		}

		DynamicMaterial->SetVectorParameterValue(SeasonColorParameterName, SeasonColor);

		if (bWriteSeasonIndexParameter)
		{
			DynamicMaterial->SetScalarParameterValue(SeasonIndexParameterName, SeasonIndex);
		}
	}
}

FLinearColor ASeasonalStaticMeshActor::GetColorForSeason(EWorldSeason Season) const
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
