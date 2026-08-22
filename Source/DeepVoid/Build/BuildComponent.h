#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildComponent.generated.h"

class UInputAction;
struct FInputActionValue;
class AShipBlockBase;
class AShip;

/** Что делает конкретный пункт меню ПКМ - см. FBuildMenuEntry. */
UENUM(BlueprintType)
enum class EBuildAction : uint8
{
	Install,  // поставить то, что в руках, в указанную клетку корабля
	PickUp,   // поднять указанный блок (свободный ИЛИ уже установленный на корабле)
	Drop      // бросить то, что в руках, рядом с собой
};

/**
 * Один пункт контекстного меню ПКМ. Возвращается массивом из QueryBuildMenu - ваш UMG
 * просто рисует по кнопке на каждый элемент с текстом DisplayName, а по нажатию
 * передаёт этот же элемент назад в ExecuteBuildAction. Реальную валидацию (сеть,
 * сетка, дальность) делает C++ - виджету достаточно просто показать/передать.
 */
USTRUCT(BlueprintType)
struct FBuildMenuEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Build")
	EBuildAction Action = EBuildAction::PickUp;

	// Для PickUp - какой конкретно блок поднять.
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	TObjectPtr<AShipBlockBase> TargetBlock = nullptr;

	// Для Install - в какой корабль и в какую клетку.
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	TObjectPtr<AShip> TargetShip = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FIntPoint TargetCell = FIntPoint::ZeroValue;

	// Мировая точка, к которой должен быть достаточно близко персонаж, чтобы действие
	// выполнилось - см. UBuildComponent::IsEntryInRange. Только для UI/предпроверки на
	// клиенте, сервер всегда пересчитывает дальность сам от актуального состояния.
	UPROPERTY(BlueprintReadOnly, Category = "Build")
	FVector WorldLocation = FVector::ZeroVector;
};

/**
 * Добавьте этот компонент на персонажа (Details -> Add -> Build Component), тот же
 * паттерн, что и UInteractionComponent. Отвечает за структурные блоки корпуса:
 *
 *  - F подбирает СВОБОДНЫЙ блок с земли (через IInteractableInterface самого
 *    AShipBlockBase - см. его CanInteract/OnInteract) или бросает то, что в руках -
 *    это единственное, за что отвечает F в этом компоненте, сам трейс под F не пишем,
 *    подбор идёт через уже существующий UInteractionComponent.
 *  - ПКМ (реализуете в своём UMG) вызывает QueryBuildMenu/ExecuteBuildAction - через
 *    это меню происходит УСТАНОВКА в корабль и СНЯТИЕ уже установленного блока (не
 *    через F - специально, чтобы игрок не разбирал корабль случайно, пытаясь
 *    провзаимодействовать с чем-то рядом).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class DEEPVOID_API UBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildComponent();

	bool IsHoldingBlock() const { return HeldBlock != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Build")
	AShipBlockBase* GetHeldBlock() const { return HeldBlock; }

	// Вызывается из AShipBlockBase::OnInteract_Implementation (уже на сервере, F). Берёт
	// СВОБОДНЫЙ (не установленный на корабле) блок в руки.
	UFUNCTION(BlueprintCallable, Category = "Build")
	void PickUpLooseBlock(AShipBlockBase* Block);

	// --- Меню ПКМ - вызывайте из своего UMG-виджета, см. пояснение в чате. ---

	// ScreenPosition - координаты курсора (PlayerController::GetMousePosition). Трейс
	// идёт от КАМЕРЫ через курсор, независимо от того, куда смотрит персонаж - это
	// осознанно (RimWorld/Ostranauts-стиль). Дальность НЕ проверяется здесь - меню
	// показывается на весь экран, дойти до цели нужно только к моменту выполнения
	// действия (см. ExecuteBuildAction/IsEntryInRange).
	UFUNCTION(BlueprintCallable, Category = "Build")
	TArray<FBuildMenuEntry> QueryBuildMenu(FVector2D ScreenPosition);

	// true, если персонаж сейчас достаточно близко, чтобы это действие выполнилось -
	// используйте для подсветки/отключения кнопок в виджете. Не авторитетно (финальную
	// проверку всегда переделывает сервер) - только для UX.
	UFUNCTION(BlueprintPure, Category = "Build")
	bool IsEntryInRange(const FBuildMenuEntry& Entry) const;

	// Выполнить пункт меню. Если персонаж слишком далеко - никуда не уходит, сразу
	// отказ (см. лог). Иначе уходит на сервер, результат прилетит через
	// ClientBuildActionResult (зелёным/красным в лог, как и остальные системы).
	UFUNCTION(BlueprintCallable, Category = "Build")
	void ExecuteBuildAction(const FBuildMenuEntry& Entry);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// Назначьте на клавишу F - ОТДЕЛЬНЫЙ ассет Input Action от вашего Interact Action,
	// но повешенный на ТУ ЖЕ физическую клавишу в Input Mapping Context (это разрешено -
	// один физический ввод может триггерить несколько разных Input Action одновременно).
	// Срабатывает только если руки заняты (см. Input_Drop) - если пусто, ничего не
	// делает, и обычный Interact Action у UInteractionComponent сработает как подбор.
	UPROPERTY(EditDefaultsOnly, Category = "Build")
	TObjectPtr<UInputAction> DropAction;

	UPROPERTY(EditDefaultsOnly, Category = "Build")
	float BuildRange = 300.f;

	// Радиус, в котором свободные блоки считаются "мешающими друг другу" при броске -
	// см. FindFreeDropLocation.
	UPROPERTY(EditDefaultsOnly, Category = "Build")
	float DropClearRadius = 60.f;

	// Блок, который сейчас в руках - тот же самый актор, что был на земле/на корабле,
	// НЕ копия и НЕ отдельный класс-заглушка.
	UPROPERTY(ReplicatedUsing = OnRep_HeldBlock, BlueprintReadOnly, Category = "Build")
	TObjectPtr<AShipBlockBase> HeldBlock;

	bool bWasLocallyControlled = false;

	void TryBindInput();
	void Input_Drop(const FInputActionValue& Value);

	UFUNCTION()
	void OnRep_HeldBlock();

	// Крепит блок к персонажу и переводит в состояние Held. Общая часть подбора со
	// свободной земли (PickUpLooseBlock) и снятия с корабля (см. DoPickUp).
	void AttachHeldBlock(AShipBlockBase* Block);
	void DetachHeldBlock();

	// Ищет ближайшую к DesiredLocation точку, где ещё нет другого свободного блока в
	// радиусе DropClearRadius - простой перебор по спирали клетка за клеткой, БЕЗ
	// физики/симуляции (см. обсуждение в чате: физика на дочернем компоненте создавала
	// бы больше проблем, чем решала). Установленные на кораблях блоки не мешают.
	FVector FindFreeDropLocation(const FVector& DesiredLocation) const;

	void DoPickUp(AShipBlockBase* Block);
	void DoInstall(AShip* TargetShip, FIntPoint Cell);
	void DoDrop(const FVector& AtLocation);

	UFUNCTION(Server, Reliable)
	void ServerExecuteBuildAction(FBuildMenuEntry Entry);

	UFUNCTION(Client, Reliable)
	void ClientBuildActionResult(bool bSuccess, const FText& Reason);
};
