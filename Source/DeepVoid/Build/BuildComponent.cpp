#include "Build/BuildComponent.h"
#include "Ship/Ship.h"
#include "Ship/ShipGridComponent.h"
#include "Ship/ShipBlockBase.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

UBuildComponent::UBuildComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UBuildComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!DropAction && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red,
			TEXT("[Build] Drop Action НЕ назначен в компоненте!"));
	}
}

void UBuildComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBuildComponent, HeldBlock);
}

void UBuildComponent::TryBindInput()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const bool bIsNowLocallyControlled = OwnerPawn && OwnerPawn->IsLocallyControlled();

	// Тот же паттерн пересборки на переход false->true, что и в UInteractionComponent -
	// см. подробный комментарий там же для причины.
	if (bIsNowLocallyControlled && !bWasLocallyControlled)
	{
		APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
		if (PC && OwnerPawn->InputComponent)
		{
			if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(OwnerPawn->InputComponent))
			{
				EIC->ClearBindingsForObject(this);

				if (DropAction)
				{
					EIC->BindAction(DropAction, ETriggerEvent::Started, this, &UBuildComponent::Input_Drop);
				}

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[Build] Клавиша броска пересобрана заново"));
				}
			}
		}
	}

	bWasLocallyControlled = bIsNowLocallyControlled;
}

void UBuildComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TryBindInput();
}

void UBuildComponent::Input_Drop(const FInputActionValue& Value)
{
	// Пусто в руках - ничего не делаем, дадим отработать обычному Interact Action
	// (подбор) у UInteractionComponent на той же клавише.
	if (!HeldBlock) return;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return;

	FBuildMenuEntry Entry;
	Entry.Action = EBuildAction::Drop;
	Entry.WorldLocation = OwnerPawn->GetActorLocation() + OwnerPawn->GetActorForwardVector() * 150.f;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, TEXT("[Build] F - бросаю то, что в руках"));
	}

	ExecuteBuildAction(Entry);
}

TArray<FBuildMenuEntry> UBuildComponent::QueryBuildMenu(FVector2D ScreenPosition)
{
	TArray<FBuildMenuEntry> Entries;

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* PC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!PC || !GetWorld())
	{
		return Entries;
	}

	FVector WorldOrigin, WorldDirection;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, WorldOrigin, WorldDirection))
	{
		return Entries;
	}

	const FVector TraceEnd = WorldOrigin + WorldDirection * 100000.f;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerPawn);

	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, WorldOrigin, TraceEnd, ECC_Visibility, Params))
	{
		return Entries;
	}

	// --- "Поднять" - сам объект, в который попали, плюс всё похожее рядом (несколько
	// предметов почти в одной точке - см. обсуждение "селектора" в чате). ---
	TSet<AShipBlockBase*> NearbyBlocks;
	if (AShipBlockBase* HitBlock = Cast<AShipBlockBase>(Hit.GetActor()))
	{
		NearbyBlocks.Add(HitBlock);
	}

	static constexpr float PickRadius = 120.f;
	for (TActorIterator<AShipBlockBase> It(GetWorld()); It; ++It)
	{
		if (*It && FVector::DistSquared(It->GetActorLocation(), Hit.Location) <= FMath::Square(PickRadius))
		{
			NearbyBlocks.Add(*It);
		}
	}

	for (AShipBlockBase* Block : NearbyBlocks)
	{
		if (!Block) continue;

		FBuildMenuEntry Entry;
		Entry.Action = EBuildAction::PickUp;
		Entry.DisplayName = FText::Format(NSLOCTEXT("DeepVoid", "PickUpFmt", "Поднять {0}"), Block->GetDisplayNameText());
		Entry.TargetBlock = Block;
		Entry.WorldLocation = Block->GetActorLocation();
		Entries.Add(Entry);
	}

	// --- "Установить" - только если в руках что-то есть, и попали в грань уже
	// установленного блока (ставим в соседнюю с этой гранью клетку - как в майнкрафте,
	// не требует отдельной коллизии на пустых клетках корпуса). ---
	if (HeldBlock)
	{
		if (AShipBlockBase* HitBlock = Cast<AShipBlockBase>(Hit.GetActor()))
		{
			if (AShip* Ship = HitBlock->OwningShip)
			{
				if (UShipGridComponent* Grid = Ship->GetGridComponent())
				{
					const FVector LocalNormal = Ship->GetActorRotation().UnrotateVector(Hit.ImpactNormal);
					const FIntPoint NeighborOffset(FMath::RoundToInt(LocalNormal.X), FMath::RoundToInt(LocalNormal.Y));
					const FIntPoint CandidateCell = HitBlock->GridCell + NeighborOffset;

					if (!Grid->IsStructuralCell(CandidateCell))
					{
						FBuildMenuEntry Entry;
						Entry.Action = EBuildAction::Install;
						Entry.DisplayName = FText::Format(NSLOCTEXT("DeepVoid", "InstallFmt", "Установить {0}"), HeldBlock->GetDisplayNameText());
						Entry.TargetShip = Ship;
						Entry.TargetCell = CandidateCell;
						Entry.WorldLocation = Ship->GetActorTransform().TransformPosition(Grid->GridCellToLocalOffset(CandidateCell));
						Entries.Add(Entry);
					}
				}
			}
		}

		// "Бросить" - доступно всегда, пока руки заняты, независимо от того, куда
		// именно кликнул игрок (бросаем в точку клика).
		FBuildMenuEntry DropEntry;
		DropEntry.Action = EBuildAction::Drop;
		DropEntry.DisplayName = FText::Format(NSLOCTEXT("DeepVoid", "DropFmt", "Бросить {0}"), HeldBlock->GetDisplayNameText());
		DropEntry.WorldLocation = Hit.Location;
		Entries.Add(DropEntry);
	}

	return Entries;
}

