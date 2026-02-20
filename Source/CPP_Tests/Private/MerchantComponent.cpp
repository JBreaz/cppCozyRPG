// Fill out your copyright notice in the Description page of Project Settings.


#include "MerchantComponent.h"

// Sets default values for this component's properties
UMerchantComponent::UMerchantComponent()
{
	// Placeholder component: keep non-ticking until behavior is implemented.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void UMerchantComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}


// Called every frame
void UMerchantComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

