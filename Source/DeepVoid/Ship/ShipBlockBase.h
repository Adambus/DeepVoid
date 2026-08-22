#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/InteractableInterface.h"
#include "ShipBlockBase.generated.h"

class AShip;
class UStaticMeshComponent;

/**
 * Структурный блок корпуса - "кирпичик", из которых физически состоит корабль.
 * Один блок = ровно одна клетка сетки (в отличие от модулей у структурных блоков
 * нет footprint - неправильная форма корпуса достигается тем, какие клетки вообще
 * заполнены, а не формой отдельного блока - осознанное упрощение для соло-разработки).
 *
 * ВАЖНО: один и тот же класс/актор используется в ТРЁХ состояниях, между которыми
 * блок физически переключается в процессе игры (см. SetState*):
 *   - Loose     - лежит сам по себе в мире (заспавнен вручную для теста, снят с
 *                 корабля, брошен игроком). Обычная коллизия, подбирается по F.
 *   - Held      - в руках у персонажа. Коллизия выключена, прикреплён к актору игрока.
 *   - Installed - часть сетки конкретного корабля (см. UShipGridComponent).
 * Никакого отдельного класса "предмет для подбора" не существует - это тот же самый
 * AShipBlockBase в другом состоянии, как и просили.
 *
 * Abstract - конкретные визуальные варианты (обычная стена, скошенный угол и т.д.)
 * делайте Blueprint-подклассами.
 */
UCLASS(Abstract)
class DEEPVOID_API AShipBlockBase : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AShipBlockBase();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditDefaultsOnly, Category = "Block")
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Block")
	float Health = 100.f;

	UFUNCTION()
	void OnRep_Health();

	// true для "ядра" корабля - именно от таких клеток UShipGridComponent считает
	// связность (flood-fill/BFS). На сцене/у стартового корабля должен быть как
	// минимум один блок с bIsCore = true (обычно - клетка под мостиком), иначе
	// связность считать не от чего и отвал отсутствующих блоков не сработает.
	UPROPERTY(EditDefaultsOnly, Category = "Block")
	bool bIsCore = false;

	// Как называть блок в UI (меню подбора/установки и т.п.). Пусто - берём имя класса.
	UPROPERTY(EditDefaultsOnly, Category = "Block")
	FText DisplayName;

public:
	UPROPERTY(BlueprintReadOnly, Category = "Block")
	TObjectPtr<AShip> OwningShip;

	UPROPERTY(BlueprintReadOnly, Category = "Block")
	FIntPoint GridCell = FIntPoint::ZeroValue;

	bool IsCore() const { return bIsCore; }
	float GetHealthPercent() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Block")
	FText GetDisplayNameText() const { return DisplayName.IsEmpty() ? GetClass()->GetDisplayNameText() : DisplayName; }

	// Сервер вызывает сразу после установки блока в сетку (UShipGridComponent::PlaceBlock
	// / PlaceExistingBlock).
	void InitializeBlock(AShip* InOwningShip, FIntPoint InCell);

	// Сбрасывает принадлежность кораблю - вызывает UShipGridComponent::DetachBlockForPickup
	// перед тем, как отдать блок в руки игроку.
	void ClearShipOwnership();

	// --- Переключение физического состояния блока (см. комментарий у класса) ---
	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetStateInstalled();

	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetStateHeld();

	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetStateLoose();

	// Возвращает true, если блок уничтожен (Health <= 0) - вызывающий код
	// (например, боевая система) должен после этого вызвать
	// UShipGridComponent::RemoveBlock + RecalculateConnectivity.
	UFUNCTION(BlueprintCallable, Category = "Block")
	bool ApplyDamage(float DamageAmount);

	// --- IInteractableInterface - подбор с земли по F. Снятие УСТАНОВЛЕННОГО блока с
	// корабля через F намеренно НЕ разрешено (см. CanInteract_Implementation) - это
	// осознанное решение, чтобы игрок случайно не разбирал корабль, пытаясь
	// провзаимодействовать с чем-то рядом. Снятие с корабля - только через контекстное
	// меню ПКМ (см. UBuildComponent::QueryBuildMenu, EBuildAction::PickUp).
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* InteractingActor) const override;
	virtual void OnInteract_Implementation(AActor* InteractingActor) override;
};
