#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipModuleBase.h"
#include "ShipModule_Docking.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ERampRotationAxis : uint8
{
	Pitch, // вокруг локальной оси Y - "классический" разводной мост
	Roll,  // вокруг локальной оси X
	Yaw    // вокруг локальной оси Z - как дверь
};

/**
 * Стыковочная рампа - функциональный модуль сетки, занимает 2 клетки подряд
 * (пример "footprint > 1" из ГДД) и directional (должна смотреть наружу корпуса,
 * иначе стыковаться будет некуда - см. bRequiresOpenFacing в конструкторе).
 * F открывает/закрывает с плавной анимацией через Tick, как раньше.
 */
UCLASS()
class DEEPVOID_API AShipModule_Docking : public AShipModuleBase
{
	GENERATED_BODY()

public:
	AShipModule_Docking();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Docking")
	TObjectPtr<UStaticMeshComponent> FrameMesh; // неподвижная рама

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Docking")
	TObjectPtr<UStaticMeshComponent> DoorMesh; // подвижная створка

	UPROPERTY(EditDefaultsOnly, Category = "Docking")
	float OpenAnglePitch = 90.f;

	// Если рампа заваливается не в ту сторону - переключите ось в деталях, код
	// трогать не нужно.
	UPROPERTY(EditDefaultsOnly, Category = "Docking")
	ERampRotationAxis RotationAxis = ERampRotationAxis::Pitch;

	UPROPERTY(EditDefaultsOnly, Category = "Docking")
	bool bInvertDirection = false;

	UPROPERTY(EditDefaultsOnly, Category = "Docking")
	float OpenSpeedDegPerSec = 90.f;

	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, BlueprintReadOnly, Category = "Docking")
	bool bIsOpen = false;

	float CurrentRampPitch = 0.f;

	UFUNCTION()
	void OnRep_IsOpen();

public:
	virtual FText GetInteractionPrompt_Implementation() const override;

	// Рампу, в отличие от оружия, разрешаем открывать/закрывать с ЛЮБОЙ стороны
	// (и снаружи, и изнутри) - поэтому явно переопределяем базовую проверку
	// AShipModuleBase::CanInteract_Implementation (которая для directional-модулей
	// блокирует сторону Facing).
	virtual bool CanInteract_Implementation(AActor* InteractingActor) const override;
	virtual void OnInteract_Implementation(AActor* InteractingActor) override;
};
