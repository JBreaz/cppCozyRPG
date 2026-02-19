#include "NPCHealthBarWidget.h"

#include "Components/ProgressBar.h"

void UNPCHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Start hidden
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::Hidden);

	// Bind "hide when fade finishes"
	if (Anim_FadeOut)
	{
		FWidgetAnimationDynamicEvent Finished;
		Finished.BindDynamic(this, &UNPCHealthBarWidget::HandleFadeOutFinished);
		BindToAnimationFinished(Anim_FadeOut, Finished);
	}
}

void UNPCHealthBarWidget::SetHealthPercent(float Percent)
{
	if (PB_Health)
	{
		PB_Health->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}
}

void UNPCHealthBarWidget::ShowInstant()
{
	SetVisibility(ESlateVisibility::Visible);
	SetRenderOpacity(1.f);

	if (Anim_FadeOut)
	{
		StopAnimation(Anim_FadeOut);
	}
}

void UNPCHealthBarWidget::PlayFadeOut()
{
	if (Anim_FadeOut)
	{
		// Ensure visible before fading
		SetVisibility(ESlateVisibility::Visible);
		PlayAnimation(Anim_FadeOut);
	}
	else
	{
		// Fallback if animation missing
		SetRenderOpacity(0.f);
		SetVisibility(ESlateVisibility::Hidden);
	}
}

void UNPCHealthBarWidget::HandleFadeOutFinished()
{
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::Hidden);
}
