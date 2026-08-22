#include "Ship/Ship.h"
#include "Ship/ShipGridComponent.h"
#include "Ship/ShipBlockBase.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"

AShip::AShip()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	SetReplicateMovement(false); // движение считаем и реплицируем сами (см. Tick)

	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	SetRootComponent(HullMesh);
	HullMesh->SetMobility(EComponentMobility::Movable); // обязательно - иначе персонаж не
	                                                     // сможет "стоять" на корабле как на
	                                                     // движущейся платформе (см. ниже)
	HullMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	HullMesh->SetSimulatePhysics(false);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 1200.f;
	SpringArm->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	SpringArm->bDoCollisionTest = false; // урок с прошлого раза - иначе камера ловится в геометрии

	TopDownCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TopDownCamera"));
	TopDownCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	TopDownCamera->bUsePawnControlRotation = false;

	AutoPossessPlayer = EAutoReceiveInput::Disabled; // возможен только через явный Possess от мостика
	AutoPossessAI = EAutoPossessAI::Disabled; // на всякий случай - та же защита, что и у турели

	GridComponent = CreateDefaultSubobject<UShipGridComponent>(TEXT("GridComponent"));
}

void AShip::BeginPlay()
{
	Super::BeginPlay();
}

void AShip::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShip, CurrentVelocity);
	DOREPLIFETIME(AShip, ThrustInput);
	DOREPLIFETIME(AShip, YawInput);
	DOREPLIFETIME(AShip, PilotOriginalPawn);
}

void AShip::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority()) return;

	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector DesiredDir = (Forward * ThrustInput.Y + Right * ThrustInput.X);

	CurrentVelocity += DesiredDir * MaxThrust * DeltaSeconds;

	if (DesiredDir.IsNearlyZero())
	{
		CurrentVelocity = FMath::VInterpTo(CurrentVelocity, FVector::ZeroVector, DeltaSeconds, 0.5f);
	}

	CurrentVelocity = CurrentVelocity.GetClampedToMaxSize(MaxSpeed);
	// false вместо true - без проверки коллизий. Мы сами считаем движение вручную (не через
	// физику), а sweep=true мгновенно блокировал корабль, если рядом стоит мостик/дверь/оружие -
	// именно поэтому ввод доходил (см. debug "Move input"), а сам корабль не двигался ни на пиксель.
	AddActorWorldOffset(CurrentVelocity * DeltaSeconds, false);

	if (!FMath::IsNearlyZero(YawInput))
	{
		AddActorLocalRotation(FRotator(0.f, YawInput * MaxAngularSpeed * DeltaSeconds, 0.f));
	}

	// Персонажа на палубе больше НЕ таскаем вручную (раньше был отдельный DeckTrigger +
	// CarryPassengers). Вместо этого используется встроенный механизм Unreal - "Movement Base":
	// CharacterMovementComponent сам определяет, что персонаж физически стоит на HullMesh
	// (у него Mobility = Movable, обязательное условие), и сам корректно двигает/поворачивает
	// персонажа вместе с этим компонентом - без конфликта двух параллельных систем движения,
	// который у нас был раньше (см. комментарий в ExitPilotMode).
}

void AShip::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShip::Input_Move);
			EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &AShip::Input_MoveCompleted);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Ship] Move Action НЕ назначен в BP_Ship!"));
		}

		if (YawAction)
		{
			EIC->BindAction(YawAction, ETriggerEvent::Triggered, this, &AShip::Input_Yaw);
			EIC->BindAction(YawAction, ETriggerEvent::Completed, this, &AShip::Input_YawCompleted);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Ship] Yaw Action НЕ назначен в BP_Ship!"));
		}

		if (ExitPilotAction)
		{
			EIC->BindAction(ExitPilotAction, ETriggerEvent::Started, this, &AShip::Input_ExitPilot);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Ship] Exit Pilot Action НЕ назначен в BP_Ship!"));
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[Ship] SetupPlayerInputComponent выполнен"));
		}
	}
	else if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Ship] PlayerInputComponent НЕ является EnhancedInputComponent!"));
	}
}

void AShip::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (PilotingMappingContext)
			{
				Subsystem->AddMappingContext(PilotingMappingContext, 10); // выше приоритет, чем у персонажа
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, TEXT("[Ship] PossessedBy: Mapping Context добавлен"));
				}
			}
			else if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
					TEXT("[Ship] Piloting Mapping Context НЕ назначен в BP_Ship! WASD работать не будет."));
			}
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Ship] PossessedBy: EnhancedInputLocalPlayerSubsystem не найден"));
		}
	}
}

void AShip::UnPossessed()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (PilotingMappingContext)
			{
				Subsystem->RemoveMappingContext(PilotingMappingContext);
			}
		}
	}

	Super::UnPossessed();
}

void AShip::Input_Move(const FInputActionValue& Value)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(2, 0.f, FColor::Cyan,
			FString::Printf(TEXT("[Ship] Move input: %s"), *Value.Get<FVector2D>().ToString()));
	}
	ServerSetThrust(Value.Get<FVector2D>());
}

void AShip::Input_MoveCompleted(const FInputActionValue& Value)
{
	ServerSetThrust(FVector2D::ZeroVector);
}

void AShip::Input_Yaw(const FInputActionValue& Value)
{
	ServerSetYaw(Value.Get<float>());
}

