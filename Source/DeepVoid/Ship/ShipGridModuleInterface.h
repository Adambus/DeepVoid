#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Ship/ShipGridTypes.h"
#include "ShipGridModuleInterface.generated.h"

class AShip;

UINTERFACE(MinimalAPI, Blueprintable)
class UShipGridModuleInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Реализуют все функциональные модули, которые ставятся в сетку корабля (мостик,
 * рампа, оружие, проводка...). Большинство модулей параллельно реализуют ещё и
 * IInteractableInterface (F-взаимодействие) - это два независимых интерфейса на
 * одном акторе, тот же паттерн, что уже использован у турели.
 *
 * GetFootprintCells/IsDirectional/RequiresOpenFacing - BlueprintNativeEvent, чтобы
 * их можно было настраивать/переопределять из Blueprint-подклассов без C++.
 * Остальное - обычные C++ virtual: им нужно хранить состояние (AnchorCell/Facing) и
 * дёргать друг друга, что плохо сочетается с механизмом Execute_/_Implementation.
 */
class DEEPVOID_API IShipGridModuleInterface
{
	GENERATED_BODY()

public:
	// Клетки, занимаемые модулем, заданные ТАК, БУДТО он смотрит на North.
	// (0,0) обязательно должна быть в списке - это якорная клетка.
	// Пример рампы на 2 клетки вперёд: {(0,0), (1,0)}.
	UFUNCTION(BlueprintNativeEvent, Category = "ShipGrid")
	TArray<FIntPoint> GetFootprintCells() const;

	// true для оружия/сопел и т.п. - модулей, которым нужно "смотреть наружу".
	UFUNCTION(BlueprintNativeEvent, Category = "ShipGrid")
	bool IsDirectional() const;

	// true, если для установки обязательно нужна свободная (не занятая корпусом)
	// клетка снаружи, в сторону Facing. Если true, а такой стороны нет - размещение
	// отклоняется (см. UShipGridComponent::CanPlaceModule).
	UFUNCTION(BlueprintNativeEvent, Category = "ShipGrid")
	bool RequiresOpenFacing() const;

	// Вызывается сеткой ПОСЛЕ успешной проверки и присвоения клеток - модуль должен
	// сохранить AnchorCell/Facing у себя и физически встать на место (позиция +
	// поворот относительно корабля). Именно здесь, а не в конструкторе/BeginPlay -
	// до этого вызова модуль не знает, куда он попал.
	virtual void OnPlacedInGrid(AShip* OwningShip, FIntPoint AnchorCell, EGridDirection Facing) = 0;

	// Вызывается перед тем, как сетка уберёт модуль из клеток (снятие игроком или
	// отвал при потере связности) - до Destroy(). По умолчанию ничего не делает;
	// переопределяйте, если модулю нужно что-то сделать заранее (например, высадить
	// пилота из турели).
	virtual void OnRemovedFromGrid() {}

	virtual FIntPoint GetAnchorCell() const = 0;
	virtual EGridDirection GetCurrentFacing() const = 0;

	// Разрешено ли подходить/взаимодействовать с модулем со стороны ApproachDir
	// (направление ОТ модуля К игроку, в координатах корабля - см.
	// ShipGrid::GetApproachDirection). Дефолт: запрещена только сторона Facing у
	// направленных модулей - т.е. нельзя использовать пушку, стоя прямо перед стволом.
	// Дефолт-заглушка "всё разрешено" - у чистого интерфейса нет доступа к полям
	// Facing/Directional конкретного модуля. Реальную проверку (запрет стороны
	// Facing для направленных модулей) переопределяют AShipModuleBase и
	// AShipModule_Weapon, у которых эти поля уже есть.
	virtual bool IsApproachDirectionAllowed(EGridDirection ApproachDir) const
	{
		return true;
	}
};
