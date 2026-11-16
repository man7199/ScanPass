#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "BallFollowCamera.generated.h"

class UCameraComponent;

UENUM(BlueprintType)
enum class EBallCamStyle : uint8
{
	Side      UMETA(DisplayName = "Side (low, beside the ball)"),
	Chase     UMETA(DisplayName = "Chase (low, behind the ball)"),
	Trackside UMETA(DisplayName = "Trackside (fixed point, ball rolls past)")
};

/**
 * Recording camera for the portfolio clips.
 * Press ArmKey (C), then kick: the view cuts to a low camera that follows the ball through the grass
 * and blends back to the player once the ball has stopped. StyleKey (V) cycles the shot style.
 * Place one in the level. Leave Ball empty to use the first actor with a GrassRollingResistanceComponent.
 */
UCLASS()
class FOOTBALL_API ABallFollowCamera : public AActor
{
	GENERATED_BODY()

public:
	ABallFollowCamera();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ball Cam")
	TObjectPtr<UCameraComponent> Camera;

	/** Ball to follow. Empty = first actor in the level with a GrassRollingResistanceComponent. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam")
	TObjectPtr<AActor> Ball;

	UPROPERTY(EditAnywhere, Category = "Ball Cam")
	EBallCamStyle Style = EBallCamStyle::Side;

	/** Camera height above the ground (the bottom of the ball). Low = blades fill the frame. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam", meta = (Units = "cm"))
	float CameraHeight = 20.f;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Side", meta = (Units = "cm"))
	float SideDistance = 180.f;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Chase", meta = (Units = "cm"))
	float ChaseDistance = 250.f;

	/** Trackside: how far ahead along the ball's path the fixed camera is placed when the follow starts. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Trackside", meta = (Units = "cm"))
	float TracksideAhead = 900.f;

	/** Trackside: sideways offset from the ball's path so the ball rolls past, not into, the lens. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Trackside", meta = (Units = "cm"))
	float TracksideOffset = 100.f;

	/** Aim this many seconds ahead of the ball so it sits slightly behind frame centre. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Smoothing")
	float LookAheadTime = 0.2f;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Smoothing")
	float PositionLag = 6.f;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Smoothing")
	float RotationLag = 8.f;

	/** How quickly the camera turns to a new direction of travel. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Smoothing")
	float DirectionLag = 3.f;

	/** Follow starts once the armed ball moves faster than this. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Timing", meta = (Units = "cm/s"))
	float StartSpeed = 300.f;

	/** Ball counts as stopped below this speed... */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Timing", meta = (Units = "cm/s"))
	float StopSpeed = 30.f;

	/** ...for this long, then the view blends back to the player. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam|Timing", meta = (Units = "s"))
	float ReturnDelay = 1.f;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Timing", meta = (Units = "s"))
	float BlendTime = 0.35f;

	/** One press of ArmKey = one clip. Turn off to follow every kick and every return pass. */
	UPROPERTY(EditAnywhere, Category = "Ball Cam")
	bool bDisarmAfterFollow = true;

	UPROPERTY(EditAnywhere, Category = "Ball Cam")
	bool bHideHUDWhileFollowing = true;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Keys")
	FKey ArmKey = EKeys::C;

	UPROPERTY(EditAnywhere, Category = "Ball Cam|Keys")
	FKey StyleKey = EKeys::V;

protected:
	virtual void BeginPlay() override;

private:
	void ToggleArmed();
	void CycleStyle();
	void FindBall();
	void StartFollowing();
	void StopFollowing();
	void PickTracksidePoint();
	void ComputeDesired(FVector& OutLocation, FVector& OutLookAt) const;

	bool bArmed = false;
	bool bFollowing = false;
	bool bHUDWasShown = true;
	float SlowTime = 0.f;
	float FindCooldown = 0.f;
	FVector TravelDir = FVector::ForwardVector;
	FVector TracksidePoint = FVector::ZeroVector;
};
