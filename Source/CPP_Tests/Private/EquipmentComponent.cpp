#include "EquipmentComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

UEquipmentComponent::UEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	// Create lazily on demand
}

UStaticMeshComponent* UEquipmentComponent::CreateOrGetMeshComp(TObjectPtr<UStaticMeshComponent>& Comp, FName Name)
{
	if (Comp)
	{
		return Comp.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(Owner, Name);
	if (!NewComp)
	{
		return nullptr;
	}

	NewComp->RegisterComponent();
	NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewComp->SetGenerateOverlapEvents(false);
	NewComp->SetCastShadow(true);

	Comp = NewComp;
	return NewComp;
}

bool UEquipmentComponent::EquipMainHandStaticMesh(USkeletalMeshComponent* TargetSkelMesh, UStaticMesh* Mesh, FName SocketName)
{
	if (!TargetSkelMesh || !Mesh)
	{
		return false;
	}

	UStaticMeshComponent* Comp = CreateOrGetMeshComp(MainHandMeshComp, TEXT("MainHandMeshComp"));
	if (!Comp)
	{
		return false;
	}

	Comp->SetStaticMesh(Mesh);
	Comp->AttachToComponent(TargetSkelMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	return true;
}

bool UEquipmentComponent::EquipOffHandStaticMesh(USkeletalMeshComponent* TargetSkelMesh, UStaticMesh* Mesh, FName SocketName)
{
	if (!TargetSkelMesh || !Mesh)
	{
		return false;
	}

	UStaticMeshComponent* Comp = CreateOrGetMeshComp(OffHandMeshComp, TEXT("OffHandMeshComp"));
	if (!Comp)
	{
		return false;
	}

	Comp->SetStaticMesh(Mesh);
	Comp->AttachToComponent(TargetSkelMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	return true;
}

void UEquipmentComponent::UnequipMainHand()
{
	if (MainHandMeshComp)
	{
		MainHandMeshComp->SetStaticMesh(nullptr);
		MainHandMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}

void UEquipmentComponent::UnequipOffHand()
{
	if (OffHandMeshComp)
	{
		OffHandMeshComp->SetStaticMesh(nullptr);
		OffHandMeshComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
}
