#include "BallFollowCamera.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

#include "GrassRollingResistanceComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BallFollowCamera)

ABallFollowCamera::ABallFollowCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	// Move after physics has moved the ball this frame, otherwise the camera trails by a frame and jitters.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	Camera->SetFieldOfView(55.f);
}

void ABallFollowCamera::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		EnableInput(PC);
		if (InputComponent)
		{
			InputComponent->BindKey(ArmKey, IE_Pressed, this, &ABallFollowCamera::ToggleArmed);
			InputComponent->BindKey(StyleKey, IE_Pressed, this, &ABallFollowCamera::CycleStyle);
		}
	}

	FindBall();
}

void ABallFollowCamera::FindBall()
{
	if (IsValid(Ball))
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->FindComponentByClass<UGrassRollingResistanceComponent>())
		{
			Ball = *It;
			return;
		}
	}
}

void ABallFollowCamera::ToggleArmed()
{
	bArmed = !bArmed;
	if (!bArmed && bFollowing)
	{
		StopFollowing();
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Yellow,
			bArmed ? TEXT("Ball cam ON - kick the ball") : TEXT("Ball cam OFF"));
	}
}

void ABallFollowCamera::CycleStyle()
{
	Style = static_cast<EBallCamStyle>((static_cast<uint8>(Style) + 1) % 3);
	if (bFollowing && IsValid(Ball))
	{
		PickTracksidePoint();
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Yellow,
			FString::Printf(TEXT("Ball cam: %s"), *UEnum::GetDisplayValueAsText(Style).ToString()));
	}
}

void ABallFollowCamera::PickTracksidePoint()
{
	FVector Origin, Extent;
	Ball->GetActorBounds(true, Origin, Extent);
	const FVector Right = FVector::CrossProduct(FVector::UpVector, TravelDir);
	TracksidePoint = Origin + TravelDir * TracksideAhead + Right * TracksideOffset;
	TracksidePoint.Z = Origin.Z - Extent.Z + CameraHeight;
}

void ABallFollowCamera::ComputeDesired(FVector& OutLocation, FVector& OutLookAt) const
{
	FVector Origin, Extent;
	Ball->GetActorBounds(true, Origin, Extent);
	const float GroundZ = Origin.Z - Extent.Z;
	const FVector Velocity = Ball->GetVelocity();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, TravelDir);

	switch (Style)
	{
	case EBallCamStyle::Side:
		// Beside the path and a little behind, so the flattened blades in the ball's wake stay in frame.
		OutLocation = Origin + Right * SideDistance - TravelDir * (SideDistance * 0.35f);
		OutLocation.Z = GroundZ + CameraHeight;
		break;
	case EBallCamStyle::Chase:
		OutLocation = Origin - TravelDir * ChaseDistance;
		OutLocation.Z = GroundZ + CameraHeight;
		break;
	case EBallCamStyle::Trackside:
	default:
		OutLocation = TracksidePoint;
		break;
	}

	OutLookAt = Origin + FVector(Velocity.X, Velocity.Y, 0.f) * LookAheadTime;
}

void ABallFollowCamera::StartFollowing()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	bFollowing = true;
	SlowTime = 0.f;
	if (bDisarmAfterFollow)
	{
		bArmed = false;
	}

	const FVector Velocity = Ball->GetVelocity();
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	if (!Flat.IsNearlyZero())
	{
		TravelDir = Flat.GetSafeNormal();
	}
	PickTracksidePoint();

	// Jump straight to the shot, then let the view blend from the player's camera to it.
	FVector Location, LookAt;
	ComputeDesired(Location, LookAt);
	SetActorLocationAndRotation(Location, (LookAt - Location).Rotation());
	PC->SetViewTargetWithBlend(this, BlendTime, VTBlend_Cubic);

	if (bHideHUDWhileFollowing)
	{
		if (AHUD* HUD = PC->GetHUD())
		{
			bHUDWasShown = HUD->bShowHUD;
			HUD->bShowHUD = false;
		}
	}
}

void ABallFollowCamera::StopFollowing()
{
	bFollowing = false;
	SlowTime = 0.f;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, BlendTime, VTBlend_Cubic);
		}
		if (bHideHUDWhileFollowing)
		{
			if (AHUD* HUD = PC->GetHUD())
			{
				HUD->bShowHUD = bHUDWasShown;
			}
		}
	}
}

void ABallFollowCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The drill manager may create or replace the ball after BeginPlay, so look again now and then.
	if (!IsValid(Ball))
	{
		Ball = nullptr;
		FindCooldown -= DeltaSeconds;
		if (FindCooldown <= 0.f)
		{
			FindCooldown = 1.f;
			FindBall();
		}
		if (!Ball)
		{
			if (bFollowing)
			{
				StopFollowing();
			}
			return;
		}
	}

	const FVector Velocity = Ball->GetVelocity();
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	const float Speed = Flat.Size();

	if (Speed > 50.f)
	{
		const FVector NewDir = Flat / Speed;
		const float Alpha = FMath::Clamp(DeltaSeconds * DirectionLag, 0.f, 1.f);
		const FVector Blended = FMath::Lerp(TravelDir, NewDir, Alpha).GetSafeNormal2D();
		TravelDir = Blended.IsNearlyZero() ? NewDir : Blended;
	}

	if (!bFollowing)
	{
		if (bArmed && Speed >= StartSpeed)
		{
			StartFollowing();
		}
		return;
	}

	if (Speed < StopSpeed)
	{
		SlowTime += DeltaSeconds;
		if (SlowTime >= ReturnDelay)
		{
			StopFollowing();
			return;
		}
	}
	else
	{
		SlowTime = 0.f;
	}

	FVector Location, LookAt;
	ComputeDesired(Location, LookAt);

	const FVector NewLocation = (Style == EBallCamStyle::Trackside)
		? Location
		: FMath::VInterpTo(GetActorLocation(), Location, DeltaSeconds, PositionLag);
	const FRotator WantRotation = (LookAt - NewLocation).Rotation();

	SetActorLocationAndRotation(NewLocation, FMath::RInterpTo(GetActorRotation(), WantRotation, DeltaSeconds, RotationLag));
}
