#include "Ship/ShipModule_Weapon.h"
#include "Ship/Ship.h"
#include "Ship/ShipGridComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Net/UnrealNetwork.h"

AShipModule_Weapon::AShipModule_Weapon()
{
	bReplicates = true;
	SetReplicateMovement(false);

	TurretRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TurretRoot"));
	SetRootComponent(TurretRoot);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(TurretRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 800.f;
	SpringArm->SetRelativeRotation(FRotator(-70.f, 0.f, 0.f));
	SpringArm->SetUsingAbsoluteRotation(true);
	SpringArm->bDoCollisionTest = false;

	TurretCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TurretCamera"));
	TurretCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	TurretCamera->bUsePawnControlRotation = false;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AShipModule_Weapon::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine)
	{
		AActor* Parent = GetAttachParentActor();
		GEngine->AddOnScreenDebugMessage(-1, 15.f, Parent ? FColor::Green : FColor::Red,
			FString::Printf(TEXT("[Weapon] BeginPlay: AttachParentActor = %s"),
				Parent ? *Parent->GetName() : TEXT("NONE - НЕ ПРИКРЕПЛЕНО!")));
	}
}

void AShipModule_Weapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShipModule_Weapon, CurrentYawOffset);
	DOREPLIFETIME(AShipModule_Weapon, bIsOccupiedByPlayer);
	DOREPLIFETIME(AShipModule_Weapon, OperatorOriginalPawn);
	DOREPLIFETIME(AShipModule_Weapon, AnchorCell);
	DOREPLIFETIME(AShipModule_Weapon, CurrentFacing);
}

void AShipModule_Weapon::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (RotateAction)
		{
			EIC->BindAction(RotateAction, ETriggerEvent::Triggered, this, &AShipModule_Weapon::Input_Rotate);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Weapon] Rotate Action НЕ назначен в BP_ShipModule_Weapon!"));
		}

		if (FireAction)
		{
			EIC->BindAction(FireAction, ETriggerEvent::Started, this, &AShipModule_Weapon::Input_Fire);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Weapon] Fire Action НЕ назначен в BP_ShipModule_Weapon!"));
		}

		if (ExitTurretAction)
		{
			EIC->BindAction(ExitTurretAction, ETriggerEvent::Started, this, &AShipModule_Weapon::Input_ExitTurret);
		}
		else if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red, TEXT("[Weapon] Exit Turret Action НЕ назначен в BP_ShipModule_Weapon!"));
		}
	}
}

void AShipModule_Weapon::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (TurretMappingContext)
			{
				Subsystem->AddMappingContext(TurretMappingContext, 10);
			}
			else if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
					TEXT("[Weapon] Turret Mapping Context НЕ назначен в BP_ShipModule_Weapon!"));
			}
		}

		if (PC->IsLocalController())
		{
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void AShipModule_Weapon::UnPossessed()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (TurretMappingContext)
			{
				Subsystem->RemoveMappingContext(TurretMappingContext);
			}
		}

		if (PC->IsLocalController())
		{
			PC->SetShowMouseCursor(true);

			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
		}
	}

	Super::UnPossessed();
}

void AShipModule_Weapon::Input_Rotate(const FInputActionValue& Value)
{
	const float MouseDeltaX = Value.Get<FVector2D>().X;
	ServerRotate(MouseDeltaX);
}

void AShipModule_Weapon::Input_Fire(const FInputActionValue& Value)
{
	ServerFire();
}

void AShipModule_Weapon::Input_ExitTurret(const FInputActionValue& Value)
{
	ServerExitTurret();
}

void AShipModule_Weapon::ServerRotate_Implementation(float MouseDeltaX)
{
	CurrentYawOffset = FMath::Clamp(CurrentYawOffset + MouseDeltaX * MouseSensitivity, -MaxTraverseDegrees, MaxTraverseDegrees);
	// Итоговый поворот = базовый Facing (куда пушка "смотрит наружу" по сетке) +
	// свободный люфт прицеливания вокруг него. Раньше тут был просто CurrentYawOffset
	// от нуля - работало, пока модуль не был привязан к сетке с собственной
	// ориентацией; теперь ноль без Facing был бы неверным "по умолчанию смотрит на
	// North корабля", а не туда, куда пушку реально развернула сетка при установке.
	SetActorRelativeRotation(FRotator(0.f, ShipGrid::DirectionToYaw(CurrentFacing) + CurrentYawOffset, 0.f));
	OnRep_CurrentYawOffset();
}

