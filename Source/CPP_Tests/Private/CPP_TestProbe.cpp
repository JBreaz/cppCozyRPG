#include "CPP_TestProbe.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ACPP_TestProbe::ACPP_TestProbe()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	SetRootComponent(MeshComp);

	// Give it a default cube so you see something immediately.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComp->SetStaticMesh(CubeMesh.Object);
	}

	// Optional: make it visible even if placed at origin.
	MeshComp->SetMobility(EComponentMobility::Movable);
}

void ACPP_TestProbe::BeginPlay()
{
	Super::BeginPlay();

	// Print immediately once, then every 1 second.
	PrintWorking();
	GetWorldTimerManager().SetTimer(
		WorkingTimerHandle,
		this,
		&ACPP_TestProbe::PrintWorking,
		1.0f,
		true
	);
}

void ACPP_TestProbe::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Spin around Z (Yaw) at RotationSpeedDegPerSec.
	const float YawDelta = RotationSpeedDegPerSec * DeltaTime;
	AddActorLocalRotation(FRotator(0.0f, YawDelta, 0.0f));
}

void ACPP_TestProbe::PrintWorking()
{
	UE_LOG(LogTemp, Warning, TEXT("Working"));

	if (GEngine)
	{
		// Use a fixed key so it refreshes instead of stacking lines.
		const int32 Key = 1337;
		const float DisplayTime = 1.1f;
		GEngine->AddOnScreenDebugMessage(Key, DisplayTime, FColor::Green, TEXT("Working"));
	}
}
