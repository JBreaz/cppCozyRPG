#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EquipmentComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CPP_TESTS_API UEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEquipmentComponent();

	// Spawned/attached meshes (MVP)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	TObjectPtr<UStaticMeshComponent> MainHandMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Equipment")
	TObjectPtr<UStaticMeshComponent> OffHandMeshComp;

	// Attaches a static mesh to a socket on the given skeletal mesh (defaults to HandSocket)
	UFUNCTION(BlueprintCallable, Category="Equipment")
	bool EquipMainHandStaticMesh(USkeletalMeshComponent* TargetSkelMesh, UStaticMesh* Mesh, FName SocketName = "HandSocket");

	UFUNCTION(BlueprintCallable, Category="Equipment")
	bool EquipOffHandStaticMesh(USkeletalMeshComponent* TargetSkelMesh, UStaticMesh* Mesh, FName SocketName = "HandSocket");

	UFUNCTION(BlueprintCallable, Category="Equipment")
	void UnequipMainHand();

	UFUNCTION(BlueprintCallable, Category="Equipment")
	void UnequipOffHand();

protected:
	virtual void BeginPlay() override;

private:
	UStaticMeshComponent* CreateOrGetMeshComp(TObjectPtr<UStaticMeshComponent>& Comp, FName Name);
};
