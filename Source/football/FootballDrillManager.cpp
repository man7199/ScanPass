#include "FootballDrillManager.h"

#include "Animation/AnimInstance.h"
#include "BallKickComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FootballBot.h"
#include "FootballHUD.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GrassInteractorComponent.h"
#include "GrassRollingResistanceComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FootballDrillManager)

namespace FootballDrill
{
	/** Key for the on-screen message used when the drill cannot start at all. */
	static const int32 SetupMessageKey = 7101;

	static const float ResultMessageDuration = 2.2f;

	static TArray<TWeakObjectPtr<AFootballDrillManager>>& GetRegistry()
	{
		static TArray<TWeakObjectPtr<AFootballDrillManager>> Registry;
		return Registry;
	}

	static UPrimitiveComponent* FindPhysicsBody(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (Primitive != nullptr && Primitive->IsSimulatingPhysics())
			{
				return Primitive;
			}
		}
		return nullptr;
	}

	static double Dist2D(const FVector& A, const FVector& B)
	{
		return FVector::Dist2D(A, B);
	}
}

AFootballDrillManager::AFootballDrillManager()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	FriendlyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	EnemyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	BotAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")));
	BodyPaintMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Football/M_BodyPaint.M_BodyPaint")));
	HudClass = AFootballHUD::StaticClass();

	// Level 1 as it was, 1.5x on levels 2 and 3, 2x on level 4, 2.3x in the endless level.
	ReturnPassSpeedByLevel = { 1.f, 1.5f, 1.5f, 2.f, 2.3f };
}

AFootballDrillManager* AFootballDrillManager::Get(const UWorld* World)
{
	for (const TWeakObjectPtr<AFootballDrillManager>& Weak : FootballDrill::GetRegistry())
	{
		AFootballDrillManager* Manager = Weak.Get();
		if (Manager != nullptr && Manager->GetWorld() == World)
		{
			return Manager;
		}
	}
	return nullptr;
}

void AFootballDrillManager::BeginPlay()
{
	Super::BeginPlay();
	FootballDrill::GetRegistry().AddUnique(this);
	LivesLeft = FMath::Max(NumLives, 1);
}

void AFootballDrillManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FootballDrill::GetRegistry().RemoveSwap(this);
	Super::EndPlay(EndPlayReason);
}

float AFootballDrillManager::GetSpeedScale() const
{
	// Level 2 is the base speed and every set of passes after it adds a bit. The count carries on
	// through the endless level, so that one keeps getting quicker until the cap.
	const int32 TiersAboveFirst = FMath::Max(GetTier() - 1, 0);
	return FMath::Min(1.f + SpeedGainPerLevel * (float)TiersAboveFirst, MaxSpeedScale);
}

float AFootballDrillManager::GetReturnPassScale() const
{
	if (ReturnPassSpeedByLevel.Num() == 0)
	{
		return 1.f;
	}
	const int32 Index = FMath::Clamp(GetLevel(), 0, ReturnPassSpeedByLevel.Num() - 1);
	return FMath::Max(ReturnPassSpeedByLevel[Index], 0.1f);
}

float AFootballDrillManager::GetDecisionTime() const
{
	const float Passes = (float)PassesPlayed;
	const float StartPass = (float)ClockShrinkStartPass;
	const float MidPass = FMath::Max((float)ClockMidPass, StartPass + 1.f);
	const float MinPass = FMath::Max((float)ClockMinPass, MidPass + 1.f);

	if (Passes <= StartPass)
	{
		return DecisionTimeStart;
	}
	if (Passes <= MidPass)
	{
		return FMath::Lerp(DecisionTimeStart, DecisionTimeMid, (Passes - StartPass) / (MidPass - StartPass));
	}
	if (Passes <= MinPass)
	{
		return FMath::Lerp(DecisionTimeMid, DecisionTimeMin, (Passes - MidPass) / (MinPass - MidPass));
	}
	return DecisionTimeMin;
}

FVector AFootballDrillManager::GetBallLocation() const
{
	return BallBody->Bounds.Origin;
}

FVector AFootballDrillManager::GetBallVelocity() const
{
	return BallBody->GetPhysicsLinearVelocity();
}

void AFootballDrillManager::StopBall()
{
	BallBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
	BallBody->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
}

FVector AFootballDrillManager::GetPointOnBallPath(const FVector& Location) const
{
	const FVector BallLocation = GetBallLocation();
	FVector Velocity = GetBallVelocity();
	Velocity.Z = 0.0;
	const double Speed = Velocity.Size();
	if (Speed < 50.0)
	{
		return BallLocation;
	}

	// Rough distance the ball still has in it, so nobody runs to a point it will never reach.
	const double RemainingDistance = FMath::Clamp(Speed * Speed / 300.0 + 200.0, 200.0, 4500.0);
	const FVector Direction = Velocity / Speed;
	const double Along = FMath::Clamp(FVector::DotProduct(Location - BallLocation, Direction), 0.0, RemainingDistance);

	FVector Point = BallLocation + Direction * Along;
	Point.Z = GroundZ;
	return Point;
}

