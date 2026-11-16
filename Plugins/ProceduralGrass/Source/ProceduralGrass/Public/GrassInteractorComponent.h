#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GrassInteractorComponent.generated.h"

/**
 * Marks something that pushes procedural grass down: a ball, a foot, a sliding player.
 *
 * Add it to an actor and place it where the object touches the ground. For a character, add one per
 * foot and attach it to the foot bone (Parent Socket in the Details panel). A Procedural Grass
 * Component with Auto Collect Interactors on picks these up by itself; there is nothing to wire.
 *
 * By default the pushing shape is a sphere around the component (right for a ball). Set Shape End
 * Socket to stretch it into a capsule along a foot instead: from this component (the ankle bone) to
 * that socket on the same mesh (the ball-of-foot bone), extended back for the heel and forward for the
 * toes. Only the part of the capsule that is down in the grass pushes it, so a lifted heel or a foot in
 * the air leaves the grass alone.
 */
UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class PROCEDURALGRASS_API UGrassInteractorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Grass within this distance (cm) of the component (or of the capsule's centre line) is pushed away and flattened. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass", meta = (ClampMin = "1.0"))
	float Radius = 25.f;

	/**
	 * Socket or bone on the mesh this component is attached to, such as ball_l for a component on foot_l.
	 * The pushing shape becomes a capsule from here to there. None keeps a sphere.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Shape")
	FName ShapeEndSocket;

	/** How far in cm the capsule reaches back past this component, flat along the ground (the heel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Shape", meta = (ClampMin = "0.0"))
	float ShapeStartExtension = 0.f;

	/** How far in cm the capsule reaches past the end socket (the toes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Shape", meta = (ClampMin = "0.0"))
	float ShapeEndExtension = 0.f;

	/**
	 * How far in cm below the centre line the shape reaches when deciding whether it is down in the
	 * grass. 0 uses Radius, as for a sphere. Smaller than Radius gives a wide but flat footprint, so a
	 * foot only pushes when it is really on the ground.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Shape", meta = (ClampMin = "0.0"))
	float VerticalReach = 0.f;

	/**
	 * Only a limited number of interactors can push at once. Higher priority wins a slot first; among
	 * equals, the ones nearest the camera win.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
	int32 PushPriority = 0;

	/** The pushing shape's centre line in world space. Start and end are the same point for a sphere. */
	void GetShapeSegment(FVector& OutStart, FVector& OutEnd) const;

	/** VerticalReach, or Radius when that is 0. */
	float GetVerticalReach() const { return VerticalReach > 0.f ? VerticalReach : Radius; }

	/**
	 * Whether blades inside Radius lean away from this component. Only eight interactors can do that at
	 * once, so turn it off for actors that should just press the grass with their shape (Conform To
	 * Objects), such as a crowd of AI players.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
	bool bPushGrass = true;

	/** Lets gameplay switch the effect off without removing the component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
	bool bInteractorEnabled = true;

	/** Every registered interactor, in all worlds. Game thread only. */
	static const TArray<TWeakObjectPtr<UGrassInteractorComponent>>& GetAllInteractors();

protected:
	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface
};
