#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Interactable.h"          // <-- IMPORTANT: the interface
#include "InventoryComponent.h"    // EItemRarity
#include "PickupItemActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UItemDataAsset;

UCLASS()
class CPP_TESTS_API APickupItemActor : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APickupItemActor();

	// This overrides the INTERFACE function (name/signature must match your Interactable.h)
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root = nullptr;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> InteractSphere = nullptr;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup")
	TObjectPtr<UItemDataAsset> ItemData = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup", meta=(ClampMin="1"))
	int32 Quantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup")
	EItemRarity PickupRarity = EItemRarity::Garbage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Pickup")
	bool bDestroyOnPickup = true;
};
