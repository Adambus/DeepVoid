#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractionComponent.generated.h"

class UInputAction;
class UInputComponent;
struct FInputActionValue;

/**
 * Добавьте этот компонент к ВАШЕМУ уже существующему Blueprint-персонажу
 * (Details -> Add -> Interaction Component). Ничего в самом персонаже менять не нужно -
 * компонент сам находит InputComponent владельца и добавляет туда кнопку F.
 *
 * Логика: трейс вперёд каждый тик, если объект впереди реализует IInteractableInterface -
 * показывает подсказку (GetCurrentInteractionPrompt) и по F вызывает OnInteract на сервере.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class DEEPVOID_API UInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractionComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Назначьте в деталях компонента ассет Input Action для клавиши F (Value Type: Digital/bool).
	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Interaction")
	float InteractionRange = 250.f;

	UPROPERTY(Transient)
	TScriptInterface<class IInteractableInterface> CurrentFocus;

	// Отслеживаем именно ПЕРЕХОД в состояние "локально управляем" (false -> true), а не сам факт.
	// При каждом таком переходе (первая посадка, ЛЮБАЯ повторная посадка после Possess/UnPossess)
	// принудительно снимаем все старые Enhanced Input привязки этого компонента и ставим заново -
	// так надёжнее, чем полагаться на то, что объект InputComponent не поменялся.
	bool bWasLocallyControlled = false;

	void TryBindInput();
	void UpdateFocus();
	void HandleInteractPressed(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerDoInteract(AActor* TargetActor);

public:
	UFUNCTION(BlueprintPure, Category = "Interaction")
	FText GetCurrentInteractionPrompt() const;
};
