#include "PickupItemActor.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"

#include "CPP_TestsCharacter.h"
#include "InventoryComponent.h"
#include "ItemDataAsset.h"

APickupItemActor::APickupItemActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	InteractSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractSphere"));
	InteractSphere->SetupAttachment(Root);
	InteractSphere->SetSphereRadius(40.f);

	InteractSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void APickupItemActor::Interact_Implementation(AActor* Interactor)
{
	if (!ItemData || Quantity <= 0) return;

	ACPP_TestsCharacter* Player = Cast<ACPP_TestsCharacter>(Interactor);
	if (!Player) return;

	UInventoryComponent* Inv = Player->FindComponentByClass<UInventoryComponent>();
	if (!Inv) return;

	const bool bAdded = Inv->AddItem(ItemData, Quantity, PickupRarity);

	if (GEngine)
	{
		const FString Msg = FString::Printf(TEXT("Picked up: %s x%d"),
			*GetNameSafe(ItemData),
			Quantity);
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Green, Msg);
	}

	if (bAdded && bDestroyOnPickup)
	{
		Destroy();
	}
}
