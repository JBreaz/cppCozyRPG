#include "PlayerHUD.h"

#include "PlayerHUDWidget.h"
#include "CPP_TestsCharacter.h"
#include "PlayerStatsComponent.h"
#include "StatusEffectComponent.h"

#include "GameFramework/PlayerController.h"

void APlayerHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !HUDWidgetClass) return;

	HUDWidget = CreateWidget<UPlayerHUDWidget>(PC, HUDWidgetClass);
	if (!HUDWidget) return;

	HUDWidget->AddToViewport();

	ACPP_TestsCharacter* Char = Cast<ACPP_TestsCharacter>(PC->GetCharacter());
	if (!Char) return;

	UPlayerStatsComponent* Stats = Char->FindComponentByClass<UPlayerStatsComponent>();
	UStatusEffectComponent* Effects = Char->FindComponentByClass<UStatusEffectComponent>();

	HUDWidget->InitializeFromComponents(Stats, Effects);
}