bool UBuildComponent::IsEntryInRange(const FBuildMenuEntry& Entry) const
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return false;

	return FVector::Dist(OwnerPawn->GetActorLocation(), Entry.WorldLocation) <= BuildRange;
}

void UBuildComponent::ExecuteBuildAction(const FBuildMenuEntry& Entry)
{
	if (!IsEntryInRange(Entry))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Orange, TEXT("[Build] Слишком далеко - подойдите ближе"));
		}
		return;
	}

	ServerExecuteBuildAction(Entry);
}

void UBuildComponent::ServerExecuteBuildAction_Implementation(FBuildMenuEntry Entry)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return;

	// Не доверяем WorldLocation от клиента буквально - для PickUp/Install пересчитываем
	// из ТЕКУЩЕГО состояния актуальных объектов на сервере. Для Drop используем точку
	// клика, но дальность всё равно проверяем здесь же, заново, авторитетно.
	FVector VerifiedLocation = Entry.WorldLocation;

	if (Entry.Action == EBuildAction::PickUp && Entry.TargetBlock)
	{
		VerifiedLocation = Entry.TargetBlock->GetActorLocation();
	}
	else if (Entry.Action == EBuildAction::Install && Entry.TargetShip && Entry.TargetShip->GetGridComponent())
	{
		VerifiedLocation = Entry.TargetShip->GetActorTransform().TransformPosition(
			Entry.TargetShip->GetGridComponent()->GridCellToLocalOffset(Entry.TargetCell));
	}

	if (FVector::Dist(OwnerPawn->GetActorLocation(), VerifiedLocation) > BuildRange)
	{
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "TooFar", "Слишком далеко"));
		return;
	}

	switch (Entry.Action)
	{
	case EBuildAction::PickUp:
		DoPickUp(Entry.TargetBlock);
		break;
	case EBuildAction::Install:
		DoInstall(Entry.TargetShip, Entry.TargetCell);
		break;
	case EBuildAction::Drop:
		DoDrop(VerifiedLocation);
		break;
	}
}

void UBuildComponent::DoPickUp(AShipBlockBase* Block)
{
	if (!Block)
	{
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "NoTarget", "Нечего поднимать"));
		return;
	}
	if (HeldBlock)
	{
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "HandsFull", "Руки уже заняты"));
		return;
	}

	if (AShip* Ship = Block->OwningShip)
	{
		if (UShipGridComponent* Grid = Ship->GetGridComponent())
		{
			AShipBlockBase* Detached = Grid->DetachBlockForPickup(Block->GridCell);
			if (!Detached)
			{
				ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "CantDetach", "Нельзя снять - возможно, сверху стоит модуль"));
				return;
			}
			Grid->RecalculateConnectivity();
		}
	}

	AttachHeldBlock(Block);
	ClientBuildActionResult(true, FText::GetEmpty());
}

