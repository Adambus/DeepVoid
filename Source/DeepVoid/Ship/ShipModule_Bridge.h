#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipModuleBase.h"
#include "ShipModule_Bridge.generated.h"

class UStaticMeshComponent;

/**
 * Мостик - функциональный модуль сетки (1 клетка, не directional). F рядом с ним
 * пересаживает игрока на OwningShip (см. AShipModuleBase - Owning Ship
 * назначается автоматически сеткой при установке, вручную в Details задавать
 * больше не нужно, в отличие от старой версии этого класса).
 * Повторный F/Esc для выхода обрабатывается самим кораблём (AShip::ExitPilotMode) -
 * этот класс отвечает только за "посадку", не за "высадку".
 */
UCLASS()
class DEEPVOID_API AShipModule_Bridge : public AShipModuleBase
{
	GENERATED_BODY()

public:
	AShipModule_Bridge();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bridge")
	TObjectPtr<UStaticMeshComponent> Mesh;

public:
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual void OnInteract_Implementation(AActor* InteractingActor) override;
};
