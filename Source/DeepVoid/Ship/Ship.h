#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Ship/ShipGridTypes.h"
#include "Ship.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
class UShipGridComponent;
class AShipBlockBase;

/**
 * Корабль как отдельный Pawn (простая геометрическая фигура - куб). Игрок садится за
 * штурвал через AShipModule_Bridge (Possess), после чего WASD/Q/E управляют кораблём
 * напрямую - это стандартный ввод пешки, ничего вручную не перенаправляется.
 */
UCLASS()
class DEEPVOID_API AShip : public APawn
{
	GENERATED_BODY()

public:
	AShip();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UCameraComponent> TopDownCamera;

	// Сетка корабля - блоки корпуса + модули. См. Ship/ShipGridComponent.h.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ship")
	TObjectPtr<UShipGridComponent> GridComponent;

	// --- Ввод при пилотировании ---
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> PilotingMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction; // WASD, Axis2D

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> YawAction; // Q/E, Axis1D

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> ExitPilotAction; // F ещё раз или Esc

	// --- Движение (инерция, не мгновенное) ---
	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float MaxThrust = 200000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float MaxSpeed = 3000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float MaxAngularSpeed = 30.f; // градусы/сек

	UPROPERTY(Replicated)
	FVector CurrentVelocity = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector2D ThrustInput = FVector2D::ZeroVector;

	UPROPERTY(Replicated)
	float YawInput = 0.f;

	// Кого пилот "занял" - чтобы вернуть управление при выходе.
	UPROPERTY(Replicated)
	TObjectPtr<APawn> PilotOriginalPawn;

	void Input_Move(const FInputActionValue& Value);
	void Input_MoveCompleted(const FInputActionValue& Value);
	void Input_Yaw(const FInputActionValue& Value);
	void Input_YawCompleted(const FInputActionValue& Value);
	void Input_ExitPilot(const FInputActionValue& Value);

	UFUNCTION(Server, Unreliable)
	void ServerSetThrust(FVector2D Thrust);

	UFUNCTION(Server, Unreliable)
	void ServerSetYaw(float Yaw);

	UFUNCTION(Server, Reliable)
	void ServerExitPilot();

public:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// Вызывается мостиком (AShipModule_Bridge) на сервере, когда игрок нажал F рядом с ним.
	UFUNCTION(BlueprintCallable, Category = "Ship")
	void EnterPilotMode(APawn* CharacterToStore, AController* InstigatingController);

	UFUNCTION(BlueprintCallable, Category = "Ship")
	void ExitPilotMode();

	UFUNCTION(BlueprintPure, Category = "Ship")
	UShipGridComponent* GetGridComponent() const { return GridComponent; }

	// --- Строительство (вызывать с клиента - реализация сама уходит на сервер) ---
	// Reliable, в отличие от движения/поворота - действия строительства редкие и
	// каждое из них важно (в отличие от постоянного потока Move/Yaw), потеря
	// такого пакета не должна молча "съедать" установку блока.
	UFUNCTION(BlueprintCallable, Category = "Ship|Build")
	void RequestPlaceBlock(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass);

	UFUNCTION(BlueprintCallable, Category = "Ship|Build")
	void RequestRemoveBlock(FIntPoint Cell);

	UFUNCTION(BlueprintCallable, Category = "Ship|Build")
	void RequestPlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass);

	UFUNCTION(BlueprintCallable, Category = "Ship|Build")
	void RequestRemoveModule(FIntPoint AnchorCell);

protected:
	UFUNCTION(Server, Reliable)
	void ServerPlaceBlock(FIntPoint Cell, TSubclassOf<AShipBlockBase> BlockClass);

	UFUNCTION(Server, Reliable)
	void ServerRemoveBlock(FIntPoint Cell);

	UFUNCTION(Server, Reliable)
	void ServerPlaceModule(FIntPoint AnchorCell, EGridDirection RequestedFacing, TSubclassOf<AActor> ModuleClass);

	UFUNCTION(Server, Reliable)
	void ServerRemoveModule(FIntPoint AnchorCell);
};
