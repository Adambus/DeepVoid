#include "Ship/ShipGridComponent.h"
#include "Ship/Ship.h"
#include "Ship/ShipBlockBase.h"
#include "Ship/ShipGridModuleInterface.h"

UShipGridComponent::UShipGridComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// bReplicates остаётся false (дефолт ActorComponent) - сама сетка не реплицируется,
	// см. комментарий в .h.
}

AShip* UShipGridComponent::GetOwnerShip() const
{
	return Cast<AShip>(GetOwner());
}

// ---------------------------------------------------------------------------
// Координаты
// ---------------------------------------------------------------------------

FIntPoint UShipGridComponent::WorldToGridCell(const FVector& WorldLocation) const
{
	const AShip* Ship = GetOwnerShip();
	if (!Ship) return FIntPoint::ZeroValue;

	const FVector Local = Ship->GetActorTransform().InverseTransformPosition(WorldLocation);
	return FIntPoint(FMath::FloorToInt(Local.X / CellSize), FMath::FloorToInt(Local.Y / CellSize));
}

FVector UShipGridComponent::GridCellToLocalOffset(FIntPoint Cell) const
{
	// +0.5 клетки - геометрический центр ячейки, а не угол.
	return FVector((Cell.X + 0.5f) * CellSize, (Cell.Y + 0.5f) * CellSize, 0.f);
}

// ---------------------------------------------------------------------------
// Запросы
// ---------------------------------------------------------------------------

bool UShipGridComponent::IsStructuralCell(FIntPoint Cell) const
{
	const FShipCell* Found = Grid.Find(Cell);
	return Found && Found->StructuralBlock != nullptr;
}

TArray<FIntPoint> UShipGridComponent::GetNeighborCells(FIntPoint Cell) const
{
	return {
		Cell + ShipGrid::DirectionToOffset(EGridDirection::North),
		Cell + ShipGrid::DirectionToOffset(EGridDirection::East),
		Cell + ShipGrid::DirectionToOffset(EGridDirection::South),
		Cell + ShipGrid::DirectionToOffset(EGridDirection::West)
	};
}

TArray<EGridDirection> UShipGridComponent::GetOpenDirectionsForFootprint(FIntPoint AnchorCell, EGridDirection Facing, const TArray<FIntPoint>& UnrotatedFootprint) const
{
	TArray<FIntPoint> RotatedCells;
	for (const FIntPoint& Offset : UnrotatedFootprint)
	{
		RotatedCells.Add(AnchorCell + ShipGrid::RotateOffset(Offset, Facing));
	}

	TArray<EGridDirection> OpenDirections;
	for (uint8 DirIndex = 0; DirIndex < 4; ++DirIndex)
	{
		const EGridDirection Dir = static_cast<EGridDirection>(DirIndex);
		const FIntPoint Step = ShipGrid::DirectionToOffset(Dir);

		bool bAllEdgesOpen = true;
		bool bHasRelevantEdge = false;

		for (const FIntPoint& FootCell : RotatedCells)
		{
			const FIntPoint NeighborInDir = FootCell + Step;
			// Сосед - тоже часть footprint'а (например, вторая клетка рампы) - это
			// не край, а внутреннее ребро, его не считаем.
			if (RotatedCells.Contains(NeighborInDir))
			{
				continue;
			}

			bHasRelevantEdge = true;
			if (IsStructuralCell(NeighborInDir))
			{
				bAllEdgesOpen = false;
				break;
			}
		}

		if (bHasRelevantEdge && bAllEdgesOpen)
		{
			OpenDirections.Add(Dir);
		}
	}

	return OpenDirections;
}

// ---------------------------------------------------------------------------
// Структурные блоки
// ---------------------------------------------------------------------------

bool UShipGridComponent::CanPlaceBlock(FIntPoint Cell, FText& OutReason) const
{
	if (IsStructuralCell(Cell))
	{
		OutReason = NSLOCTEXT("DeepVoid", "CellOccupied", "В этой клетке уже есть корпус");
		return false;
	}

	// Самый первый блок корабля (пустая сетка) разрешаем без проверки соседства -
	// иначе корабль невозможно было бы построить с нуля.
	if (Grid.Num() == 0)
	{
		return true;
	}

	for (const FIntPoint& Neighbor : GetNeighborCells(Cell))
	{
		if (IsStructuralCell(Neighbor))
		{
			return true;
		}
	}

	OutReason = NSLOCTEXT("DeepVoid", "NotAdjacent", "Блок нужно ставить вплотную к уже установленным");
	return false;
}

