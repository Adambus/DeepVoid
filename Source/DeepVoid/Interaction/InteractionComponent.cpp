#include "Interaction/InteractionComponent.h"
#include "Interaction/InteractableInterface.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

UInteractionComponent::UInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!InteractAction && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
			TEXT("[Interaction] Interact Action НЕ назначен в компоненте! Проверьте Details -> Interact Action"));
	}
}

void UInteractionComponent::TryBindInput()
{
	if (!InteractAction) return;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const bool bIsNowLocallyControlled = OwnerPawn && OwnerPawn->IsLocallyControlled();

	// Реагируем только на ПЕРЕХОД false -> true (только что снова стали управляемым игроком).
	// Каждый такой переход - это либо самая первая посадка, либо ЛЮБОЙ возврат управления
	// после выхода из корабля/турели. Именно тут форсируем чистую пересборку привязки.
	if (bIsNowLocallyControlled && !bWasLocallyControlled)
	{
		APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
		if (PC && OwnerPawn->InputComponent)
		{
			if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(OwnerPawn->InputComponent))
			{
				// Снимаем ВСЕ прошлые привязки этого компонента (если были) перед тем, как
				// поставить новую - иначе при повторных посадках могут копиться дубли,
				// либо (как в баге) старая привязка окажется "мёртвой" после Un/Possess.
				EIC->ClearBindingsForObject(this);
				EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &UInteractionComponent::HandleInteractPressed);

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
						TEXT("[Interaction] Клавиша F пересобрана заново (переход в locally controlled)"));
				}
			}
		}
	}

	bWasLocallyControlled = bIsNowLocallyControlled;
}

void UInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Проверяем на каждый тик, а не один раз - актуально при повторной посадке в транспорт:
	// после UnPossess/Possess InputComponent пешки может создаться заново или как минимум
	// на секунду стать недоступным, поэтому просто пробуем привязаться заново, пока не совпадёт.
	TryBindInput();

	UpdateFocus();
}

void UInteractionComponent::UpdateFocus()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	const FVector Start = OwnerPawn->GetActorLocation();
	const FVector End = Start + OwnerPawn->GetActorForwardVector() * InteractionRange;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerPawn);

	FHitResult Hit;
	const bool bHit = GetWorld()->SweepSingleByChannel(
		Hit, Start, End, FQuat::Identity,
		ECC_Visibility, FCollisionShape::MakeSphere(60.f), Params);

	if (bHit && Hit.GetActor() && Hit.GetActor()->Implements<UInteractableInterface>())
	{
		CurrentFocus = TScriptInterface<IInteractableInterface>(Hit.GetActor());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::Cyan,
				FString::Printf(TEXT("[Interaction] В фокусе: %s"), *Hit.GetActor()->GetName()));
		}
	}
	else
	{
		CurrentFocus = nullptr;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 0.f, FColor::White,
				bHit
					? FString::Printf(TEXT("[Interaction] Трейс попал в %s, но он НЕ интерактивный"), *Hit.GetActor()->GetName())
					: TEXT("[Interaction] Трейс ни во что не попал"));
		}
	}
}

void UInteractionComponent::HandleInteractPressed(const FInputActionValue& Value)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, TEXT("[Interaction] F нажата"));
	}

	if (CurrentFocus)
	{
		if (AActor* TargetActor = Cast<AActor>(CurrentFocus.GetObject()))
		{
			ServerDoInteract(TargetActor);
		}
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("[Interaction] F нажата, но фокуса нет"));
	}
}

void UInteractionComponent::ServerDoInteract_Implementation(AActor* TargetActor)
{
	if (!TargetActor || !GetOwner()) return;

	if (TargetActor->Implements<UInteractableInterface>())
	{
		if (IInteractableInterface::Execute_CanInteract(TargetActor, GetOwner()))
		{
			IInteractableInterface::Execute_OnInteract(TargetActor, GetOwner());
		}
	}
}

FText UInteractionComponent::GetCurrentInteractionPrompt() const
{
	if (CurrentFocus)
	{
		if (AActor* TargetActor = Cast<AActor>(CurrentFocus.GetObject()))
		{
			return IInteractableInterface::Execute_GetInteractionPrompt(TargetActor);
		}
	}
	return FText::GetEmpty();
}