void AShip::Input_YawCompleted(const FInputActionValue& Value)
{
	ServerSetYaw(0.f);
}

void AShip::Input_ExitPilot(const FInputActionValue& Value)
{
	ServerExitPilot();
}

void AShip::ServerSetThrust_Implementation(FVector2D Thrust)
{
	ThrustInput = Thrust.GetSafeNormal();
}

void AShip::ServerSetYaw_Implementation(float Yaw)
{
	YawInput = FMath::Clamp(Yaw, -1.f, 1.f);
}

void AShip::ServerExitPilot_Implementation()
{
	ExitPilotMode();
}

void AShip::EnterPilotMode(APawn* CharacterToStore, AController* InstigatingController)
{
	if (!HasAuthority() || !InstigatingController)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				TEXT("[Ship] EnterPilotMode: нет прав сервера или контроллер пуст"));
		}
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
			FString::Printf(TEXT("[Ship] EnterPilotMode: сохраняю %s, вызываю Possess"),
				CharacterToStore ? *CharacterToStore->GetName() : TEXT("NULL")));
	}

	PilotOriginalPawn = CharacterToStore;
	InstigatingController->Possess(this);
}

void AShip::ExitPilotMode()
{
	if (!HasAuthority()) return;

	AController* PilotController = GetController();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow,
			FString::Printf(TEXT("[Ship] ExitPilotMode: Controller=%s, PilotOriginalPawn=%s"),
				PilotController ? TEXT("valid") : TEXT("NULL"),
				PilotOriginalPawn ? *PilotOriginalPawn->GetName() : TEXT("NULL")));
	}

	if (PilotController && PilotOriginalPawn)
	{
		// Останавливаем корабль, чтобы не летел по инерции с "залипшим" вводом.
		ThrustInput = FVector2D::ZeroVector;
		YawInput = 0.f;

		PilotController->Possess(PilotOriginalPawn);

		// Пока персонаж стоял "непосаженным" на палубе, CarryPassengers двигал его вручную
		// через AddActorWorldOffset, а CharacterMovementComponent тем временем ПАРАЛЛЕЛЬНО
		// и независимо продолжал считать гравитацию/пол/скорость - эти две системы
		// конфликтовали, и накопленное состояние резко "выстреливало" при возврате
		// управления (ощущалось как "сбила машина"). Сбрасываем его явно.
		if (ACharacter* Character = Cast<ACharacter>(PilotOriginalPawn))
		{
			if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
			{
				CMC->StopMovementImmediately();
			}
		}

		PilotOriginalPawn = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Строительство - тонкие обёртки над GridComponent. Вызывайте Request* с клиента
// (или сразу на сервере, при желании), Server* сами уходят через RPC.
// ---------------------------------------------------------------------------

void AShip::RequestPlaceBlock(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass)
{
	ServerPlaceBlock(Cell, BlockClass);
}

void AShip::RequestRemoveBlock(FIntPoint Cell)
{
	ServerRemoveBlock(Cell);
}

void AShip::RequestPlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass)
{
	ServerPlaceModule(AnchorCell, RequestedFacing, ModuleClass);
}

void AShip::RequestRemoveModule(FIntPoint AnchorCell)
{
	ServerRemoveModule(AnchorCell);
}

void AShip::ServerPlaceBlock_Implementation(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass)
{
	if (!GridComponent) return;

	FText Reason;
	if (!GridComponent->CanPlaceBlock(Cell, Reason))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
				FString::Printf(TEXT("[Grid] Block (%d,%d) НЕ поставлен: %s"), Cell.X, Cell.Y, *Reason.ToString()));
		}
		return;
	}

	GridComponent->PlaceBlock(Cell, BlockClass);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Green,
			FString::Printf(TEXT("[Grid] Block (%d,%d) поставлен"), Cell.X, Cell.Y));
	}
}

void AShip::ServerRemoveBlock_Implementation(FIntPoint Cell)
{
	if (GridComponent && GridComponent->RemoveBlock(Cell))
	{
		// Снятие блока могло разорвать связность корабля с остальными частями -
		// пересчитываем после каждого успешного снятия (то же самое нужно сделать
		// в боевой системе после AShipBlockBase::ApplyDamage, вернувшего true).
		GridComponent->RecalculateConnectivity();
	}
}

void AShip::ServerPlaceModule_Implementation(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass)
{
	if (!GridComponent) return;

	EGridDirection ResolvedFacing;
	FText Reason;
	if (!GridComponent->CanPlaceModule(AnchorCell, RequestedFacing, ModuleClass, ResolvedFacing, Reason))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
				FString::Printf(TEXT("[Grid] Module на (%d,%d) НЕ поставлен: %s"), AnchorCell.X, AnchorCell.Y, *Reason.ToString()));
		}
		return;
	}

	GridComponent->PlaceModule(AnchorCell, RequestedFacing, ModuleClass);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Green,
			FString::Printf(TEXT("[Grid] Module на (%d,%d) поставлен, Facing=%d"), AnchorCell.X, AnchorCell.Y, (int32)ResolvedFacing));
	}
}

void AShip::ServerRemoveModule_Implementation(FIntPoint AnchorCell)
{
	if (GridComponent)
	{
		GridComponent->RemoveModule(AnchorCell);
	}
}