void UShipGridComponent::RegisterBlockInGrid(AShipBlockBase* Block, FIntPoint Cell)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Block) return;

	Block->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	Block->SetActorRelativeLocation(GridCellToLocalOffset(Cell));
	Block->SetActorRelativeRotation(FRotator::ZeroRotator);
	Block->InitializeBlock(Ship, Cell);
	Block->SetStateInstalled();

	FShipCell& CellData = Grid.FindOrAdd(Cell);
	CellData.StructuralBlock = Block;
}

AShipBlockBase* UShipGridComponent::PlaceBlock(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority() || !BlockClass)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
				TEXT("[Grid] PlaceBlock: нет Ship/прав сервера/BlockClass пуст"));
		}
		return nullptr;
	}

	FText Reason;
	if (!CanPlaceBlock(Cell, Reason))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
				FString::Printf(TEXT("[Grid] PlaceBlock(%d,%d) отклонён: %s"), Cell.X, Cell.Y, *Reason.ToString()));
		}
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = Ship;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AShipBlockBase* NewBlock = GetWorld()->SpawnActor<AShipBlockBase>(BlockClass, Ship->GetActorTransform(), Params);
	if (!NewBlock) return nullptr;

	RegisterBlockInGrid(NewBlock, Cell);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("[Grid] Block установлен в (%d,%d)"), Cell.X, Cell.Y));
	}

	return NewBlock;
}

bool UShipGridComponent::PlaceExistingBlock(FIntPoint Cell, AShipBlockBase* ExistingBlock)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority() || !ExistingBlock)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
				TEXT("[Grid] PlaceExistingBlock: нет Ship/прав сервера/блок пуст"));
		}
		return false;
	}

	FText Reason;
	if (!CanPlaceBlock(Cell, Reason))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
				FString::Printf(TEXT("[Grid] PlaceExistingBlock(%d,%d) отклонён: %s"), Cell.X, Cell.Y, *Reason.ToString()));
		}
		return false;
	}

	RegisterBlockInGrid(ExistingBlock, Cell);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("[Grid] Существующий блок установлен в (%d,%d)"), Cell.X, Cell.Y));
	}

	return true;
}

bool UShipGridComponent::RemoveBlock(FIntPoint Cell)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority()) return false;

	FShipCell* CellData = Grid.Find(Cell);
	if (!CellData || !CellData->StructuralBlock) return false;

	// Нельзя снять корпус, пока в клетке ещё стоит модуль - см. правило "структурный
	// блок + функциональный модуль на клетку" из ГДД, модуль держится именно в корпусе.
	if (CellData->Module != nullptr) return false;

	CellData->StructuralBlock->Destroy();
	CellData->StructuralBlock = nullptr;

	if (CellData->IsEmpty())
	{
		Grid.Remove(Cell);
	}

	return true;
}

AShipBlockBase* UShipGridComponent::DetachBlockForPickup(FIntPoint Cell)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority()) return nullptr;

	FShipCell* CellData = Grid.Find(Cell);
	if (!CellData || !CellData->StructuralBlock) return nullptr;

	// Та же зависимость, что и в RemoveBlock - нельзя снять корпус, пока сверху стоит модуль.
	if (CellData->Module != nullptr) return nullptr;

	AShipBlockBase* Block = CellData->StructuralBlock;
	Block->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Block->ClearShipOwnership();

	CellData->StructuralBlock = nullptr;
	if (CellData->IsEmpty())
	{
		Grid.Remove(Cell);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("[Grid] Блок (%d,%d) снят с корабля (не уничтожен)"), Cell.X, Cell.Y));
	}

	return Block;
}

// ---------------------------------------------------------------------------
// Модули
// ---------------------------------------------------------------------------