void UBuildComponent::DoInstall(AShip* TargetShip, FIntPoint Cell)
{
	if (!TargetShip || !HeldBlock)
	{
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "NothingToInstall", "Нечего устанавливать"));
		return;
	}

	UShipGridComponent* Grid = TargetShip->GetGridComponent();
	FText Reason;
	if (!Grid || !Grid->CanPlaceBlock(Cell, Reason))
	{
		ClientBuildActionResult(false, Reason.IsEmpty() ? NSLOCTEXT("DeepVoid", "InstallFailed", "Не удалось установить") : Reason);
		return;
	}

	AShipBlockBase* BlockToInstall = HeldBlock;
	DetachHeldBlock(); // сначала освобождаем руки - иначе AttachToActor внутри сетки конфликтует с текущим прикреплением к персонажу

	if (!Grid->PlaceExistingBlock(Cell, BlockToInstall))
	{
		// Не должно случаться (мы только что проверили CanPlaceBlock), но на всякий
		// случай не теряем блок - возвращаем обратно в руки.
		AttachHeldBlock(BlockToInstall);
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "InstallFailed", "Не удалось установить"));
		return;
	}

	ClientBuildActionResult(true, FText::GetEmpty());
}

void UBuildComponent::DoDrop(const FVector& AtLocation)
{
	if (!HeldBlock)
	{
		ClientBuildActionResult(false, NSLOCTEXT("DeepVoid", "HandsEmpty", "Руки пусты"));
		return;
	}

	AShipBlockBase* Block = HeldBlock;
	DetachHeldBlock();

	const FVector FreeSpot = FindFreeDropLocation(AtLocation);
	Block->SetActorLocation(FreeSpot);
	Block->SetActorRotation(FRotator::ZeroRotator);
	Block->SetStateLoose();

	ClientBuildActionResult(true, FText::GetEmpty());
}

FVector UBuildComponent::FindFreeDropLocation(const FVector& DesiredLocation) const
{
	// Простой перебор по спирали клетка за клеткой (не физика - см. комментарий в .h).
	// Установленные на кораблях блоки (OwningShip != nullptr) не считаются "мешающими" -
	// нас интересуют только другие свободные блоки, лежащие на полу.
	static const TArray<FIntPoint> SpiralOffsets = {
		{0,0}, {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {1,-1}, {-1,1}, {-1,-1}
	};

	for (const FIntPoint& Offset : SpiralOffsets)
	{
		const FVector Candidate = DesiredLocation + FVector(Offset.X * DropClearRadius * 2.f, Offset.Y * DropClearRadius * 2.f, 0.f);

		bool bBlocked = false;
		for (TActorIterator<AShipBlockBase> It(GetWorld()); It; ++It)
		{
			if (!*It || It->OwningShip != nullptr) continue; // установленные не мешают

			if (FVector::DistSquared(It->GetActorLocation(), Candidate) < FMath::Square(DropClearRadius * 1.8f))
			{
				bBlocked = true;
				break;
			}
		}

		if (!bBlocked)
		{
			return Candidate;
		}
	}

	// Не нашли свободное место за 9 попыток - ставим как просили. Лучше видимое
	// наложение, чем незаметно потерянный блок.
	return DesiredLocation;
}

void UBuildComponent::PickUpLooseBlock(AShipBlockBase* Block)
{
	DoPickUp(Block);
}

void UBuildComponent::AttachHeldBlock(AShipBlockBase* Block)
{
	HeldBlock = Block;
	Block->SetStateHeld();
	Block->AttachToActor(GetOwner(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	// Грубо "перед грудью" - на глаз, как и обсуждали, замените на нормальный держащий
	// сокет/анимацию когда дойдут руки.
	Block->SetActorRelativeLocation(FVector(80.f, 0.f, 40.f));
	Block->SetActorRelativeRotation(FRotator::ZeroRotator);

	OnRep_HeldBlock(); // сервер не получает свой же OnRep автоматически - дублируем вручную
}

void UBuildComponent::DetachHeldBlock()
{
	if (HeldBlock)
	{
		HeldBlock->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	HeldBlock = nullptr;

	OnRep_HeldBlock();
}

void UBuildComponent::ClientBuildActionResult_Implementation(bool bSuccess, const FText& Reason)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, bSuccess ? FColor::Green : FColor::Red,
			bSuccess
				? TEXT("[Build] Действие выполнено")
				: FString::Printf(TEXT("[Build] Отказ: %s"), *Reason.ToString()));
	}
}

void UBuildComponent::OnRep_HeldBlock()
{
	// Визуал "в руках" уже обеспечен стандартной репликацией attachment'а (HeldBlock -
	// реплицируемый актор, прикреплённый на сервере - клиенты увидят прикрепление сами).
	// Точка расширения на будущее - звук подбора/сброса, анимация рук и т.п.
}
