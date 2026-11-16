#include "FootballBot.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GrassInteractorComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FootballBot)

AFootballBot::AFootballBot()
{
	PrimaryActorTick.bCanEverTick = true;

	// No controller: the drill manager steers the bot directly.
	AutoPossessAI = EAutoPossessAI::Disabled;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationYaw = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bRunPhysicsWithNoController = true;	// keep walking and falling without a controller
	Movement->bOrientRotationToMovement = true;		// face the way it walks
	Movement->RotationRate = FRotator(0.0, 540.0, 0.0);
	Movement->MaxWalkSpeed = 300.f;
	Movement->MaxAcceleration = 1200.f;
	Movement->BrakingDecelerationWalking = 1400.f;

	// Same placement the mannequin has in the engine templates: feet at the bottom of the capsule, facing +X.
	GetMesh()->SetRelativeLocation(FVector(0.0, 0.0, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
	GetMesh()->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
	GetMesh()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// The ball goes through the bots' capsules. A bot that wants it takes it before it gets there, and
	// a ball running into the round bottom of a capsule is pushed up into the air.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);

	TeamMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeamMarker"));
	TeamMarker->SetupAttachment(RootComponent);
	TeamMarker->SetRelativeLocation(FVector(0.0, 0.0, 125.0));
	TeamMarker->SetRelativeScale3D(FVector(0.22));
	TeamMarker->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	TeamMarker->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		TeamMarker->SetStaticMesh(SphereMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MarkerMaterial.Succeeded())
	{
		TeamMarker->SetMaterial(0, MarkerMaterial.Object);
		PaintMaterial = MarkerMaterial.Object;
	}

	// One grass interactor per foot. Each is a capsule shaped like the foot: from the ankle bone to the
	// ball of the foot (ball_l / ball_r), 5 cm further back for the heel and 6 cm further forward for the
	// toes, 8 cm either side. Only the part that is down in the grass bends it aside (VerticalReach 6 cm
	// below the bone line), so a foot in the air mid-stride or a lifted heel leaves it alone. The bot is
	// also drawn into the grass depth capture, which presses the blades under the exact foot.
	// PushPriority -1: the ball always gets one of the push slots before any foot.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const bool bLeft = Side == 0;
		UGrassInteractorComponent* Foot = CreateDefaultSubobject<UGrassInteractorComponent>(bLeft ? TEXT("LeftFootGrassInteractor") : TEXT("RightFootGrassInteractor"));
		Foot->SetupAttachment(GetMesh(), bLeft ? TEXT("foot_l") : TEXT("foot_r"));
		Foot->Radius = 8.0f;
		Foot->ShapeEndSocket = bLeft ? TEXT("ball_l") : TEXT("ball_r");
		Foot->ShapeStartExtension = 5.0f;
		Foot->ShapeEndExtension = 6.0f;
		Foot->VerticalReach = 6.0f;
		Foot->PushPriority = -1;
		(bLeft ? LeftFootGrassInteractor : RightFootGrassInteractor) = Foot;
	}

}

void AFootballBot::Setup(EFootballTeam InTeam, USkeletalMesh* InMesh, UClass* InAnimClass)
{
	Team = InTeam;

	if (InMesh != nullptr)
	{
		GetMesh()->SetSkeletalMesh(InMesh);
		if (InAnimClass != nullptr)
		{
			GetMesh()->SetAnimInstanceClass(InAnimClass);
		}
	}

	const FLinearColor Color = (Team == EFootballTeam::Friendly) ? FLinearColor(0.02f, 0.25f, 1.0f) : FLinearColor(1.0f, 0.03f, 0.02f);
	if (UMaterialInstanceDynamic* MarkerMaterial = TeamMarker->CreateDynamicMaterialInstance(0))
	{
		MarkerMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void AFootballBot::PaintBody(const FLinearColor& Color, UMaterialInterface* Material)
{
	UMaterialInterface* Base = (Material != nullptr) ? Material : PaintMaterial.Get();
	if (Base == nullptr)
	{
		return;
	}

	// One plain coloured material on every slot of the body mesh.
	UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(Base, this);
	Paint->SetVectorParameterValue(TEXT("Color"), Color);

	USkeletalMeshComponent* Body = GetMesh();
	for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
	{
		Body->SetMaterial(Slot, Paint);
	}
}

void AFootballBot::SetBotActive(bool bActive)
{
	bBotActive = bActive;
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
	SetActorTickEnabled(bActive);
	LeftFootGrassInteractor->bInteractorEnabled = bActive;
	RightFootGrassInteractor->bInteractorEnabled = bActive;
	if (!bActive)
	{
		StopMoving();
		GetCharacterMovement()->StopMovementImmediately();
	}
}

void AFootballBot::MoveToward(const FVector& Target, float Speed)
{
	MoveTarget = Target;
	bHasMoveTarget = true;
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(Speed, 0.f);
}

void AFootballBot::StopMoving()
{
	bHasMoveTarget = false;
}

void AFootballBot::PlaceAt(const FVector& GroundLocation, const FVector& LookAt)
{
	const double HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Location = GroundLocation + FVector(0.0, 0.0, HalfHeight + 2.0);

	FVector ToLookAt = LookAt - Location;
	ToLookAt.Z = 0.0;
	const FRotator Rotation = ToLookAt.IsNearlyZero() ? GetActorRotation() : ToLookAt.Rotation();

	GetCharacterMovement()->StopMovementImmediately();
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	bHasMoveTarget = false;
	FaceTarget = LookAt;
}

void AFootballBot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bBotActive)
	{
		return;
	}

	const FVector Location = GetActorLocation();

	if (bHasMoveTarget)
	{
		FVector ToTarget = MoveTarget - Location;
		ToTarget.Z = 0.0;
		const double Distance = ToTarget.Size();
		if (Distance > 25.0)
		{
			// Ease off just before arriving so the bot does not overshoot and jitter around the spot.
			const float Scale = (float)FMath::Clamp(Distance / 80.0, 0.3, 1.0);
			AddMovementInput(ToTarget / Distance, Scale);
			return;
		}
	}

	// Standing: turn towards whatever the manager wants watched (normally the ball).
	FVector ToFace = FaceTarget - Location;
	ToFace.Z = 0.0;
	if (!ToFace.IsNearlyZero())
	{
		const FRotator Current = GetActorRotation();
		const FRotator Wanted(0.0, ToFace.Rotation().Yaw, 0.0);
		SetActorRotation(FMath::RInterpTo(Current, Wanted, DeltaSeconds, 6.f));
	}
}
