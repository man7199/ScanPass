#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralGrassField.generated.h"

class UProceduralGrassComponent;

/** Drop this into a level to get a field of procedural grass. All settings live on the Grass Component. */
UCLASS(hidecategories = (Collision, Cooking, Input, Replication))
class PROCEDURALGRASS_API AProceduralGrassField : public AActor
{
	GENERATED_BODY()

public:
	AProceduralGrassField(const FObjectInitializer& ObjectInitializer);

	UProceduralGrassComponent* GetGrassComponent() const { return GrassComponent; }

#if WITH_EDITOR
	/** Editor button. Creates (or reuses) /Game/Grass/M_ProceduralGrass and assigns it to the grass. */
	UFUNCTION(CallInEditor, Category = "Grass")
	void CreateDefaultMaterial();
#endif

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grass", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UProceduralGrassComponent> GrassComponent;
};