FVector AFootballDrillManager::GetPlayerGroundLocation() const
{
	const FVector Location = Player->GetActorLocation();
	return FVector(Location.X, Location.Y, GroundZ);
}

bool AFootballDrillManager::Initialize()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	Player = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (Player == nullptr)
	{
		return false;	// the player has not spawned yet, try again next frame
	}

	// First guess at the ground height, from the player's capsule. The trace below refines it; the
	// guess alone is wrong while the player is still dropping in from his start point.
	GroundZ = (float)Player->GetActorLocation().Z - Player->GetSimpleCollisionHalfHeight();

	// The ball: the one picked in the editor, or the nearest physics object.
	BallBody = FootballDrill::FindPhysicsBody(BallActor);
	if (BallBody == nullptr)
	{
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It == Player)
			{
				continue;
			}
			UPrimitiveComponent* Body = FootballDrill::FindPhysicsBody(*It);
			if (Body != nullptr)
			{
				const double DistSq = FVector::DistSquared(Body->Bounds.Origin, Player->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BallBody = Body;
				}
			}
		}
	}
	if (BallBody == nullptr)
	{
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(FootballDrill::SetupMessageKey, 10.f, FColor::Red, TEXT("Football drill: no ball found. Put a physics ball in the level or set Ball Actor on the drill manager."));
		}
		bGaveUp = true;
		return false;
	}

	// The ball has to slow down on the grass and stay on it when rolling. If it was set up without a
	// Grass Rolling Resistance component it would roll on almost for ever, so give it one.
	if (AActor* BallOwner = BallBody->GetOwner())
	{
		if (BallOwner->FindComponentByClass<UGrassRollingResistanceComponent>() == nullptr)
		{
			UGrassRollingResistanceComponent* Resistance = NewObject<UGrassRollingResistanceComponent>(BallOwner, TEXT("GrassRollingResistance"));
			BallOwner->AddInstanceComponent(Resistance);
			Resistance->RegisterComponent();
		}

		// Blades only bend around the ball and fit its shape if it carries a Grass Interactor. Put one at
		// the ball's centre: the push is measured flat across the pitch, so the ball's spin does not matter.
		if (BallOwner->FindComponentByClass<UGrassInteractorComponent>() == nullptr)
		{
			UGrassInteractorComponent* Interactor = NewObject<UGrassInteractorComponent>(BallOwner, TEXT("BallGrassInteractor"));
			Interactor->Radius = FMath::Max((float)BallBody->Bounds.BoxExtent.Z * 1.8f, 15.f);
			Interactor->SetupAttachment(BallBody);
			BallOwner->AddInstanceComponent(Interactor);
			Interactor->RegisterComponent();
		}
	}

	// The drill's own HUD: crosshair, power bar, lives and clock. Installed here so there is nothing
	// to set up in the editor. Turn off bInstallHud to keep the project's own HUD.
	if (bInstallHud && HudClass != nullptr)
	{
		PlayerController->ClientSetHUD(HudClass);
	}

	// Listen for the player's kicks. Without a Ball Kick component, passes are recognised from the ball's speed.
	if (UBallKickComponent* KickComponent = Player->FindComponentByClass<UBallKickComponent>())
	{
		KickComponent->OnBallKicked.AddUObject(this, &AFootballDrillManager::HandlePlayerKick);
		bKickEventsAvailable = true;
	}

	// Spawn both teams once; they are moved around from then on. One spare opponent for the later levels.
	const int32 FriendCount = FMath::Clamp(NumTeammates, 1, 11);
	const int32 EnemyCount = FMath::Clamp(NumOpponents + 1, 0, FriendCount);

	USkeletalMesh* FriendlyMeshAsset = FriendlyMesh.LoadSynchronous();
	USkeletalMesh* EnemyMeshAsset = EnemyMesh.LoadSynchronous();
	UClass* AnimClass = BotAnimClass.LoadSynchronous();
	UMaterialInterface* PaintMaterialAsset = BodyPaintMaterial.LoadSynchronous();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector SpawnLocation = Player->GetActorLocation() + FVector(0.0, 0.0, 200.0);

	for (int32 Index = 0; Index < FriendCount + EnemyCount; ++Index)
	{
		const bool bFriendly = Index < FriendCount;
		AFootballBot* Bot = World->SpawnActor<AFootballBot>(AFootballBot::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
		if (Bot == nullptr)
		{
			continue;
		}
		Bot->Setup(bFriendly ? EFootballTeam::Friendly : EFootballTeam::Enemy, bFriendly ? FriendlyMeshAsset : EnemyMeshAsset, AnimClass);
		if (bFriendly ? bPaintTeammates : bPaintEnemies)
		{
			Bot->PaintBody(bFriendly ? TeammateBodyColor : EnemyBodyColor, PaintMaterialAsset);
		}
		SpawnedBots.Add(Bot);

		FBotSlot Slot;
		Slot.Bot = Bot;
		(bFriendly ? Friends : Enemies).Add(Slot);
	}

	if (Friends.Num() == 0)
	{
		bGaveUp = true;
		return false;
	}

	// Music for the whole game. Marked as a UI sound so it carries on while the menus pause the game.
	if (BackgroundMusic != nullptr)
	{
		MusicComponent = UGameplayStatics::CreateSound2D(this, BackgroundMusic, MusicVolume, 1.f, 0.f, nullptr, false, false);
		if (MusicComponent != nullptr)
		{
			MusicComponent->bIsUISound = true;
			MusicComponent->Play();
		}
	}

	// A bot's pass sounds like the player's unless it has been given a sound of its own.
	if (BotKickSound == nullptr)
	{
		if (const UBallKickComponent* KickComponent = Player->FindComponentByClass<UBallKickComponent>())
		{
			BotKickSound = KickComponent->KickSound;
		}
	}

	bInitialized = true;
	LivesLeft = FMath::Max(NumLives, 1);
	UpdateGroundZ();

	if (bStartInMenu && bInstallHud)
	{
		// Everything is ready; the main menu decides when it starts.
		PutBotsAway();
		PutBallAtPlayersFeet();
		State = EDrillState::InMenu;
		StateTime = 0.f;
	}
	else
	{
		LayoutBots(INDEX_NONE, INDEX_NONE);
		GiveBallToPlayer(true);
	}
	return true;
}

