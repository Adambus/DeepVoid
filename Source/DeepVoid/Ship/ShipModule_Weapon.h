#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Interaction/InteractableInterface.h"
#include "Ship/ShipGridModuleInterface.h"
#include "Ship/ShipGridTypes.h"
#include "ShipModule_Weapon.generated.h"

class UStaticMeshComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class AShip;

/**
 * Оружие как отдельная "турель"-Pawn, независимая от штурвала. F рядом с ним -
 * отдельная посадка (Possess), не связанная с пилотированием всего корабля.
 * Пока сидите за турелью: мышь крутит ствол в пределах ±MaxTraverseDegrees ОТНОСИТЕЛЬНО
 * направления, куда модуль развёрнут в сетке (см. CurrentFacing) - не абсолютно
 * относительно корабля, как было раньше. ЛКМ стреляет, повторный F/Esc возвращает
 * управление персонажу.
 *
 * IShipGridModuleInterface реализован здесь напрямую (не через AShipModuleBase),
 * потому что AShipModule_Weapon - APawn, а AShipModuleBase - AActor: одиночное
 * наследование в C++ не позволяет унаследовать оба. Из-за этого несколько полей
 * (AnchorCell/CurrentFacing/OwningShip/Footprint-флаги) дублируют то, что есть в
 * AShipModuleBase - при появлении второго Pawn-модуля стоит вынести это в общий
 * набор свободных функций/миксин, сейчас ради простоты просто продублировано.
 */
UCLASS()
class DEEPVOID_API AShipModule_Weapon : public APawn, public IInteractableInterface, public IShipGridModuleInterface
{
	GENERATED_BODY()

public:
	AShipModule_Weapon();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	// Пустой корень, вокруг которого крутится прицел (Yaw). Настоящий UPROPERTY-член
	// класса - иначе BP-редактор показывает его как "чужой" generic-компонент без
	// нормального Details.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> TurretRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// Без своей камеры Possess() переключает управление, но игрок физически ничего не видит.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<class USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<class UCameraComponent> TurretCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float MaxTraverseDegrees = 45.f;

	// Чувствительность мыши - на сколько градусов крутить ствол за один "пиксель"
	// сырой дельты движения мыши. Подберите под вкус в BP.
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float MouseSensitivity = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Damage = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Range = 6000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float FireRate = 0.3f;

	// Смещение прицела ОТНОСИТЕЛЬНО базового Facing (не абсолютный угол на корабле) -
	// см. комментарий в .cpp у ServerRotate_Implementation.
	UPROPERTY(ReplicatedUsing = OnRep_CurrentYawOffset, BlueprintReadOnly, Category = "Weapon")
	float CurrentYawOffset = 0.f;

	// Кого "занял" оператор - чтобы вернуть управление при выходе.
	UPROPERTY(Replicated)
	TObjectPtr<APawn> OperatorOriginalPawn;

	// Явный флаг "занята ли турель прямо сейчас игроком" - НЕ полагаемся на встроенное
	// поле Controller у APawn (см. AutoPossessAI ниже).
	UPROPERTY(Replicated)
	bool bIsOccupiedByPlayer = false;

	FTimerHandle FireCooldownHandle;
	bool bCanFire = true;

	// --- ShipGrid ---
	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	TArray<FIntPoint> FootprintCells = { FIntPoint(0, 0) };

	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	bool bIsDirectional = true; // пушку нельзя ставить стволом внутрь корабля

	UPROPERTY(EditDefaultsOnly, Category = "ShipGrid")
	bool bRequiresOpenFacing = true;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ShipGrid")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ShipGrid")
	EGridDirection CurrentFacing = EGridDirection::North;

	UPROPERTY(BlueprintReadOnly, Category = "ShipGrid")
	TObjectPtr<AShip> OwningShip;

	// --- Ввод во время управления турелью ---
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> TurretMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ExitTurretAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> RotateAction;

	UFUNCTION()
	void OnRep_CurrentYawOffset();

	void ResetFireCooldown() { bCanFire = true; }

	void Input_Rotate(const FInputActionValue& Value);
	void Input_Fire(const FInputActionValue& Value);
	void Input_ExitTurret(const FInputActionValue& Value);

	UFUNCTION(Server, Unreliable)
	void ServerRotate(float MouseDeltaX);

	UFUNCTION(Server, Reliable)
	void ServerFire();

	UFUNCTION(Server, Reliable)
	void ServerExitTurret();

public:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	void Fire();

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EnterTurret(APawn* CharacterToStore, AController* InstigatingController);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ExitTurret();

	// --- IShipGridModuleInterface ---
	virtual TArray<FIntPoint> GetFootprintCells_Implementation() const override { return FootprintCells; }
	virtual bool IsDirectional_Implementation() const override { return bIsDirectional; }
	virtual bool RequiresOpenFacing_Implementation() const override { return bRequiresOpenFacing; }
	virtual void OnPlacedInGrid(AShip* InOwningShip, FIntPoint InAnchorCell, EGridDirection InFacing) override;
	virtual void OnRemovedFromGrid() override;
	virtual FIntPoint GetAnchorCell() const override { return AnchorCell; }
	virtual EGridDirection GetCurrentFacing() const override { return CurrentFacing; }
	virtual bool IsApproachDirectionAllowed(EGridDirection ApproachDir) const override;

	// --- IInteractableInterface ---
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* InteractingActor) const override;
	virtual void OnInteract_Implementation(AActor* InteractingActor) override;
};
