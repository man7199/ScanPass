#include "GrassRollingResistanceComponent.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProceduralGrassComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GrassRollingResistanceComponent)

UGrassRollingResistanceComponent::UGrassRollingResistanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGrassRollingResistanceComponent::NotifyLaunched(float GraceSeconds)
{
	LaunchGraceLeft = FMath::Max(LaunchGraceLeft, GraceSeconds);
	bGroundPass = false;
}

UPrimitiveComponent* UGrassRollingResistanceComponent::FindBody() const
{
	// The component of the ball that simulates physics. Usually the root, but not always in a Blueprint.
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return nullptr;
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Owner);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive != nullptr && Primitive->IsSimulatingPhysics())
		{
			return Primitive;
		}
	}
	return nullptr;
}

double UGrassRollingResistanceComponent::GetGrassFactor(const FVector& Location) const
{
	float HeightAboveGrass = 0.f;
	if (const UProceduralGrassComponent* Grass = UProceduralGrassComponent::FindGrassAt(GetWorld(), Location, HeightAboveGrass))
	{
		// Longer grass holds the ball back more.
		return FMath::Clamp((double)Grass->BladeHeight / 3.5, 0.5, 3.0);
	}
	return 1.0;
}

double UGrassRollingResistanceComponent::GetDecelerationAt(double Speed) const
{
	const UPrimitiveComponent* Body = FindBody();
	const double GrassFactor = (Body != nullptr) ? GetGrassFactor(Body->Bounds.Origin) : 1.0;
	return ((double)RollingDeceleration + FMath::Max(Speed, 0.0) * (double)SpeedDrag) * GrassFactor;
}

double UGrassRollingResistanceComponent::GetRollDistance(double FromSpeed, double ToSpeed) const
{
	// Solves dv/dt = -g (a + k v) for the distance: (1/g) [ (v0 - v1) / k - a / k^2 ln((a + k v0) / (a + k v1)) ].
	FromSpeed = FMath::Max(FromSpeed, 0.0);
	ToSpeed = FMath::Clamp(ToSpeed, 0.0, FromSpeed);

	const UPrimitiveComponent* Body = FindBody();
	const double G = (Body != nullptr) ? GetGrassFactor(Body->Bounds.Origin) : 1.0;
	const double A = FMath::Max((double)RollingDeceleration, 1.0);
	const double K = (double)SpeedDrag;

	if (K < 1e-4)
	{
		return (FromSpeed * FromSpeed - ToSpeed * ToSpeed) / (2.0 * A * G);
	}
	return ((FromSpeed - ToSpeed) / K - (A / (K * K)) * FMath::Loge((A + K * FromSpeed) / (A + K * ToSpeed))) / G;
}

void UGrassRollingResistanceComponent::LaunchAlongGround(UPrimitiveComponent* Body, const FVector& Velocity)
{
	if (Body == nullptr)
	{
		return;
	}

	// No spin is forced on the ball: the ground contact gives it spin as it goes. (Forcing the full
	// rolling spin of a hard pass, hundreds of radians a second, makes the contact solver unstable and
	// throws the ball into the air.) From here the component drives the roll; see TickComponent.
	const FVector Flat(Velocity.X, Velocity.Y, 0.0);
	Body->SetPhysicsLinearVelocity(Flat);
	Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);

	if (const AActor* Owner = Body->GetOwner())
	{
		if (UGrassRollingResistanceComponent* Resistance = Owner->FindComponentByClass<UGrassRollingResistanceComponent>())
		{
			const double Speed = Flat.Size();
			Resistance->LaunchGraceLeft = 0.f;
			Resistance->bGroundPass = Speed > (double)Resistance->StopSpeed;
			Resistance->RollDirection = (Speed > 0.0) ? Flat / Speed : FVector::ForwardVector;
			Resistance->RollSpeed = Speed;
		}
	}
}

bool UGrassRollingResistanceComponent::GetGroundGap(const UPrimitiveComponent* Body, double& OutGap) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	const FVector Center = Body->Bounds.Origin;
	const double Radius = (double)Body->Bounds.BoxExtent.Z;

	// Straight down onto anything static: the pitch, the stadium floor. Players and the ball itself do not count.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrassRollingResistanceGround), false, GetOwner());
	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, Center, Center - FVector(0.0, 0.0, Radius + 100.0), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		OutGap = (double)Hit.Distance - Radius;
		return true;
	}

	// No collision under the ball: fall back to the grass field, if it is over one.
	float CenterHeight = 0.f;
	if (UProceduralGrassComponent::FindGrassAt(World, Center, CenterHeight) != nullptr)
	{
		OutGap = (double)CenterHeight - Radius;
		return true;
	}
	return false;
}

void UGrassRollingResistanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (DeltaTime <= 0.f)
	{
		return;
	}
	LaunchGraceLeft = FMath::Max(LaunchGraceLeft - DeltaTime, 0.f);

	UPrimitiveComponent* Body = FindBody();
	if (Body == nullptr)
	{
		return;
	}

	FVector Velocity = Body->GetPhysicsLinearVelocity();
	const FVector PreviousVelocity = LastVelocity;
	const bool bHadPrevious = bHasLastVelocity;
	LastVelocity = Velocity;
	bHasLastVelocity = true;

	double Gap = 0.0;
	if (!GetGroundGap(Body, Gap))
	{
		return;	// nothing under the ball
	}

	// A bounce: last frame the ball was coming down, now the ground has turned it round. The physics
	// engine's own bounce on grass is far too dead (it keeps about 1/8 of the height), so the bounce is
	// set here instead: BounceRestitution of the speed it landed with, and BounceGrip of its pace.
	if (bHadPrevious && !bGroundPass && Gap < (double)HopCatchHeight && PreviousVelocity.Z < -(double)MaxHopSpeed && Velocity.Z > -10.0)
	{
		const double LandingSpeed = -PreviousVelocity.Z;
		double ReboundSpeed = LandingSpeed * (double)BounceRestitution;
		if (bSuppressHops && ReboundSpeed < (double)MaxHopSpeed)
		{
			ReboundSpeed = 0.0;	// too small to clear the grass: it just rolls on from here
		}
		Velocity = FVector(PreviousVelocity.X * (double)BounceGrip, PreviousVelocity.Y * (double)BounceGrip, ReboundSpeed);
		Body->SetPhysicsLinearVelocity(Velocity);
		LastVelocity = Velocity;
		if (ReboundSpeed > 0.0)
		{
			LaunchGraceLeft = FMath::Max(LaunchGraceLeft, 0.15f);	// a real bounce: do not take it for a hop
			return;
		}
	}

	const FVector Flat(Velocity.X, Velocity.Y, 0.0);
	const double PhysicsSpeed = Flat.Size();
	const bool bOnGround = Gap <= (double)GroundTolerance;

	// A ball that is rolling along the ground (not just landed from a chip, not bouncing) becomes a
	// ground roll: from then on this component drives it, so it goes dead straight and slows down
	// exactly as the settings say.
	if (!bGroundPass && bOnGround && LaunchGraceLeft <= 0.f && FMath::Abs(Velocity.Z) < (double)MaxHopSpeed && PhysicsSpeed > (double)StopSpeed)
	{
		bGroundPass = true;
		RollDirection = Flat / PhysicsSpeed;
		RollSpeed = PhysicsSpeed;
	}

	if (bGroundPass)
	{
		// The physics contact between ball and floor keeps nudging a rolling ball: sideways off the
		// edges of floor triangles and the facets of the ball's collision, and backwards through
		// friction while the ball is still skidding. On a real pitch none of that happens; grass only
		// takes pace off. So small nudges are undone, while a real knock (a post, a player, the ball
		// being stopped or picked up) ends the roll and the ball does whatever physics says.
		const bool bStopped = PhysicsSpeed < RollSpeed * 0.5;
		const bool bTurned = PhysicsSpeed > 50.0 && FVector::DotProduct(Flat / PhysicsSpeed, RollDirection) < FMath::Cos(FMath::DegreesToRadians(30.0));
		if (bStopped || bTurned)
		{
			bGroundPass = false;
			return;
		}

		// Grass: a constant part plus a part that grows with speed.
		const double Deceleration = ((double)RollingDeceleration + RollSpeed * (double)SpeedDrag) * GetGrassFactor(Body->Bounds.Origin);
		RollSpeed = FMath::Max(RollSpeed - Deceleration * (double)DeltaTime, 0.0);

		if (RollSpeed < (double)StopSpeed)
		{
			bGroundPass = false;
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			return;
		}

		// Never upward: any lift the contact gave it is taken away, and if it got off the ground anyway
		// it is brought straight back down.
		double VerticalSpeed = FMath::Min(Velocity.Z, 0.0);
		if (bSuppressHops && !bOnGround)
		{
			VerticalSpeed = FMath::Min(VerticalSpeed, -FMath::Min(Gap * 20.0, 400.0));
		}
		if (!bSuppressHops)
		{
			VerticalSpeed = Velocity.Z;
		}

		Body->SetPhysicsLinearVelocity(RollDirection * RollSpeed + FVector(0.0, 0.0, VerticalSpeed));
		return;
	}

	// Not a ground roll: a ball in flight, bouncing, or knocked. Small hops near the ground are still
	// taken away, but real bounces are left alone.
	if (bSuppressHops && LaunchGraceLeft <= 0.f && Gap < (double)HopCatchHeight && Velocity.Z > 0.0 && Velocity.Z < (double)MaxHopSpeed)
	{
		Velocity.Z = 0.0;
		Body->SetPhysicsLinearVelocity(Velocity);
	}

	if (!bOnGround)
	{
		return;	// in the air
	}

	const double Speed = PhysicsSpeed;
	if (Speed < UE_KINDA_SMALL_NUMBER)
	{
		return;	// already at rest, let the body go to sleep
	}

	// Nearly stopped and not bouncing: bring it to rest once, so it does not creep forever.
	if (Speed < (double)StopSpeed && FMath::Abs(Velocity.Z) < 10.0)
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		return;
	}

	double Deceleration = ((double)RollingDeceleration + Speed * (double)SpeedDrag) * GetGrassFactor(Body->Bounds.Origin);
	// Never more than what would stop the ball within this frame, or it would start rolling backwards.
	Deceleration = FMath::Min(Deceleration, Speed / (double)DeltaTime);

	// Applied as a force, which the physics solver combines with gravity and the ground contact.
	const FVector Direction = Flat / Speed;
	Body->AddForce(-Direction * Deceleration, NAME_None, /*bAccelChange*/ true);
}
