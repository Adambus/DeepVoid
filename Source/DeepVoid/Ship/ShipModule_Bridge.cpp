#include "Ship/ShipModule_Bridge.h"
#include "Ship/Ship.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

AShipModule_Bridge::AShipModule_Bridge()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	// Дочерний компонент к ModuleRoot (из AShipModuleBase), НЕ root - поэтому его
	// Transform свободно виден и редактируется в Details с мгновенным превью во
	// вьюпорте (просто подвигайте меш стрелками на глаз, пока не встанет как нужно).
	Mesh->SetupAttachment(RootComponent);
	// Обязательно, иначе персонаж, физически стоящий на мостике, не сможет "ехать"
	// вместе с кораблём как на движущейся платформе (тот же фикс, что у AShip::HullMesh).
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

FText AShipModule_Bridge::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("DeepVoid", "BridgeEnter", "Занять штурвал");
}

void AShipModule_Bridge::OnInteract_Implementation(AActor* InteractingActor)
{
	if (!OwningShip)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				TEXT("[Bridge] OwningShip пуст - модуль ещё не установлен в сетку корабля"));
		}
		return;
	}

	APawn* InstigatorPawn = Cast<APawn>(InteractingActor);
	if (!InstigatorPawn) return;

	AController* Controller = InstigatorPawn->GetController();
	if (!Controller) return;

	OwningShip->EnterPilotMode(InstigatorPawn, Controller);
}
