#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Ship/ShipGridTypes.h"
#include "ShipGridComponent.generated.h"

class AShip;
class AShipBlockBase;

/**
 * Держит сетку клеток корабля (структурные блоки + модули) и всю логику
 * размещения/снятия/связности. Живёт на AShip как обычный ActorComponent - тот же
 * паттерн, что и UInteractionComponent на персонаже.
 *
 * Сетка НЕ реплицируется как структура данных (TMap с указателями на акторы плохо
 * реплицируется "из коробки" без ручного FastArraySerializer). Она существует
 * только на сервере как авторитетная книга учёта, а видимость на клиентах
 * достигается штатной репликацией самих акторов блоков/модулей (они обычные
 * реплицируемые AActor). Если позже понадобится клиенту ЗАПРАШИВАТЬ содержимое
 * произвольной клетки (например, для UI строительства) - добавляйте отдельный
 * Server RPC-запрос, не пытайтесь реплицировать Grid целиком.
 */
UCLASS(ClassGroup = (ShipGrid), meta = (BlueprintSpawnableComponent))
class DEEPVOID_API UShipGridComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShipGridComponent();

protected:
	// Размер одной клетки в мировых юнитах (uu). Подберите под размер ваших мешей блоков.
	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	float CellSize = 200.f;

	// Server-only книга учёта - см. комментарий у класса.
	TMap<FIntPoint, FShipCell> Grid;

	AShip* GetOwnerShip() const;

	// Общая часть PlaceBlock/PlaceExistingBlock - прикрепляет уже существующий актор
	// блока к клетке, переводит в состояние Installed, регистрирует в Grid. Вызывающий
	// код обязан САМ до этого проверить CanPlaceBlock.
	void RegisterBlockInGrid(AShipBlockBase* Block, FIntPoint Cell);

public:
	// --- Координаты ---
	UFUNCTION(BlueprintPure, Category = "ShipGrid")
	FIntPoint WorldToGridCell(const FVector& WorldLocation) const;

	// Центр клетки в ЛОКАЛЬНЫХ координатах корабля (для SetActorRelativeLocation).
	UFUNCTION(BlueprintPure, Category = "ShipGrid")
	FVector GridCellToLocalOffset(FIntPoint Cell) const;

	float GetCellSize() const { return CellSize; }

	// --- Запросы ---
	UFUNCTION(BlueprintPure, Category = "ShipGrid")
	bool IsStructuralCell(FIntPoint Cell) const;

	UFUNCTION(BlueprintPure, Category = "ShipGrid")
	TArray<FIntPoint> GetNeighborCells(FIntPoint Cell) const;

	// По каким направлениям от footprint'а (уже повёрнутого под Facing) НЕТ
	// структурного блока сразу за границей - то есть куда "смотрит" открытое
	// пространство. Используется для авто-разворота направленных модулей; может
	// пригодиться и UI (подсветка доступных для установки/поворота сторон).
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	TArray<EGridDirection> GetOpenDirectionsForFootprint(FIntPoint AnchorCell, EGridDirection Facing, const TArray<FIntPoint>& UnrotatedFootprint) const;

	// --- Структурные блоки ---
	bool CanPlaceBlock(FIntPoint Cell, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	AShipBlockBase* PlaceBlock(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass);

	// То же самое, что PlaceBlock, но берёт УЖЕ СУЩЕСТВУЮЩИЙ актор блока (например, тот,
	// что игрок держит в руках), а не спавнит новый - используется системой подбора/
	// установки (см. UBuildComponent). Возвращает false, если клетка недопустима
	// (см. CanPlaceBlock) - в этом случае актор НЕ трогается, вызывающий код должен сам
	// решить, что с ним делать (например, вернуть обратно в руки).
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	bool PlaceExistingBlock(FIntPoint Cell, AShipBlockBase* ExistingBlock);

	// Снять/сломать блок. Отказывает, если в клетке ещё стоит модуль (сначала
	// снимите модуль) - структурный блок и модуль на одной клетке зависимы.
	// Сама по себе НЕ вызывает RecalculateConnectivity за вас в бою -
	// см. комментарий у RecalculateConnectivity.
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	bool RemoveBlock(FIntPoint Cell);

	// Снимает блок из клетки БЕЗ уничтожения - отвязывает от корабля и возвращает
	// указатель на тот же самый актор, чтобы вызывающий код (UBuildComponent) мог
	// отдать его игроку в руки. В отличие от RemoveBlock ничего не разрушает.
	// Как и RemoveBlock, отказывает (возвращает nullptr), если на клетке ещё есть модуль.
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	AShipBlockBase* DetachBlockForPickup(FIntPoint Cell);

	// --- Модули ---
	// RequestedFacing используется только для directional-модулей и только как
	// ПРЕДПОЧТЕНИЕ - если оно упирается в корпус, метод сам подбирает первую
	// рабочую сторону (см. .cpp). OutResolvedFacing - итоговая ориентация.
	bool CanPlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass, EGridDirection& OutResolvedFacing, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	AActor* PlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass);

	// AnchorCell - обязательно якорная клетка модуля (не любая клетка его footprint'а).
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	bool RemoveModule(FIntPoint AnchorCell);

	// --- Связность ---
	// BFS от всех клеток с bIsCore=true; всё, что не достижимо - отваливается
	// (снимается и уничтожается, включая модули на этих клетках). MVP: обломки
	// просто исчезают, не превращаются в отдельный летающий объект - это
	// сознательное упрощение, точка расширения ниже (OnCellsDetached).
	// Зовите после КАЖДОГО RemoveBlock и после разрушения блока боем
	// (AShipBlockBase::ApplyDamage вернул true).
	UFUNCTION(BlueprintCallable, Category = "ShipGrid")
	void RecalculateConnectivity();

	// Точка расширения для VFX/звука/спавна обломков - BlueprintImplementableEvent,
	// чтобы повесить партиклы из BP без правки C++.
	UFUNCTION(BlueprintImplementableEvent, Category = "ShipGrid")
	void OnCellsDetached(const TArray<FIntPoint>& DetachedCells);
};