void AFootballDrillManager::PutBotsAway()
{
	for (FBotSlot& Friend : Friends)
	{
		Friend.Bot->SetBotActive(false);
	}
	for (FBotSlot& Enemy : Enemies)
	{
		Enemy.Bot->SetBotActive(false);
	}
}

void AFootballDrillManager::StartFromMenu()
{
	RestartDrill();
}

void AFootballDrillManager::ReturnToMenu()
{
	if (!bInitialized)
	{
		return;
	}

	StopBall();
	PutBotsAway();
	HolderFriend = INDEX_NONE;
	HolderEnemy = INDEX_NONE;
	HudState.bActive = false;
	HudState.bGameOver = false;
	HudState.ResultMessageTimeLeft = 0.f;
	State = EDrillState::InMenu;
	StateTime = 0.f;
}

void AFootballDrillManager::RestartDrill()
{
	if (!bInitialized)
	{
		return;
	}

	PassesPlayed = 0;
	PassesCompleted = 0;
	LivesLeft = FMath::Max(NumLives, 1);
	HolderFriend = INDEX_NONE;
	HolderEnemy = INDEX_NONE;

	// Everybody back on the pitch (LayoutBots then decides which opponents this level uses).
	for (FBotSlot& Friend : Friends)
	{
		Friend.Bot->SetBotActive(true);
	}
	for (FBotSlot& Enemy : Enemies)
	{
		Enemy.Bot->SetBotActive(true);
	}

	UpdateGroundZ();
	LayoutBots(INDEX_NONE, INDEX_NONE);
	GiveBallToPlayer(true);
	ShowResult(EFootballDrillMessage::Go, FLinearColor(0.3f, 1.f, 0.4f));
}

void AFootballDrillManager::UpdateGroundZ()
{
	const UWorld* World = GetWorld();
	if (World == nullptr || Player == nullptr)
	{
		return;
	}

	// Straight down from the player, ignoring everything that is not the pitch.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FootballDrillGround), false);
	Params.AddIgnoredActor(Player);
	if (BallBody != nullptr)
	{
		Params.AddIgnoredActor(BallBody->GetOwner());
	}
	for (const AFootballBot* Bot : SpawnedBots)
	{
		Params.AddIgnoredActor(Bot);
	}

	const FVector Start = Player->GetActorLocation();
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, 3000.0), ECC_WorldStatic, Params))
	{
		GroundZ = (float)Hit.ImpactPoint.Z;
	}
}

FVector AFootballDrillManager::GetHoldLocation() const
{
	const FVector Forward = FRotator(0.0, Player->GetControlRotation().Yaw, 0.0).Vector();
	const double BallRadius = BallBody->Bounds.BoxExtent.Z;
	// Just clear of the player's capsule, so the two never push against each other.
	const double HoldDistance = (double)Player->GetSimpleCollisionRadius() + BallRadius + 15.0;
	return GetPlayerGroundLocation() + Forward * HoldDistance + FVector(0.0, 0.0, BallRadius + 0.5);
}

void AFootballDrillManager::PutBallAtPlayersFeet()
{
	const FVector BallCenterOffset = BallBody->Bounds.Origin - BallBody->GetComponentLocation();

	StopBall();
	BallBody->SetWorldLocation(GetHoldLocation() - BallCenterOffset, false, nullptr, ETeleportType::TeleportPhysics);
	StopBall();
	bBallSettled = false;
}

