#include "ProceduralGrassField.h"

#include "ProceduralGrassComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ProceduralGrassField)

AProceduralGrassField::AProceduralGrassField(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GrassComponent = CreateDefaultSubobject<UProceduralGrassComponent>(TEXT("GrassComponent"));
	RootComponent = GrassComponent;
}

#if WITH_EDITOR
void AProceduralGrassField::CreateDefaultMaterial()
{
	if (GrassComponent != nullptr)
	{
		GrassComponent->CreateDefaultMaterial();
	}
}
#endif