void AShipModule_Weapon::ServerFire_Implementation()
{
	Fire();
}

void AShipModule_Weapon::ServerExitTurret_Implementation()
{
	ExitTurret();
}

void AShipModule_Weapon::Fire()
{
	if (!HasAuthority() || !bCanFire) return;

	bCanFire = false;
	GetWorld()->GetTimerManager().SetTimer(FireCooldownHandle, this, &AShipModule_Weapon::ResetFireCooldown, FireRate, false);

	const FVector Start = Mesh->GetComponentLocation();
	const FVector End = Start + GetActorForwardVector() * Range;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	if (GetAttachParentActor()) Params.AddIgnoredActor(GetAttachParentActor());

	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel1, Params);

	if (bHit && Hit.GetActor())
	{
		UGameplayStatics::ApplyPointDamage(
			Hit.GetActor(), Damage, GetActorForwardVector(),
			Hit, GetInstigatorController(), this, UDamageType::StaticClass());
	}
}

void AShipModule_Weapon::OnRep_CurrentYawOffset()
{
	SetActorRelativeRotation(FRotator(0.f, ShipGrid::DirectionToYaw(CurrentFacing) + CurrentYawOffset, 0.f));
}

void AShipModule_Weapon::EnterTurret(APawn* CharacterToStore, AController* InstigatingController)
{
	if (!HasAuthority() || !InstigatingController) return;

	OperatorOriginalPawn = CharacterToStore;
	bIsOccupiedByPlayer = true;
	InstigatingController->Possess(this);
}

void AShipModule_Weapon::ExitTurret()
{
	if (!HasAuthority()) return;

	AController* OperatorController = GetController();
	if (OperatorController && OperatorOriginalPawn)
	{
		OperatorController->Possess(OperatorOriginalPawn);

		if (ACharacter* Character = Cast<ACharacter>(OperatorOriginalPawn))
		{
			if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
			{
				CMC->StopMovementImmediately();
			}
		}

		OperatorOriginalPawn = nullptr;
	}

	bIsOccupiedByPlayer = false;
}

// ---------------------------------------------------------------------------
// IShipGridModuleInterface
// ---------------------------------------------------------------------------

void AShipModule_Weapon::OnPlacedInGrid(AShip* InOwningShip, FIntPoint InAnchorCell, EGridDirection InFacing)
{
	OwningShip = InOwningShip;
	AnchorCell = InAnchorCell;
	CurrentFacing = InFacing;
	CurrentYawOffset = 0.f;

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

void AShipModule_Weapon::OnRemovedFromGrid()
{
	// Если в момент снятия (например, отвал секции корабля в бою) турель была
	// занята игроком - высаживаем его сначала, иначе Destroy() унесёт с собой
	// контроллер живого игрока.
	if (bIsOccupiedByPlayer)
	{
		ExitTurret();
	}
}

bool AShipModule_Weapon::IsApproachDirectionAllowed(EGridDirection ApproachDir) const
{
	// Нельзя занять турель, стоя прямо перед стволом (см. открытую задачу №2 из
	// хендоффа) - разрешены все стороны, кроме той, куда пушка "смотрит".
	if (bIsDirectional && ApproachDir == CurrentFacing)
	{
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// IInteractableInterface
// ---------------------------------------------------------------------------

FText AShipModule_Weapon::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("DeepVoid", "WeaponSeat", "Занять турель");
}

bool AShipModule_Weapon::CanInteract_Implementation(AActor* InteractingActor) const
{
	if (bIsOccupiedByPlayer) return false;

	if (OwningShip && InteractingActor)
	{
		const EGridDirection ApproachDir = ShipGrid::GetApproachDirection(OwningShip, GetActorLocation(), InteractingActor->GetActorLocation());
		if (!IsApproachDirectionAllowed(ApproachDir))
		{
			return false;
		}
	}

	return true;
}

void AShipModule_Weapon::OnInteract_Implementation(AActor* InteractingActor)
{
	APawn* InstigatorPawn = Cast<APawn>(InteractingActor);
	if (!InstigatorPawn) return;

	AController* InstigatingController = InstigatorPawn->GetController();
	if (!InstigatingController) return;

	EnterTurret(InstigatorPawn, InstigatingController);
}
