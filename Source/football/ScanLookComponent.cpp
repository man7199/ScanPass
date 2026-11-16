#include "ScanLookComponent.h"

#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScanLookComponent)

bool UScanCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	InOutPOV.Rotation.Yaw += YawOffset;
	return false;	// let any other modifiers run too
}

UScanLookComponent::UScanLookComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UScanLookComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PlayerController == nullptr || PlayerController->PlayerCameraManager == nullptr)
	{
		return;
	}

	if (Modifier == nullptr)
	{
		Modifier = Cast<UScanCameraModifier>(PlayerController->PlayerCameraManager->AddNewCameraModifier(UScanCameraModifier::StaticClass()));
		if (Modifier == nullptr)
		{
			return;
		}
	}

	// Both keys together cancel out.
	float TargetYaw = 0.f;
	if (PlayerController->IsInputKeyDown(LookLeftKey))
	{
		TargetYaw -= ScanAngle;
	}
	if (PlayerController->IsInputKeyDown(LookRightKey))
	{
		TargetYaw += ScanAngle;
	}

	// Fast at first and easing in at the end, like a quick turn of the head.
	CurrentYaw = FMath::FInterpTo(CurrentYaw, TargetYaw, DeltaTime, ScanSpeed);
	Modifier->YawOffset = CurrentYaw;
}

void UScanLookComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Modifier != nullptr)
	{
		Modifier->YawOffset = 0.f;
		Modifier = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
