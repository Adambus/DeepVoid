#include "Ship/ShipModuleBase.h"
#include "Ship/Ship.h"
#include "Ship/ShipGridComponent.h"
#include "Net/UnrealNetwork.h"

AShipModuleBase::AShipModuleBase()
{
	bReplicates = true;

	ModuleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ModuleRoot"));
	SetRootComponent(ModuleRoot);
	ModuleRoot->SetMobility(EComponentMobility::Movable);
}

void AShipModuleBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipModuleBase, AnchorCell);
	DOREPLIFETIME(AShipModuleBase, CurrentFacing);
}

void AShipModuleBase::OnPlacedInGrid(AShip* InOwningShip, FIntPoint InAnchorCell, EGridDirection InFacing)
{
	OwningShip = InOwningShip;
	AnchorCell = InAnchorCell;
	CurrentFacing = InFacing;

	if (OwningShip)
	{
		AttachToActor(OwningShip, FAttachmentTransformRules::KeepWorldTransform);
		if (UShipGridComponent* Grid = OwningShip->GetGridComponent())
		{
			SetActorRelativeLocation(Grid->GridCellToLocalOffset(AnchorCell));
		}
		SetActorRelativeRotation(FRotator(0.f, ShipGrid::DirectionToYaw(CurrentFacing), 0.f));
	}
}

void AShipModuleBase::OnRemovedFromGrid()
{
	// Точка расширения - переопределяйте в потомке, если при снятии нужно что-то
	// сделать заранее (высадить игрока и т.п.). Сам Destroy() зовёт UShipGridComponent,
	// не сам модуль.
}

bool AShipModuleBase::IsApproachDirectionAllowed(EGridDirection ApproachDir) const
{
	// Направленный модуль запрещает подход именно со стороны, куда он "смотрит" -
	// для оружия это буквально "не подходить и не использовать, стоя перед стволом".
	if (bIsDirectional && ApproachDir == CurrentFacing)
	{
		return false;
	}
	return true;
}

bool AShipModuleBase::CanInteract_Implementation(AActor* InteractingActor) const
{
	if (!OwningShip || !InteractingActor)
	{
		return true;
	}

	const EGridDirection ApproachDir = ShipGrid::GetApproachDirection(OwningShip, GetActorLocation(), InteractingActor->GetActorLocation());
	return IsApproachDirectionAllowed(ApproachDir);
}
