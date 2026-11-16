#include "GrassInteractorComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GrassInteractorComponent)

namespace ProceduralGrass
{
	static TArray<TWeakObjectPtr<UGrassInteractorComponent>>& GetInteractorRegistry()
	{
		static TArray<TWeakObjectPtr<UGrassInteractorComponent>> Registry;
		return Registry;
	}
}

const TArray<TWeakObjectPtr<UGrassInteractorComponent>>& UGrassInteractorComponent::GetAllInteractors()
{
	return ProceduralGrass::GetInteractorRegistry();
}

void UGrassInteractorComponent::GetShapeSegment(FVector& OutStart, FVector& OutEnd) const
{
	OutStart = GetComponentLocation();
	OutEnd = OutStart;

	const USceneComponent* Parent = GetAttachParent();
	if (ShapeEndSocket.IsNone() || Parent == nullptr || !Parent->DoesSocketExist(ShapeEndSocket))
	{
		return;	// a sphere
	}

	OutEnd = Parent->GetSocketLocation(ShapeEndSocket);
	const FVector Along = OutEnd - OutStart;
	const FVector Flat = FVector(Along.X, Along.Y, 0.0).GetSafeNormal();

	// Heel: straight back along the ground from the ankle. Toes: on past the ball of the foot, following its slope.
	OutStart -= Flat * (double)ShapeStartExtension;
	OutEnd += Along.GetSafeNormal() * (double)ShapeEndExtension;
}

void UGrassInteractorComponent::OnRegister()
{
	Super::OnRegister();
	ProceduralGrass::GetInteractorRegistry().AddUnique(this);
}

void UGrassInteractorComponent::OnUnregister()
{
	ProceduralGrass::GetInteractorRegistry().RemoveSwap(this);
	Super::OnUnregister();
}
