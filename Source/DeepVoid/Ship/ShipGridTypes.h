#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipGridTypes.generated.h"

class AShipBlockBase;

/**
 * Направление в системе координат СЕТКИ КОРАБЛЯ (не мировой!). Корабль летает и
 * вращается, но "North" всегда = GetActorForwardVector(), "East" = GetActorRightVector().
 * Ровно 4 значения - сетка прямоугольная, повороты только шагами по 90 градусов
 * (см. ГДД: "объекты можно повернуть перед установкой").
 */
UENUM(BlueprintType)
enum class EGridDirection : uint8
{
	North	UMETA(DisplayName = "North (Forward)"),
	East	UMETA(DisplayName = "East (Right)"),
	South	UMETA(DisplayName = "South (Backward)"),
	West	UMETA(DisplayName = "West (Left)")
};

/**
 * Тип второстепенного элемента клетки (проводка/экраны/...). Structural-блок и
 * Primary-модуль хранятся в FShipCell отдельными полями (без них нельзя ставить
 * остальное), а всё, что попадает в AuxElements, ключуется этим enum - по одному
 * элементу каждого типа на клетку, как требует ГДД. Добавляйте новые значения по
 * мере необходимости, каждое - независимый "слой", не конфликтующий с другими.
 */
UENUM(BlueprintType)
enum class EShipAuxElementType : uint8
{
	Wiring,
	Screen,
	Pipe
};

/** Содержимое одной ячейки сетки корабля. */
USTRUCT()
struct FShipCell
{
	GENERATED_BODY()

	// Структурный блок (корпус) в этой клетке. Модули крепятся "в" корпус, поэтому
	// без StructuralBlock в клетку нельзя ставить ничего остального.
	UPROPERTY()
	TObjectPtr<AShipBlockBase> StructuralBlock = nullptr;

	// Функциональный модуль, занимающий эту клетку. Храним как AActor (не как
	// интерфейс) - модуль может быть и AActor (мостик), и APawn (турель), оба
	// AActor-совместимы. Геймплейное поведение достаём через IShipGridModuleInterface.
	UPROPERTY()
	TObjectPtr<AActor> Module = nullptr;

	// true только в "якорной" клетке модуля - у модулей с footprint больше 1 клетки
	// Module заполнен и в остальных клетках footprint'а тоже (чтобы проверки
	// занятости работали для каждой клетки), но реально владеет и уничтожает
	// модуль код, работающий именно с якорной клеткой.
	UPROPERTY()
	bool bIsModuleAnchor = false;

	UPROPERTY()
	TMap<EShipAuxElementType, TObjectPtr<AActor>> AuxElements;

	bool IsEmpty() const
	{
		return StructuralBlock == nullptr && Module == nullptr && AuxElements.Num() == 0;
	}
};

namespace ShipGrid
{
	/** Направление -> единичное смещение в North-ориентированных координатах. */
	inline FIntPoint DirectionToOffset(EGridDirection Dir)
	{
		switch (Dir)
		{
		case EGridDirection::North: return FIntPoint(1, 0);
		case EGridDirection::East:  return FIntPoint(0, 1);
		case EGridDirection::South: return FIntPoint(-1, 0);
		case EGridDirection::West:  return FIntPoint(0, -1);
		}
		return FIntPoint(0, 0);
	}

	/** Поворот направления на Steps шагов по 90 градусов по часовой стрелке (вид сверху). */
	inline EGridDirection RotateDirection(EGridDirection Dir, uint8 Steps)
	{
		return static_cast<EGridDirection>((static_cast<uint8>(Dir) + (Steps % 4)) % 4);
	}

	inline EGridDirection OppositeDirection(EGridDirection Dir)
	{
		return RotateDirection(Dir, 2);
	}

	/**
	 * Поворачивает локальное смещение (задано так, будто модуль смотрит на North)
	 * на угол, соответствующий Facing. Используется и чтобы посчитать итоговые
	 * занятые клетки footprint'а, и чтобы понять, куда "смотрит" направленный модуль.
	 * Формула поворота на 90° по часовой стрелке (North=+X, East=+Y): (x,y)->(-y,x).
	 * Если после сборки в редакторе поворот окажется зеркальным относительно
	 * конкретного меша - это калибровка на уровне визуала (как было с осью рампы),
	 * не ошибка в этой функции.
	 */
	inline FIntPoint RotateOffset(FIntPoint Offset, EGridDirection Facing)
	{
		FIntPoint Result = Offset;
		const uint8 Steps = static_cast<uint8>(Facing);
		for (uint8 i = 0; i < Steps; ++i)
		{
			Result = FIntPoint(-Result.Y, Result.X);
		}
		return Result;
	}

	/** Yaw корабля-локальной ориентации для данного Facing. North = 0. */
	inline float DirectionToYaw(EGridDirection Dir)
	{
		return 90.f * static_cast<float>(Dir);
	}

	/**
	 * С какой стороны (в координатах корабля) находится OtherWorldLocation
	 * относительно ModuleWorldLocation. Используется для проверки "не подошёл ли
	 * игрок прямо к дулу" - см. IShipGridModuleInterface::IsApproachDirectionAllowed.
	 * ShipActor нужен только для его текущего мирового поворота (UnrotateVector) -
	 * результат стабилен независимо от того, летит корабль или нет.
	 */
	inline EGridDirection GetApproachDirection(const AActor* ShipActor, const FVector& ModuleWorldLocation, const FVector& OtherWorldLocation)
	{
		if (!ShipActor)
		{
			return EGridDirection::North;
		}

		const FVector WorldDelta = OtherWorldLocation - ModuleWorldLocation;
		const FVector LocalDelta = ShipActor->GetActorRotation().UnrotateVector(WorldDelta);

		if (FMath::Abs(LocalDelta.X) >= FMath::Abs(LocalDelta.Y))
		{
			return LocalDelta.X >= 0.f ? EGridDirection::North : EGridDirection::South;
		}
		return LocalDelta.Y >= 0.f ? EGridDirection::East : EGridDirection::West;
	}
}