bool UShipGridComponent::CanPlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass, EGridDirection& OutResolvedFacing, FText& OutReason) const
{
	if (!ModuleClass)
	{
		OutReason = NSLOCTEXT("DeepVoid", "NoModuleClass", "Класс модуля не задан");
		return false;
	}

	AActor* CDO = ModuleClass.GetDefaultObject();
	if (!CDO || !CDO->GetClass()->ImplementsInterface(UShipGridModuleInterface::StaticClass()))
	{
		OutReason = NSLOCTEXT("DeepVoid", "NotAGridModule", "Класс не реализует IShipGridModuleInterface");
		return false;
	}

	const TArray<FIntPoint> Footprint = IShipGridModuleInterface::Execute_GetFootprintCells(CDO);
	const bool bDirectional = IShipGridModuleInterface::Execute_IsDirectional(CDO);
	const bool bRequiresOpen = IShipGridModuleInterface::Execute_RequiresOpenFacing(CDO);

	auto TestFacing = [this, AnchorCell, &Footprint, bRequiresOpen](EGridDirection Facing, FText& Reason) -> bool
	{
		for (const FIntPoint& Offset : Footprint)
		{
			const FIntPoint Cell = AnchorCell + ShipGrid::RotateOffset(Offset, Facing);
			const FShipCell* CellData = Grid.Find(Cell);
			if (!CellData || !CellData->StructuralBlock)
			{
				Reason = NSLOCTEXT("DeepVoid", "NoHull", "Под модулем должен быть корпус во всех занимаемых клетках");
				return false;
			}
			if (CellData->Module != nullptr)
			{
				Reason = NSLOCTEXT("DeepVoid", "CellHasModule", "В этой клетке уже есть модуль");
				return false;
			}
		}

		if (bRequiresOpen)
		{
			const TArray<EGridDirection> Open = GetOpenDirectionsForFootprint(AnchorCell, Facing, Footprint);
			if (!Open.Contains(Facing))
			{
				Reason = NSLOCTEXT("DeepVoid", "NoOpenFacing", "Модуль должен смотреть в открытую сторону, не внутрь корпуса");
				return false;
			}
		}
		return true;
	};

	if (!bDirectional)
	{
		if (!TestFacing(RequestedFacing, OutReason)) return false;
		OutResolvedFacing = RequestedFacing;
		return true;
	}

	// Направленный модуль: сначала пробуем то, что предпочёл игрок (клавиша
	// поворота перед установкой - важно для угловых клеток, где открыты сразу
	// несколько сторон). Если она упирается в корпус - перебираем остальные и
	// берём первую рабочую. Это и есть "сам разворачивается наружу" из ТЗ.
	if (TestFacing(RequestedFacing, OutReason))
	{
		OutResolvedFacing = RequestedFacing;
		return true;
	}

	for (uint8 i = 0; i < 4; ++i)
	{
		const EGridDirection Candidate = static_cast<EGridDirection>(i);
		if (Candidate == RequestedFacing) continue;

		FText Ignored;
		if (TestFacing(Candidate, Ignored))
		{
			OutResolvedFacing = Candidate;
			return true;
		}
	}

	OutReason = NSLOCTEXT("DeepVoid", "NoValidFacing", "Нет ни одной допустимой ориентации для установки этого модуля здесь");
	return false;
}

AActor* UShipGridComponent::PlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority()) return nullptr;

	EGridDirection ResolvedFacing;
	FText Reason;
	if (!CanPlaceModule(AnchorCell, RequestedFacing, ModuleClass, ResolvedFacing, Reason)) return nullptr;

	FActorSpawnParameters Params;
	Params.Owner = Ship;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewModule = GetWorld()->SpawnActor<AActor>(ModuleClass, Ship->GetActorTransform(), Params);
	if (!NewModule) return nullptr;

	IShipGridModuleInterface* Iface = Cast<IShipGridModuleInterface>(NewModule);
	check(Iface); // CanPlaceModule уже проверил ImplementsInterface на CDO этого же класса

	Iface->OnPlacedInGrid(Ship, AnchorCell, ResolvedFacing);

	const TArray<FIntPoint> Footprint = IShipGridModuleInterface::Execute_GetFootprintCells(NewModule);
	for (const FIntPoint& Offset : Footprint)
	{
		const FIntPoint Cell = AnchorCell + ShipGrid::RotateOffset(Offset, ResolvedFacing);
		FShipCell& CellData = Grid.FindOrAdd(Cell);
		CellData.Module = NewModule;
		CellData.bIsModuleAnchor = (Cell == AnchorCell);
	}

	return NewModule;
}

