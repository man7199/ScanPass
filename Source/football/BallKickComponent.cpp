#include "BallKickComponent.h"

#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GrassRollingResistanceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "ScanLookComponent.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(BallKickComponent)

namespace BallKick
{
	/** Fallback rolling resistance, used when the ball has no Grass Rolling Resistance component. */
	static const double DefaultRollingDeceleration = 120.0;
	static const double DefaultSpeedDrag = 0.3;
	static const double DefaultMaxHopSpeed = 150.0;

	/** How much speed a bounce keeps, when the ball has no Grass Rolling Resistance to say. */
	static const double DefaultBounceRestitution = 0.58;
	static const double DefaultBounceGrip = 0.85;

	/** Looking up by less than this many degrees still plays the ball along the ground. */
	static const double GroundPassPitch = 2.0;
	/** KickLift fades in over this many degrees above that, so lifting the aim does not jump. */
	static const double LiftFadeInPitch = 6.0;

	static const double SimulationStep = 0.02;
	static const double SimulationMaxTime = 10.0;
	static const int32 StepsPerRecordedPoint = 10;	// a path point every 0.2 s
}

UBallKickComponent::UBallKickComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

float UBallKickComponent::GetChargeFraction() const
{
	return FMath::Clamp(CurrentCharge / FMath::Max(ChargeTime, 0.01f), 0.f, 1.f);
}

float UBallKickComponent::GetPowerScale() const
{
	return FMath::Lerp(1.f, FMath::Max(MaxPowerScale, 1.f), GetChargeFraction());
}

void UBallKickComponent::IgnoreKeyUntilReleased()
{
	bIgnoreUntilReleased = true;
	bCharging = false;
	CurrentCharge = 0.f;
}

void UBallKickComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (PlayerController == nullptr)
	{
		return;
	}

	// Press starts the charge, holding builds it, release kicks. A tap is a kick at base power.
	const bool bKeyDown = PlayerController->IsInputKeyDown(KickKey);

	if (bIgnoreUntilReleased)
	{
		bIgnoreUntilReleased = bKeyDown;
		bKeyWasDown = bKeyDown;
		return;
	}

	if (bKeyDown && !bKeyWasDown)
	{
		bCharging = true;
		CurrentCharge = 0.f;
	}
	else if (bKeyDown && bCharging)
	{
		CurrentCharge = FMath::Min(CurrentCharge + DeltaTime, ChargeTime);
	}
	else if (!bKeyDown && bCharging)
	{
		const float PowerScale = GetPowerScale();
		bCharging = false;
		CurrentCharge = 0.f;
		Kick(PowerScale);
	}

	bKeyWasDown = bKeyDown;
}

UPrimitiveComponent* UBallKickComponent::FindBallInRange() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr)
	{
		return nullptr;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	UPrimitiveComponent* Best = nullptr;
	double BestDistSq = (double)KickRange * (double)KickRange;

	// Anything that simulates physics counts as a ball.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (Candidate == Owner)
		{
			continue;
		}

		// Usually the root component, but a Blueprint ball may have its mesh under a plain scene root.
		UPrimitiveComponent* Body = nullptr;
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Candidate);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (Primitive != nullptr && Primitive->IsSimulatingPhysics())
			{
				Body = Primitive;
				break;
			}
		}
		if (Body == nullptr)
		{
			continue;
		}

		const FVector ToBall = Body->Bounds.Origin - OwnerLocation;
		const double DistSq = ToBall.SizeSquared2D();
		if (DistSq <= BestDistSq && FMath::Abs(ToBall.Z) < 200.0)
		{
			BestDistSq = DistSq;
			Best = Body;
		}
	}

	return Best;
}

FVector UBallKickComponent::GetKickDirection() const
{
	// Aim where the camera looks. Looking up lifts the shot by the same angle. Looking down (which is
	// how you see the ball at your feet in first person) does not drive it into the ground: anything
	// below the horizon counts as level, and level is a true ground pass with no lift at all. KickLift
	// only comes in once the player looks up, so a ground pass never leaves the grass.
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return FVector::ForwardVector;
	}

	const APawn* Pawn = Cast<APawn>(Owner);
	const FRotator AimRotation = (Pawn != nullptr) ? Pawn->GetControlRotation() : Owner->GetActorRotation();
	const double AimPitch = FMath::Max(FRotator::NormalizeAxis(AimRotation.Pitch), 0.0);
	const double LiftAngle = FMath::RadiansToDegrees(FMath::Atan((double)KickLift));
	const double LiftAmount = FMath::Clamp((AimPitch - BallKick::GroundPassPitch) / BallKick::LiftFadeInPitch, 0.0, 1.0);
	const double Elevation = (AimPitch < BallKick::GroundPassPitch)
		? 0.0
		: FMath::Clamp(AimPitch + LiftAngle * LiftAmount, 0.0, (double)MaxKickAngle);

	// The scan look turns only the camera, so its angle is not part of the control rotation. Add it,
	// so that a pass played while glancing left or right goes where the player is actually looking.
	double AimYaw = AimRotation.Yaw;
	if (const UScanLookComponent* ScanLook = Owner->FindComponentByClass<UScanLookComponent>())
	{
		AimYaw += (double)ScanLook->GetCurrentYawOffset();
	}

	return FRotator(Elevation, AimYaw, 0.0).Vector();
}