void AFootballDrillManager::HoldBall(float DeltaSeconds)
{
	// The ball glides the last bit to the player's feet and then stays with him. It is the same ball
	// that was passed to him: it is moved from where it is, never put somewhere new.
	const FVector Current = BallBody->Bounds.Origin;
	const FVector BallCenterOffset = Current - BallBody->GetComponentLocation();

	// Carry the ball along with the player's own movement first, then close the remaining gap. Without
	// that it would trail behind a running player and end up inside his legs.
	const FVector Carried = Current + Player->GetVelocity() * (double)DeltaSeconds;
	const FVector NewCenter = FMath::VInterpTo(Carried, GetHoldLocation(), DeltaSeconds, 16.f);

	BallBody->SetWorldLocation(NewCenter - BallCenterOffset, false, nullptr, ETeleportType::TeleportPhysics);
	StopBall();
}

void AFootballDrillManager::GiveBallToPlayer(bool bBringBall)
{
	if (bBringBall)
	{
		PutBallAtPlayersFeet();
	}
	else
	{
		StopBall();
	}

	HolderFriend = INDEX_NONE;
	HolderEnemy = INDEX_NONE;
	DecisionTimeTotal = GetDecisionTime();
	DecisionTimeLeft = DecisionTimeTotal;
	State = EDrillState::PlayerHasBall;
	StateTime = 0.f;
}

void AFootballDrillManager::LoseLife(EFootballDrillMessage Reason)
{
	LivesLeft = FMath::Max(LivesLeft - 1, 0);

	if (LivesLeft > 0)
	{
		ShowResult(Reason, FLinearColor(1.f, 0.35f, 0.25f), LivesLeft);
		return;
	}

	ShowResult(Reason, FLinearColor(1.f, 0.35f, 0.25f));

	// Out of lives: everything stops until the player asks for another run.
	StopBall();
	for (FBotSlot& Friend : Friends)
	{
		Friend.Bot->StopMoving();
	}
	for (FBotSlot& Enemy : Enemies)
	{
		Enemy.Bot->StopMoving();
	}

	State = EDrillState::GameOver;
	StateTime = 0.f;
}

