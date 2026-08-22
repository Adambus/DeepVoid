#include "Ship/ShipModule_Docking.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AShipModule_Docking::AShipModule_Docking()
{
	PrimaryActorTick.bCanEverTick = true;

	// Пример footprint > 1 клетки из ГДД: якорь (0,0) + одна клетка "перед собой"
	// (в North-ориентированных координатах, до поворота под фактический Facing).
	FootprintCells = { FIntPoint(0, 0), FIntPoint(1, 0) };
	bIsDirectional = true;
	bRequiresOpenFacing = true; // рампа обязана смотреть в открытую сторону корпуса

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	// Дочерний компонент к ModuleRoot (из AShipModuleBase), НЕ root - его Transform
	// свободно виден и редактируется в Details с мгновенным превью во вьюпорте.
	FrameMesh->SetupAttachment(RootComponent);
	FrameMesh->SetMobility(EComponentMobility::Movable);
	FrameMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FrameMesh->SetCollisionProfileName(TEXT("BlockAll"));

	DoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorMesh"));
	DoorMesh->SetupAttachment(FrameMesh);
	DoorMesh->SetMobility(EComponentMobility::Movable);
	DoorMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorMesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void AShipModule_Docking::BeginPlay()
{
	Super::BeginPlay();
	CurrentRampPitch = 0.f;
}

void AShipModule_Docking::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipModule_Docking, bIsOpen);
}

void AShipModule_Docking::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float TargetAngle = bIsOpen ? OpenAnglePitch : 0.f;
	if (!FMath::IsNearlyEqual(CurrentRampPitch, TargetAngle, 0.1f))
	{
		CurrentRampPitch = FMath::FixedTurn(CurrentRampPitch, TargetAngle, OpenSpeedDegPerSec * DeltaSeconds);

		const float Applied = bInvertDirection ? -CurrentRampPitch : CurrentRampPitch;

		FRotator NewRotation = FRotator::ZeroRotator;
		switch (RotationAxis)
		{
		case ERampRotationAxis::Pitch: NewRotation.Pitch = Applied; break;
		case ERampRotationAxis::Roll:  NewRotation.Roll = Applied;  break;
		case ERampRotationAxis::Yaw:   NewRotation.Yaw = Applied;   break;
		}

		DoorMesh->SetRelativeRotation(NewRotation);
	}
}

void AShipModule_Docking::OnRep_IsOpen()
{
	// Ничего дополнительно делать не нужно - Tick сам плавно доедет до нужного угла
	// на всех клиентах, как только реплицируется bIsOpen.
}

FText AShipModule_Docking::GetInteractionPrompt_Implementation() const
{
	return bIsOpen
		? NSLOCTEXT("DeepVoid", "DockingClose", "Поднять рампу")
		: NSLOCTEXT("DeepVoid", "DockingOpen", "Опустить рампу");
}

bool AShipModule_Docking::CanInteract_Implementation(AActor* InteractingActor) const
{
	return true;
}

void AShipModule_Docking::OnInteract_Implementation(AActor* InteractingActor)
{
	if (!HasAuthority()) return;

	bIsOpen = !bIsOpen;
	OnRep_IsOpen();
}