bool UBallKickComponent::PredictKick(float PowerScale, TArray<FVector>& OutPath, FVector& OutTarget) const
{
	OutPath.Reset();

	// One search for the ball, then everything below comes from it.
	const UPrimitiveComponent* Ball = FindBallInRange();
	if (Ball == nullptr)
	{
		return false;
	}

	const float BallRadius = (float)Ball->Bounds.BoxExtent.Z;
	FVector Position = Ball->Bounds.Origin;
	const float GroundZ = (float)Position.Z - BallRadius;
	OutTarget = Position;

	const UWorld* World = GetWorld();
	const double Gravity = (World != nullptr) ? FMath::Abs(World->GetGravityZ()) : 980.0;

	// Rolling resistance of this particular ball (including the grass under it), if it has any.
	const UGrassRollingResistanceComponent* Resistance = nullptr;
	if (const AActor* BallOwner = Ball->GetOwner())
	{
		Resistance = BallOwner->FindComponentByClass<UGrassRollingResistanceComponent>();
	}
	const double MaxHopSpeed = (Resistance != nullptr && Resistance->bSuppressHops) ? (double)Resistance->MaxHopSpeed : BallKick::DefaultMaxHopSpeed;
	const double BounceRestitution = (Resistance != nullptr) ? (double)Resistance->BounceRestitution : BallKick::DefaultBounceRestitution;
	const double BounceGrip = (Resistance != nullptr) ? (double)Resistance->BounceGrip : BallKick::DefaultBounceGrip;

	// Deceleration is a + k * speed. Read both once here rather than looking up the grass every step.
	double RollA = BallKick::DefaultRollingDeceleration;
	double RollK = BallKick::DefaultSpeedDrag;
	if (Resistance != nullptr)
	{
		RollA = Resistance->GetDecelerationAt(0.0);
		RollK = (Resistance->GetDecelerationAt(1000.0) - RollA) / 1000.0;
	}

	FVector Velocity = GetKickDirection() * (double)KickSpeed * (double)FMath::Max(PowerScale, 0.f);
	const double RestHeight = (double)GroundZ + (double)BallRadius;

	OutPath.Add(Position);

	const int32 MaxSteps = (int32)(BallKick::SimulationMaxTime / BallKick::SimulationStep);
	for (int32 Step = 1; Step <= MaxSteps; ++Step)
	{
		Velocity.Z -= Gravity * BallKick::SimulationStep;
		Position += Velocity * BallKick::SimulationStep;

		if (Position.Z <= RestHeight)
		{
			Position.Z = RestHeight;

			// A real bounce, or close enough to the ground to be rolling? A bounce too small to clear the
			// grass is held down, exactly as the ball's Grass Rolling Resistance does.
			if (Velocity.Z < -MaxHopSpeed && -Velocity.Z * BounceRestitution >= MaxHopSpeed)
			{
				Velocity.Z = -Velocity.Z * BounceRestitution;
				Velocity.X *= BounceGrip;
				Velocity.Y *= BounceGrip;
			}
			else if (Velocity.Z < -MaxHopSpeed)
			{
				// The last bounce, too small to leave the grass: the pace still takes the knock.
				Velocity.Z = 0.0;
				Velocity.X *= BounceGrip;
				Velocity.Y *= BounceGrip;
			}
			else
			{
				Velocity.Z = 0.0;

				const double Speed = Velocity.Size2D();
				if (Speed < 20.0)
				{
					break;
				}

				const double Deceleration = RollA + Speed * RollK;
				const double NewSpeed = FMath::Max(Speed - Deceleration * BallKick::SimulationStep, 0.0);
				Velocity.X *= NewSpeed / Speed;
				Velocity.Y *= NewSpeed / Speed;
			}
		}

		if (Step % BallKick::StepsPerRecordedPoint == 0)
		{
			OutPath.Add(Position);
		}
	}

	OutTarget = Position;
	if (OutPath.Num() == 0 || !OutPath.Last().Equals(Position))
	{
		OutPath.Add(Position);
	}
	return true;
}

bool UBallKickComponent::Kick(float PowerScale)
{
	UPrimitiveComponent* Ball = FindBallInRange();
	if (Ball == nullptr)
	{
		return false;
	}

	const FVector Direction = GetKickDirection();
	const double Speed = (double)KickSpeed * (double)FMath::Max(PowerScale, 0.f);

	// Start from rest so the result does not depend on how the ball was moving.
	Ball->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ball->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);

	UGrassRollingResistanceComponent* Resistance = Ball->GetOwner() ? Ball->GetOwner()->FindComponentByClass<UGrassRollingResistanceComponent>() : nullptr;
	if (Direction.Z < 0.001)
	{
		// Ground pass: straight along the grass, and held on it until the ball is next chipped.
		UGrassRollingResistanceComponent::LaunchAlongGround(Ball, Direction * Speed);
	}
	else
	{
		Ball->SetPhysicsLinearVelocity(Direction * Speed);
		if (Resistance != nullptr)
		{
			Resistance->NotifyLaunched();	// a chip on purpose, not a hop
		}
	}

	if (KickSound != nullptr)
	{
		// 0 at a tap, 1 at full power.
		const float Power01 = FMath::Clamp((PowerScale - 1.f) / FMath::Max(MaxPowerScale - 1.f, 0.01f), 0.f, 1.f);
		UGameplayStatics::PlaySoundAtLocation(this, KickSound, Ball->Bounds.Origin, FRotator::ZeroRotator,
			KickSoundVolume * FMath::Lerp(0.75f, 1.f, Power01), FMath::Lerp(1.08f, 0.92f, Power01));
	}

	OnBallKicked.Broadcast(Ball, PowerScale);

	return true;
}
