#include "Ship/ShipBlockBase.h"
#include "Build/BuildComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AShipBlockBase::AShipBlockBase()
{
	bReplicates = true;

	// Пустой корень - именно ЕГО код сетки ставит ровно в центр клетки. Mesh -
	// дочерний компонент, поэтому его Transform (Location/Rotation/Scale) виден и
	// свободно редактируется в Details с мгновенным превью во вьюпорте (двигайте
	// стрелками гизмо прямо на глаз) - тот же паттерн, что уже работает у турели
	// (TurretRoot + дочерний Mesh). Так вы визуально подгоняете меш под клетку без
	// всякого подбора чисел вслепую через Play.
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	// Тот же паттерн, что и у HullMesh/остальных модулей - обязателен Movable, иначе
	// персонаж не сможет стоять на этом блоке как на движущейся платформе.
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// Явный профиль вместо дефолтного - гарантирует, что персонаж не проходит сквозь
	// блок независимо от того, что могло быть выставлено вручную в Details BP.
	// Это же - состояние по умолчанию для конструктора совпадает с Loose (см. SetStateLoose),
	// поэтому блок, поставленный вручную на уровень для теста, сразу подбираем по F,
	// без дополнительной настройки.
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void AShipBlockBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipBlockBase, Health);
}

void AShipBlockBase::OnRep_Health()
{
	// Точка расширения для VFX/звука повреждения на клиентах - пока не используется.
}

void AShipBlockBase::InitializeBlock(AShip* InOwningShip, FIntPoint InCell)
{
	OwningShip = InOwningShip;
	GridCell = InCell;
	Health = MaxHealth;
}

void AShipBlockBase::ClearShipOwnership()
{
	OwningShip = nullptr;
	GridCell = FIntPoint::ZeroValue;
}

void AShipBlockBase::SetStateInstalled()
{
	SetActorEnableCollision(true);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void AShipBlockBase::SetStateHeld()
{
	// Коллизия выключена целиком - иначе "приклеенный" к персонажу блок будет мешать
	// его же собственному движению/трейсам.
	SetActorEnableCollision(false);
	Mesh->SetSimulatePhysics(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AShipBlockBase::SetStateLoose()
{
	SetActorEnableCollision(true);
	Mesh->SetSimulatePhysics(false); // без физики - см. FindFreeDropLocation в UBuildComponent
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

bool AShipBlockBase::ApplyDamage(float DamageAmount)
{
	if (!HasAuthority()) return false;

	Health = FMath::Max(0.f, Health - DamageAmount);
	return Health <= 0.f;
}

FText AShipBlockBase::GetInteractionPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("DeepVoid", "PickUpBlockPrompt", "Поднять {0}"), GetDisplayNameText());
}

bool AShipBlockBase::CanInteract_Implementation(AActor* InteractingActor) const
{
	// Установленный (часть сетки корабля) блок по F НЕ подбирается - см. комментарий
	// у класса в заголовке. Только "свободные" блоки, и только если руки ещё пустые.
	if (OwningShip != nullptr)
	{
		return false;
	}

	if (InteractingActor)
	{
		if (UBuildComponent* Build = InteractingActor->FindComponentByClass<UBuildComponent>())
		{
			return !Build->IsHoldingBlock();
		}
	}

	return true;
}

void AShipBlockBase::OnInteract_Implementation(AActor* InteractingActor)
{
	if (!HasAuthority() || !InteractingActor) return;

	if (UBuildComponent* Build = InteractingActor->FindComponentByClass<UBuildComponent>())
	{
		Build->PickUpLooseBlock(this);
	}
}