void AFootballDrillManager::LayoutBots(int32 KeepFriend, int32 KeepEnemy)
{
	// Everything is laid out around the player, relative to where he is looking now.
	const FVector PlayerGround = GetPlayerGroundLocation();
	const double BaseYaw = Player->GetControlRotation().Yaw;
	const FVector BallLocation = GetBallLocation();
	const FVector BallGround(BallLocation.X, BallLocation.Y, GroundZ);

	const int32 NumFriends = Friends.Num();

	// One direction per team-mate. At 180 degrees they ring the player, so the spacing wraps all the
	// way round instead of doubling up at the two ends of a fan.
	const bool bFullCircle = SpreadHalfAngle >= 175.f;
	TArray<double> FanAngles;
	for (int32 Index = 0; Index < NumFriends; ++Index)
	{
		if (bFullCircle)
		{
			FanAngles.Add(360.0 * (double)Index / (double)NumFriends);
		}
		else
		{
			const double Fraction = (NumFriends > 1) ? (double)Index / (double)(NumFriends - 1) : 0.5;
			FanAngles.Add(FMath::Lerp(-(double)SpreadHalfAngle, (double)SpreadHalfAngle, Fraction));
		}
	}

	// The team-mate with the ball keeps his place. Leave out the direction he is nearest to, so nobody
	// is put on top of him.
	if (Friends.IsValidIndex(KeepFriend))
	{
		FBotSlot& Kept = Friends[KeepFriend];
		const FVector KeptLocation = Kept.Bot->GetActorLocation();
		Kept.Home = FVector(KeptLocation.X, KeptLocation.Y, GroundZ);
		Kept.WanderTarget = Kept.Home;

		const double KeptAngle = FRotator::NormalizeAxis((Kept.Home - PlayerGround).Rotation().Yaw - BaseYaw);
		int32 NearestAngle = 0;
		for (int32 Index = 1; Index < FanAngles.Num(); ++Index)
		{
			if (FMath::Abs(FRotator::NormalizeAxis(FanAngles[Index] - KeptAngle)) < FMath::Abs(FRotator::NormalizeAxis(FanAngles[NearestAngle] - KeptAngle)))
			{
				NearestAngle = Index;
			}
		}
		FanAngles.RemoveAt(NearestAngle);
	}

	// Hand the directions out at random so the same team-mate is not always on the same side.
	for (int32 Index = FanAngles.Num() - 1; Index > 0; --Index)
	{
		FanAngles.Swap(Index, FMath::RandRange(0, Index));
	}

	int32 NextAngle = 0;
	for (int32 Index = 0; Index < NumFriends; ++Index)
	{
		if (Index == KeepFriend || !FanAngles.IsValidIndex(NextAngle))
		{
			continue;
		}

		const double Spacing = bFullCircle ? (360.0 / (double)NumFriends) : (2.0 * (double)SpreadHalfAngle / (double)FMath::Max(NumFriends - 1, 1));
		const double Jitter = FMath::FRandRange(-0.2, 0.2) * Spacing;
		const double Angle = FanAngles[NextAngle++] + Jitter;

		// Some near, some far.
		const double Distance = FMath::FRandRange((double)MinPassDistance, (double)MaxPassDistance);

		FBotSlot& Friend = Friends[Index];
		Friend.Home = PlayerGround + FRotator(0.0, BaseYaw + Angle, 0.0).Vector() * Distance;
		Friend.WanderTarget = Friend.Home;
		Friend.NextWanderTime = 0.f;
		Friend.Bot->PlaceAt(Friend.Home, BallGround);
	}

	// Opponents: each marks a different team-mate, picked at random. On the early levels there is one
	// opponent fewer than team-mates, so somebody is free. Later everyone is marked, one of them loosely.
	const int32 WantedEnemies = IsEveryoneMarkedLevel() ? NumOpponents + 1 : NumOpponents;
	const int32 NumActiveEnemies = FMath::Clamp(WantedEnemies, 0, FMath::Min(Enemies.Num(), NumFriends));

	TArray<int32> FriendOrder;
	for (int32 Index = 0; Index < NumFriends; ++Index)
	{
		FriendOrder.Add(Index);
	}
	for (int32 Index = FriendOrder.Num() - 1; Index > 0; --Index)
	{
		FriendOrder.Swap(Index, FMath::RandRange(0, Index));
	}

	// The team-mate with the ball is always one of the marked ones, and marked tightly. Otherwise he
	// could be the free man twice in a row, and the player would just keep passing to him.
	if (Friends.IsValidIndex(KeepFriend))
	{
		FriendOrder.Swap(0, FriendOrder.IndexOfByKey(KeepFriend));
	}
	LastPasserFriend = INDEX_NONE;

	const bool bOneLoose = IsEveryoneMarkedLevel() && NumActiveEnemies > 0;
	int32 LooseEnemy = bOneLoose ? FMath::RandRange(0, NumActiveEnemies - 1) : INDEX_NONE;
	if (LooseEnemy == 0 && Friends.IsValidIndex(KeepFriend))
	{
		LooseEnemy = (NumActiveEnemies > 1) ? FMath::RandRange(1, NumActiveEnemies - 1) : INDEX_NONE;
	}

	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FBotSlot& Enemy = Enemies[Index];
		const bool bActive = Index < NumActiveEnemies || Index == KeepEnemy;
		Enemy.Bot->SetBotActive(bActive);
		if (!bActive || !FriendOrder.IsValidIndex(Index))
		{
			Enemy.MarkedFriend = INDEX_NONE;
			continue;
		}

		const bool bLoose = Index == LooseEnemy;
		Enemy.bClosingDown = false;
		Enemy.MarkedFriend = FriendOrder[Index];
		Enemy.MarkDistance = bLoose ? LooseMarkDistance : TightMarkDistance;
		Enemy.MarkSide = bLoose ? (float)FMath::FRandRange(-250.0, 250.0) : (float)FMath::FRandRange(-45.0, 45.0);

		// Whoever marks the team-mate with the ball stands off to one side, so the pass back has a clear path.
		if (Enemy.MarkedFriend == KeepFriend)
		{
			Enemy.MarkSide = (FMath::RandBool() ? 1.f : -1.f) * (EnemyReach + 90.f);
		}

		// Stand on the passing lane, between the player and the team-mate.
		const FVector FriendHome = Friends[Enemy.MarkedFriend].Home;
		const FVector ToPlayer = (PlayerGround - FriendHome).GetSafeNormal2D();
		const FVector Side(-ToPlayer.Y, ToPlayer.X, 0.0);
		Enemy.Home = FriendHome + ToPlayer * Enemy.MarkDistance + Side * Enemy.MarkSide;

		// The opponent with the ball stays where he won it.
		if (Index != KeepEnemy)
		{
			Enemy.Bot->PlaceAt(Enemy.Home, BallGround);
		}
	}
}

void AFootballDrillManager::HandlePlayerKick(UPrimitiveComponent* KickedBody, float PowerScale)
{
	if (!bInitialized || KickedBody != BallBody)
	{
		return;
	}

	// A first-time pass while the ball is still arriving counts just like one from a standing ball.
	if (State == EDrillState::PlayerHasBall || State == EDrillState::Returning)
	{
		BeginPass();
	}
}

void AFootballDrillManager::BeginPass()
{
	++PassesPlayed;
	HolderFriend = INDEX_NONE;
	HolderEnemy = INDEX_NONE;
	DecisionTimeLeft = 0.f;
	State = EDrillState::BallInPlay;
	StateTime = 0.f;
}

