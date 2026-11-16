#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "ScanLookComponent.generated.h"

/** Turns the camera sideways after everything else has positioned it. Driven by UScanLookComponent. */
UCLASS()
class FOOTBALL_API UScanCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	/** Degrees added to the camera yaw. Negative looks left. */
	float YawOffset = 0.f;

	virtual bool ModifyCamera(float DeltaTime, struct FMinimalViewInfo& InOutPOV) override;
};

/**
 * Scanning, as footballers do before receiving the ball: hold a key to glance over a shoulder.
 * Add to the player character. Hold Q to look left and E to look right; the view snaps back on release.
 *
 * Only the camera turns; the character keeps facing and moving where it did. A pass played while
 * glancing goes where you are looking, like a pass off the side of the foot.
 */
UCLASS(ClassGroup = Football, meta = (BlueprintSpawnableComponent))
class FOOTBALL_API UScanLookComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScanLookComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan")
	FKey LookLeftKey = EKeys::Q;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan")
	FKey LookRightKey = EKeys::E;

	/** How far the head turns, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan", meta = (ClampMin = "0.0", ClampMax = "170.0"))
	float ScanAngle = 80.f;

	/** How quickly the view turns and returns. Higher is snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan", meta = (ClampMin = "1.0"))
	float ScanSpeed = 14.f;

	/** How far the view is turned right now, in degrees. Negative is left. Passes are aimed with this included. */
	UFUNCTION(BlueprintPure, Category = "Scan")
	float GetCurrentYawOffset() const { return CurrentYaw; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UScanCameraModifier> Modifier;

	float CurrentYaw = 0.f;
};
