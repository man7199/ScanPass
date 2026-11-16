#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrassRollingResistanceComponent.generated.h"

class UPrimitiveComponent;

/**
 * Makes a physics ball slow down and stop on grass, the way a football does on a pitch.
 *
 * The physics engine has friction for sliding but nothing for rolling, so a ball that rolls without
 * slipping would keep going for a very long time. Add this component to the ball actor. While the ball
 * is on the ground it loses speed at a constant rate (the blades bending under it) plus a part that
 * grows with speed (the blades it has to push through). Over a Procedural Grass Field, longer blades
 * slow it more.
 *
 * It also keeps a rolling ball on the ground. Upward kicks from the contact solver (seams between floor
 * meshes, edges of triangles, the ball's collision shape) would otherwise make a ground pass hop now and
 * then; a real ball on grass has nothing to bounce off and simply keeps rolling. A ball sent with
 * LaunchAlongGround is held on the ground completely until it is chipped again; any other ball only
 * loses small hops, so real bounces, such as a lofted pass landing, are left alone.
 *
 * The ground is found with a short trace straight down, so it works on any floor, with or without grass.
 */
UCLASS(ClassGroup = Physics, meta = (BlueprintSpawnableComponent))
class PROCEDURALGRASS_API UGrassRollingResistanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrassRollingResistanceComponent();

	/**
	 * Speed lost per second in cm/s, whatever the speed: how quickly a slow ball comes to a stop. For
	 * 3.5 cm grass; longer grass slows the ball more.
	 *
	 * Roughly how far a pass rolls with these two settings (tap 18 m/s / half 27 m/s / full 36 m/s):
	 *   150 / 0.4    28 / 48 / 68 m   slow, heavy grass
	 *   120 / 0.3    37 / 63 / 89 m   default
	 *   100 / 0.25   45 / 75 / 107 m  quick, dry pitch
	 *    80 / 0.2    56 / 94 / 134 m  very fast
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass", meta = (ClampMin = "0.0"))
	float RollingDeceleration = 120.f;

	/**
	 * Extra speed lost per second as a fraction of the current speed: how quickly a hard pass loses its
	 * pace at first. This one matters most for long, hard passes. See RollingDeceleration for examples.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass", meta = (ClampMin = "0.0"))
	float SpeedDrag = 0.3f;

	/** Below this speed in cm/s the ball is brought to rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass", meta = (ClampMin = "0.0"))
	float StopSpeed = 15.f;

	/** The ball counts as on the ground while its lowest point is within this distance in cm of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass", meta = (ClampMin = "0.0"))
	float GroundTolerance = 6.f;

	/**
	 * Share of its downward speed a ball keeps when it bounces on the grass. The height of each bounce is
	 * this squared: 0.58 keeps 1/3 of the height, 0.7 about half, 0.45 a fifth. A real football on a
	 * natural pitch is around 0.55 to 0.7.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Bounce", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float BounceRestitution = 0.58f;

	/** Share of its forward pace a ball keeps when it bounces. The grass grabs it a little at each bounce. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Bounce", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BounceGrip = 0.85f;

	/** Keep a rolling ball on the ground instead of letting it hop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Hops")
	bool bSuppressHops = true;

	/**
	 * Upward speed in cm/s below which a ball near the ground is treated as a hop and held down. Faster
	 * than this is a real bounce. 150 cm/s would lift the ball about 1 cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Hops", meta = (ClampMin = "0.0", EditCondition = "bSuppressHops"))
	float MaxHopSpeed = 150.f;

	/** How far above the ground in cm a hop is still caught. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Hops", meta = (ClampMin = "0.0", EditCondition = "bSuppressHops"))
	float HopCatchHeight = 12.f;

	/**
	 * Call when the ball is chipped or lofted on purpose. For a short moment its upward speed is left
	 * alone, so a gentle chip is not mistaken for a hop and pressed back down.
	 */
	void NotifyLaunched(float GraceSeconds = 0.3f);

	/**
	 * Deceleration in cm/s^2 the ball would have rolling at Speed where it is now. Includes the effect of
	 * the grass under it. Used by the aiming aid so its prediction matches what really happens.
	 */
	double GetDecelerationAt(double Speed) const;

	/** Distance in cm a ball rolling at FromSpeed covers before it is down to ToSpeed, on the grass under it now. */
	double GetRollDistance(double FromSpeed, double ToSpeed) const;

	/**
	 * Sends a ball along the ground at Velocity (its vertical part is ignored) and keeps it there: until
	 * it is chipped (NotifyLaunched), it can never gain upward speed.
	 */
	static void LaunchAlongGround(UPrimitiveComponent* Body, const FVector& Velocity);

	/** True while the ball is rolling along the ground, driven by this component (straight, held down). */
	bool IsGroundPass() const { return bGroundPass; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPrimitiveComponent* FindBody() const;
	/** 1 for 3.5 cm blades or no grass field, more for longer grass. */
	double GetGrassFactor(const FVector& Location) const;
	/** Gap in cm between the bottom of the ball and the ground under it, or false if there is no ground near. */
	bool GetGroundGap(const UPrimitiveComponent* Body, double& OutGap) const;

	float LaunchGraceLeft = 0.f;

	/** Velocity seen last frame, to spot the moment the ball lands. */
	FVector LastVelocity = FVector::ZeroVector;
	bool bHasLastVelocity = false;

	/** Ground roll: the ball goes straight along RollDirection at RollSpeed, which only the grass reduces. */
	bool bGroundPass = false;
	FVector RollDirection = FVector::ForwardVector;
	double RollSpeed = 0.0;
};
