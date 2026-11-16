#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "BallKickComponent.generated.h"

class UPrimitiveComponent;
class USoundBase;

/** Fired after a kick, with the physics body that was kicked and the power it was kicked with (1 = base). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBallKicked, UPrimitiveComponent*, float);

/**
 * Add to the player character. Hold the kick key (left mouse button by default) to build up power, then
 * release to kick the nearest physics object within reach in the direction the camera is facing.
 *
 * A tap kicks at base power; holding for ChargeTime doubles it. UFootballHUD shows the bar and the
 * predicted path.
 */
UCLASS(ClassGroup = Football, meta = (BlueprintSpawnableComponent))
class FOOTBALL_API UBallKickComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBallKickComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick")
	FKey KickKey = EKeys::LeftMouseButton;

	/** Speed given to the ball in cm/s by a tap. 1800 is 18 m/s, a firm pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.0"))
	float KickSpeed = 1800.f;

	/** How long the key has to be held for full power, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.01"))
	float ChargeTime = 0.5f;

	/** Power at full charge, as a multiple of KickSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "1.0"))
	float MaxPowerScale = 2.f;

	/**
	 * Extra lift for shots played while looking up, on top of how far up the camera is looking. A pass
	 * played looking level or down is always a true ground pass and gets none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float KickLift = 0.05f;

	/** Steepest shot allowed, in degrees above the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.0", ClampMax = "85.0"))
	float MaxKickAngle = 55.f;

	/** How far from the character the ball can be, in cm (measured along the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick", meta = (ClampMin = "0.0"))
	float KickRange = 160.f;

	/** Played at the ball on every kick. Harder kicks are a little louder and lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick|Sound")
	TObjectPtr<USoundBase> KickSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kick|Sound", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float KickSoundVolume = 1.f;

	/**
	 * Ignores the kick key until it has been let go. The menus call this when they close, so the click
	 * that pressed a menu button does not also start charging a kick.
	 */
	void IgnoreKeyUntilReleased();

	/** Kicks the nearest ball in range. Returns false if there was none. Can also be called from Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Kick")
	bool Kick(float PowerScale = 1.f);

	/** True while the key is held down and power is building. */
	UFUNCTION(BlueprintPure, Category = "Kick")
	bool IsCharging() const { return bCharging; }

	/** How far the power bar is filled, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Kick")
	float GetChargeFraction() const;

	/** Power the next kick would use: 1 at a tap, MaxPowerScale at full charge. */
	UFUNCTION(BlueprintPure, Category = "Kick")
	float GetPowerScale() const;

	/** The ball this component would kick right now, or null if there is none in range. */
	UFUNCTION(BlueprintPure, Category = "Kick")
	UPrimitiveComponent* FindBallInRange() const;

	/** Direction the next kick would send the ball. */
	UFUNCTION(BlueprintPure, Category = "Kick")
	FVector GetKickDirection() const;

	/**
	 * Works out where the ball would end up, by stepping a simple simulation forward (gravity, one or
	 * more bounces, then rolling to a stop). Approximate: it is an aiming aid, not the real physics.
	 * @param PowerScale	Power to predict for, normally GetPowerScale().
	 * @param OutPath		Points along the predicted path, roughly 0.1 s apart.
	 * @param OutTarget		Where the ball comes to rest.
	 * @return False if there is no ball in range to predict for.
	 */
	bool PredictKick(float PowerScale, TArray<FVector>& OutPath, FVector& OutTarget) const;

	/** Lets other code (the passing drill) know the player has just kicked. */
	FOnBallKicked OnBallKicked;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	float CurrentCharge = 0.f;
	bool bCharging = false;
	bool bKeyWasDown = false;
	bool bIgnoreUntilReleased = false;
};