bool UShipGridComponent::RemoveModule(FIntPoint AnchorCell)
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority()) return false;

	FShipCell* AnchorData = Grid.Find(AnchorCell);
	if (!AnchorData || !AnchorData->Module || !AnchorData->bIsModuleAnchor) return false;

	AActor* ModuleActor = AnchorData->Module;
	IShipGridModuleInterface* Iface = Cast<IShipGridModuleInterface>(ModuleActor);
	const TArray<FIntPoint> Footprint = IShipGridModuleInterface::Execute_GetFootprintCells(ModuleActor);
	const EGridDirection Facing = Iface ? Iface->GetCurrentFacing() : EGridDirection::North;

	for (const FIntPoint& Offset : Footprint)
	{
		const FIntPoint Cell = AnchorCell + ShipGrid::RotateOffset(Offset, Facing);
		if (FShipCell* CellData = Grid.Find(Cell))
		{
			CellData->Module = nullptr;
			CellData->bIsModuleAnchor = false;
			if (CellData->IsEmpty())
			{
				Grid.Remove(Cell);
			}
		}
	}

	if (Iface)
	{
		Iface->OnRemovedFromGrid();
	}
	ModuleActor->Destroy();

	return true;
}

// ---------------------------------------------------------------------------
// Связность
// ---------------------------------------------------------------------------

void UShipGridComponent::RecalculateConnectivity()
{
	AShip* Ship = GetOwnerShip();
	if (!Ship || !Ship->HasAuthority()) return;

	TSet<FIntPoint> Reachable;
	TArray<FIntPoint> Frontier;

	for (const TPair<FIntPoint, FShipCell>& Pair : Grid)
	{
		if (Pair.Value.StructuralBlock && Pair.Value.StructuralBlock->IsCore())
		{
			Frontier.Add(Pair.Key);
			Reachable.Add(Pair.Key);
		}
	}

	if (Frontier.Num() == 0)
	{
		// Нет ни одной "ядровой" клетки - считать нечего, ничего не отваливаем.
		// Обычно это ошибка настройки сцены (забыли поставить bIsCore = true хотя бы
		// на одном блоке, как правило - под мостиком).
		return;
	}

	while (Frontier.Num() > 0)
	{
		const FIntPoint Current = Frontier.Pop();
		for (const FIntPoint& Neighbor : GetNeighborCells(Current))
		{
			if (Reachable.Contains(Neighbor)) continue;
			if (!IsStructuralCell(Neighbor)) continue;

			Reachable.Add(Neighbor);
			Frontier.Add(Neighbor);
		}
	}

	TArray<FIntPoint> Detached;
	for (const TPair<FIntPoint, FShipCell>& Pair : Grid)
	{
		if (Pair.Value.StructuralBlock && !Reachable.Contains(Pair.Key))
		{
			Detached.Add(Pair.Key);
		}
	}

	if (Detached.Num() == 0) return;

	// Сначала снимаем модули с отваливающихся клеток - RemoveModule требует
	// именно якорную клетку, поэтому собираем уникальные модули заранее.
	TSet<AActor*> ModulesToRemove;
	for (const FIntPoint& Cell : Detached)
	{
		if (const FShipCell* CellData = Grid.Find(Cell))
		{
			if (CellData->Module)
			{
				ModulesToRemove.Add(CellData->Module);
			}
		}
	}
	for (AActor* ModuleActor : ModulesToRemove)
	{
		if (IShipGridModuleInterface* Iface = Cast<IShipGridModuleInterface>(ModuleActor))
		{
			RemoveModule(Iface->GetAnchorCell());
		}
	}

	// MVP: отвалившиеся блоки просто уничтожаются. Если позже понадобится, чтобы
	// они физически откалывались отдельным летающим обломком - здесь нужно вместо
	// Destroy() заспавнить отдельный "wreckage"-актор с симулируемой физикой и
	// перенести на него меши блоков; сама проверка связности (BFS выше) не изменится.
	for (const FIntPoint& Cell : Detached)
	{
		if (FShipCell* CellData = Grid.Find(Cell))
		{
			if (CellData->StructuralBlock)
			{
				CellData->StructuralBlock->Destroy();
			}
			Grid.Remove(Cell);
		}
	}

	OnCellsDetached(Detached);
}
