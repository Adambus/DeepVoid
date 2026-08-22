#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractableInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Реализуют все объекты, с которыми персонаж может взаимодействовать по кнопке F:
 * мостик (сесть за штурвал), стыковочная дверь (открыть/закрыть) и т.д.
 */
class DEEPVOID_API IInteractableInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	FText GetInteractionPrompt() const;

	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	bool CanInteract(AActor* InteractingActor) const;

	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	void OnInteract(AActor* InteractingActor);
};
