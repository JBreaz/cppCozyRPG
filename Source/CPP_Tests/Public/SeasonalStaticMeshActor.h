#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SeasonTypes.h"
#include "SeasonalVisualInterface.h"
#include "SeasonalStaticMeshActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS()
class CPP_TESTS_API ASeasonalStaticMeshActor : public AActor, public ISeasonalVisualInterface
{
	GENERATED_BODY()

public:
	ASeasonalStaticMeshActor();

	virtual void BeginPlay() override;

	virtual void ApplySeasonVisual_Implementation(EWorldSeason Season) override;

	UFUNCTION(BlueprintCallable, Category = "Season|Visual")
	void SetPendingSeason(EWorldSeason InTargetSeason);

	UFUNCTION(BlueprintPure, Category = "Season|Visual")
	EWorldSeason GetCurrentVisualSeason() const { return CurrentVisualSeason; }

	UFUNCTION(BlueprintPure, Category = "Season|Visual")
	EWorldSeason GetTargetSeason() const { return TargetSeason; }

	UFUNCTION(BlueprintPure, Category = "Season|Visual")
	bool IsPendingSwap() const { return bPendingSwap; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent = nullptr;

protected:
	UPROPERTY(EditAnywhere, Category = "Season|Material")
	FName SeasonColorParameterName = TEXT("SeasonColor");

	UPROPERTY(EditAnywhere, Category = "Season|Material")
	FName SeasonIndexParameterName = TEXT("SeasonIndex");

	UPROPERTY(EditAnywhere, Category = "Season|Material")
	bool bWriteSeasonIndexParameter = true;

	UPROPERTY(EditAnywhere, Category = "Season|Color")
	FLinearColor SpringColor = FLinearColor(0.14f, 0.72f, 0.18f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Color")
	FLinearColor SummerColor = FLinearColor(0.95f, 0.85f, 0.20f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Color")
	FLinearColor FallColor = FLinearColor(0.95f, 0.45f, 0.08f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Season|Color")
	FLinearColor WinterColor = FLinearColor(0.15f, 0.45f, 1.0f, 1.0f);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Season|Runtime")
	EWorldSeason CurrentVisualSeason = EWorldSeason::Spring;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Season|Runtime")
	EWorldSeason TargetSeason = EWorldSeason::Spring;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Season|Runtime")
	bool bPendingSwap = false;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	void EnsureDynamicMaterials();
	void ApplySeasonToMaterialParameters(EWorldSeason Season);
	FLinearColor GetColorForSeason(EWorldSeason Season) const;
};