void AFootballDrillManager::ShowResult(EFootballDrillMessage Message, const FLinearColor& Color, int32 LivesLeftToShow)
{
	HudState.ResultMessage = Message;
	HudState.ResultLivesLeft = LivesLeftToShow;
	HudState.ResultColor = Color;
	HudState.ResultMessageTimeLeft = FootballDrill::ResultMessageDuration;
}

void AFootballDrillManager::UpdateBots(float DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const float Now = (float)World->GetTimeSeconds();
	const FVector BallLocation = GetBallLocation();
	const FVector BallGround(BallLocation.X, BallLocation.Y, GroundZ);
	const bool bMoving = IsMovingLevel();
	const bool bBallInPlay = State == EDrillState::BallInPlay;
	const bool bBotHasBall = State == EDrillState::BotHasBall;
	const bool bGameOver = State == EDrillState::GameOver;
	const float SpeedScale = GetSpeedScale();

	// Which team-mate is the pass heading for? The one closest to the ball's path goes to meet it.
	int32 TargetFriend = INDEX_NONE;
	if (bBallInPlay && bMoving)
	{
		double BestDistance = 600.0;	// nobody reacts to a ball passing further away than this
		for (int32 Index = 0; Index < Friends.Num(); ++Index)
		{
			const FVector BotLocation = Friends[Index].Bot->GetActorLocation();
			const double Distance = FootballDrill::Dist2D(BotLocation, GetPointOnBallPath(BotLocation));
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				TargetFriend = Index;
			}
		}
	}

	for (int32 Index = 0; Index < Friends.Num(); ++Index)
	{
		FBotSlot& Friend = Friends[Index];
		AFootballBot* Bot = Friend.Bot;

		// The one with the ball looks at the player he is about to pass to; everyone else watches the ball.
		const bool bHolder = bBotHasBall && Index == HolderFriend;
		Bot->SetFaceTarget(bHolder ? Player->GetActorLocation() : BallGround);

		if (!bMoving || bHolder || bGameOver)
		{
			Bot->StopMoving();
		}
		else if (Index == TargetFriend)
		{
			Bot->MoveToward(GetPointOnBallPath(Bot->GetActorLocation()), FriendlySpeed * SpeedScale * 1.3f);
		}
		else
		{
			// Drift around the home spot to offer a moving target.
			if (Now >= Friend.NextWanderTime)
			{
				const double Angle = FMath::FRandRange(0.0, 360.0);
				Friend.WanderTarget = Friend.Home + FRotator(0.0, Angle, 0.0).Vector() * FMath::FRandRange(100.0, 400.0);
				Friend.NextWanderTime = Now + FMath::FRandRange(1.5f, 3.5f);
			}
			Bot->MoveToward(Friend.WanderTarget, FriendlySpeed * SpeedScale);
		}
	}

	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FBotSlot& Enemy = Enemies[Index];
		AFootballBot* Bot = Enemy.Bot;
		if (!Bot->IsBotActive())
		{
			continue;
		}

		const bool bHolder = bBotHasBall && Index == HolderEnemy;
		Bot->SetFaceTarget(bHolder ? Player->GetActorLocation() : BallGround);

		// Even on the levels where nobody moves, the marker of the team-mate who has just passed back
		// still steps into the passing lane (see below).
		const bool bClosingDownCandidate = Enemy.MarkedFriend != INDEX_NONE && Enemy.MarkedFriend == LastPasserFriend
			&& (Enemy.bClosingDown || FMath::Abs(Enemy.MarkSide) > 60.f);

		if ((!bMoving && !bClosingDownCandidate) || bHolder || bGameOver || Enemy.MarkedFriend == INDEX_NONE)
		{
			Bot->StopMoving();
			continue;
		}

		const FVector BotLocation = Bot->GetActorLocation();

		if (bBallInPlay && bMoving)
		{
			// Try to cut the pass out, but only if it comes close enough to be worth leaving the man.
			const FVector PathPoint = GetPointOnBallPath(BotLocation);
			if (FootballDrill::Dist2D(BotLocation, PathPoint) < 700.0)
			{
				Bot->MoveToward(PathPoint, EnemySpeed * SpeedScale * 1.25f);
				continue;
			}
		}

		// Otherwise stay between the marked team-mate and the player, who is the one passing to him.
		const FVector FriendLocation = Friends[Enemy.MarkedFriend].Bot->GetActorLocation();
		const FVector FriendGround(FriendLocation.X, FriendLocation.Y, GroundZ);
		const FVector ToPlayer = (GetPlayerGroundLocation() - FriendGround).GetSafeNormal2D();
		const FVector Side(-ToPlayer.Y, ToPlayer.X, 0.0);

		// The team-mate who has just passed back was given room for that pass. Once the ball is past,
		// his marker steps straight back into the passing lane, so he is not free for the next pass too.
		if (Enemy.MarkedFriend == LastPasserFriend && FMath::Abs(Enemy.MarkSide) > 60.f)
		{
			const bool bBallPast = State != EDrillState::Returning
				|| FootballDrill::Dist2D(GetBallLocation(), FriendGround) > (double)Enemy.MarkDistance + 250.0;
			if (bBallPast)
			{
				Enemy.MarkDistance = TightMarkDistance;
				Enemy.MarkSide = (float)FMath::FRandRange(-30.0, 30.0);
				Enemy.bClosingDown = true;
			}
		}

		const FVector MarkSpot = FriendGround + ToPlayer * Enemy.MarkDistance + Side * Enemy.MarkSide;
		if (Enemy.bClosingDown && FootballDrill::Dist2D(BotLocation, MarkSpot) < 60.0)
		{
			Enemy.bClosingDown = false;
		}
		Bot->MoveToward(MarkSpot, EnemySpeed * SpeedScale * (Enemy.bClosingDown ? CloseDownSpeedScale : 1.f));
	}
}

