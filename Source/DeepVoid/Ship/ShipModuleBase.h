#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableInterface.h"
#include "Ship/ShipGridModuleInterface.h"
#include "Ship/ShipGridTypes.h"
#include "ShipModuleBase.generated.h"

class AShip;

/**
 * Общая база для функциональных AActor-модулей сетки (мостик, рампа, проводка...).
 * Оружие (AShipModule_Weapon) - APawn, наследоваться отсюда не может (одиночное
 * наследование в C++), поэтому реализует IShipGridModuleInterface самостоятельно,
 * дублируя несколько полей ниже - см. комментарий в ShipModule_Weapon.h.
 */
UCLASS(Abstract)
class DEEPVOID_API AShipModuleBase : public AActor, public IInteractableInterface, public IShipGridModuleInterface
{
	GENERATED_BODY()

public:
	AShipModuleBase();

protected:
	// Пустой корень - именно ЕГО ставит код сетки ровно в центр клетки (см.
	// OnPlacedInGrid). Конкретные модули (Bridge/Docking) крепят СВОЙ видимый меш
	// как дочерний компонент к этому Root, а не делают его сами root'ом - тогда
	// Transform меша (Location/Rotation/Scale) остаётся свободно редактируемым в
	// Details с мгновенным превью во вьюпорте, как уже сделано у турели
	// (TurretRoot + дочерний Mesh).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ShipGrid")
	TObjectPtr<USceneComponent> ModuleRoot;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// Форма модуля, заданная как для Facing = North. Задаётся один раз на классе
	// (C++ конструктор конкретного модуля или EditDefaultsOnly в BP), не меняется
	// на отдельных экземплярах на сцене.
	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	TArray<FIntPoint> FootprintCells = { FIntPoint(0, 0) };

	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	bool bIsDirectional = false;

	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	bool bRequiresOpenFacing = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ShipGrid")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ShipGrid")
	EGridDirection CurrentFacing = EGridDirection::North;

	UPROPERTY(BlueprintReadOnly, Category = "ShipGrid")
	TObjectPtr<AShip> OwningShip;

public:
	// --- IShipGridModuleInterface ---
	virtual TArray<FIntPoint> GetFootprintCells_Implementation() const override { return FootprintCells; }
	virtual bool IsDirectional_Implementation() const override { return bIsDirectional; }
	virtual bool RequiresOpenFacing_Implementation() const override { return bRequiresOpenFacing; }
	virtual void OnPlacedInGrid(AShip* InOwningShip, FIntPoint InAnchorCell, EGridDirection InFacing) override;
	virtual void OnRemovedFromGrid() override;
	virtual FIntPoint GetAnchorCell() const override { return AnchorCell; }
	virtual EGridDirection GetCurrentFacing() const override { return CurrentFacing; }
	virtual bool IsApproachDirectionAllowed(EGridDirection ApproachDir) const override;

	// --- IInteractableInterface ---
	// Базовая реализация - учитывает только сторону подхода (см. IsApproachDirectionAllowed).
	// Конкретный модуль (Bridge/Docking) может переопределить, если нужна доп. логика,
	// как раньше - см. пример в ShipModule_Bridge.
	virtual bool CanInteract_Implementation(AActor* InteractingActor) const override;
};