void AFootballDrillManager::UpdateState(float DeltaSeconds)
{
	StateTime += DeltaSeconds;

	const FVector BallLocation = GetBallLocation();
	const FVector BallVelocity = GetBallVelocity();
	const FVector PlayerLocation = Player->GetActorLocation();
	const double BallSpeed = BallVelocity.Size2D();
	const double BallToPlayer = FootballDrill::Dist2D(BallLocation, PlayerLocation);

	switch (State)
	{
	case EDrillState::PlayerHasBall:
	{
		// On the clock. Hold on to it too long and it costs a life.
		DecisionTimeLeft -= DeltaSeconds;
		if (DecisionTimeLeft <= 0.f)
		{
			LoseLife(EFootballDrillMessage::TooSlow);
			if (State != EDrillState::GameOver)
			{
				LayoutBots(INDEX_NONE, INDEX_NONE);
				GiveBallToPlayer(false);	// he still has it
			}
			break;
		}

		// The player has the ball under control: it is held in front of his feet and goes where he
		// goes, so walking into it or turning cannot knock it away. It is only released by a kick.
		// (Without a Ball Kick component there is no kick to release it, so then it is left free.)
		if (bKickEventsAvailable)
		{
			HoldBall(DeltaSeconds);
		}
		else if (BallToPlayer > 400.0 || BallLocation.Z < GroundZ - 300.0)
		{
			PutBallAtPlayersFeet();
			break;
		}

		// Without a Ball Kick component to tell us, a pass is a ball that was calm and is now leaving quickly.
		if (!bKickEventsAvailable)
		{
			if (BallSpeed < 150.0)
			{
				bBallSettled = true;
			}
			else if (bBallSettled && BallSpeed > 400.0 && BallToPlayer > 200.0)
			{
				BeginPass();
			}
		}
		break;
	}

	case EDrillState::BallInPlay:
	{
		// First bot within reach of the ball gets it. A ball flying over a head does not count.
		const bool bBallLow = BallLocation.Z - GroundZ < 220.0;
		const float FriendReach = IsMovingLevel() ? FriendlyReach : FriendlyReachStanding;

		int32 BestFriend = INDEX_NONE;
		double BestFriendMargin = 0.0;
		for (int32 Index = 0; Index < Friends.Num(); ++Index)
		{
			const double Margin = (double)FriendReach - FootballDrill::Dist2D(BallLocation, Friends[Index].Bot->GetActorLocation());
			if (Margin > BestFriendMargin)
			{
				BestFriendMargin = Margin;
				BestFriend = Index;
			}
		}

		int32 BestEnemy = INDEX_NONE;
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			if (Enemies[Index].Bot->IsBotActive() && FootballDrill::Dist2D(BallLocation, Enemies[Index].Bot->GetActorLocation()) < (double)EnemyReach)
			{
				BestEnemy = Index;
				break;
			}
		}

		if (bBallLow && BestEnemy != INDEX_NONE)
		{
			// Intercepted: costs a life. The opponent gives it back, and everybody else moves.
			StopBall();
			HolderEnemy = BestEnemy;
			LoseLife(EFootballDrillMessage::Intercepted);
			if (State != EDrillState::GameOver)
			{
				LayoutBots(INDEX_NONE, HolderEnemy);
				State = EDrillState::BotHasBall;
				StateTime = 0.f;
			}
		}
		else if (bBallLow && BestFriend != INDEX_NONE)
		{
			// Received. Everybody except the receiver takes up new positions straight away.
			StopBall();
			++PassesCompleted;
			HolderFriend = BestFriend;
			ShowResult(EFootballDrillMessage::PassCompleted, FLinearColor(0.3f, 1.f, 0.4f));
			LayoutBots(HolderFriend, INDEX_NONE);
			State = EDrillState::BotHasBall;
			StateTime = 0.f;
		}
		else if (StateTime > 7.f || (StateTime > 1.f && BallSpeed < 40.0))
		{
			// Nobody got it: a wasted pass, so it costs a life too.
			LoseLife(EFootballDrillMessage::NobodyGotIt);
			if (State != EDrillState::GameOver)
			{
				// The ball is out on the pitch somewhere, so this is the one time it has to be fetched.
				LayoutBots(INDEX_NONE, INDEX_NONE);
				GiveBallToPlayer(true);
			}
		}
		break;
	}

	case EDrillState::BotHasBall:
	{
		// A moment to control it, then back to the player along the ground.
		if (StateTime > HoldTime)
		{
			FVector ToPlayer = PlayerLocation - BallLocation;
			ToPlayer.Z = 0.0;
			const double Distance = ToPlayer.Size();
			if (Distance > 1.0)
			{
				// Firmer the further it has to go, and firmer still on the later levels. Never so soft that
				// the grass stops it before it gets to the player.
				const double ReturnScale = (double)GetReturnPassScale();
				double PassSpeed = FMath::Clamp(Distance * 0.8 + 500.0, 900.0, 2400.0) * ReturnScale;

				if (const UGrassRollingResistanceComponent* Resistance = BallBody->GetOwner() ? BallBody->GetOwner()->FindComponentByClass<UGrassRollingResistanceComponent>() : nullptr)
				{
					const double ArrivalSpeed = 300.0 * ReturnScale;
					double Low = ArrivalSpeed;
					double High = 6000.0;
					for (int32 Iteration = 0; Iteration < 24; ++Iteration)
					{
						const double Mid = 0.5 * (Low + High);
						if (Resistance->GetRollDistance(Mid, ArrivalSpeed) < Distance)
						{
							Low = Mid;
						}
						else
						{
							High = Mid;
						}
					}
					PassSpeed = FMath::Max(PassSpeed, High);
				}

				// The marker of this team-mate gets back into the lane once the ball is past (see UpdateBots).
				LastPasserFriend = HolderFriend;

				// Along the ground, and held there, so the return pass does not hop either.
				UGrassRollingResistanceComponent::LaunchAlongGround(BallBody, (ToPlayer / Distance) * PassSpeed);

				if (BotKickSound != nullptr)
				{
					UGameplayStatics::PlaySoundAtLocation(this, BotKickSound, BallLocation, FRotator::ZeroRotator);
				}
			}
			State = EDrillState::Returning;
			StateTime = 0.f;
		}
		break;
	}

	case EDrillState::Returning:
	{
		// The pass arrives: the player takes that ball under control where it is, and from then on it is
		// held at his feet (HoldBall eases it the last few centimetres). The clock starts again.
		// (A first-time pass before any of that is handled in HandlePlayerKick.)
		if (BallToPlayer < 170.0)
		{
			GiveBallToPlayer(false);
		}
		else if (StateTime > 6.f || (StateTime > 1.f && BallSpeed < 40.0))
		{
			// A return pass that fell short is not the player's fault. Close by he just takes it;
			// from further away it is brought to him.
			GiveBallToPlayer(BallToPlayer > 350.0);
		}
		break;
	}

	case EDrillState::GameOver:
	{
		const APlayerController* PlayerController = Cast<APlayerController>(Player->GetController());
		if (PlayerController != nullptr && PlayerController->WasInputKeyJustPressed(RestartKey))
		{
			RestartDrill();
		}
		break;
	}

	default:
		break;
	}
}

void AFootballDrillManager::UpdateHudState(float DeltaSeconds)
{
	const int32 Level = GetLevel();

	HudState.bActive = true;
	HudState.bGameOver = State == EDrillState::GameOver;
	HudState.Level = Level;
	HudState.bEndlessLevel = IsEndlessLevel();
	HudState.LivesLeft = LivesLeft;
	HudState.MaxLives = FMath::Max(NumLives, 1);
	HudState.PassesPlayed = PassesPlayed;
	HudState.PassesCompleted = PassesCompleted;

	HudState.bOnTheClock = State == EDrillState::PlayerHasBall;
	HudState.DecisionTimeLeft = FMath::Max(DecisionTimeLeft, 0.f);
	HudState.DecisionTimeTotal = FMath::Max(DecisionTimeTotal, 0.01f);

	HudState.RestartKeyName = RestartKey.GetDisplayName().ToString();

	if (HudState.ResultMessageTimeLeft > 0.f)
	{
		HudState.ResultMessageTimeLeft = FMath::Max(HudState.ResultMessageTimeLeft - DeltaSeconds, 0.f);
	}
}

void AFootballDrillManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bGaveUp)
	{
		return;
	}
	if (!bInitialized && !Initialize())
	{
		return;
	}

	// The player or the ball can disappear (falling out of the world, level streaming). Stop quietly.
	if (Player == nullptr || BallBody == nullptr)
	{
		return;
	}

	UpdateGroundZ();

	// Waiting for the main menu: nothing to run.
	if (State == EDrillState::InMenu)
	{
		return;
	}

	UpdateState(DeltaSeconds);
	UpdateBots(DeltaSeconds);
	UpdateHudState(DeltaSeconds);
}
