#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "Templates/SubclassOf.h"
#include "FootballDrillManager.generated.h"

class AFootballBot;
class AHUD;
class UAnimInstance;
class UAudioComponent;
class UMaterialInterface;
class USoundBase;
class UPrimitiveComponent;
class USkeletalMesh;

/** The messages the drill flashes up. AFootballHUD turns them into text in the chosen language. */
enum class EFootballDrillMessage : uint8
{
	None,
	Go,
	TooSlow,
	Intercepted,
	NobodyGotIt,
	PassCompleted
};

/** Everything AFootballHUD needs to draw. Rebuilt by the drill manager every frame. */
struct FFootballDrillHudState
{
	bool bActive = false;			// the drill has found the player and the ball and is running
	bool bGameOver = false;
	int32 Level = 0;				// counting from 0
	bool bEndlessLevel = false;		// the last level, which never ends
	int32 LivesLeft = 3;
	int32 MaxLives = 3;
	int32 PassesPlayed = 0;
	int32 PassesCompleted = 0;

	/** The player has the ball and the clock is running. */
	bool bOnTheClock = false;
	float DecisionTimeLeft = 0.f;
	float DecisionTimeTotal = 0.f;

	EFootballDrillMessage ResultMessage = EFootballDrillMessage::None;
	int32 ResultLivesLeft = -1;		// 0 or more: the message also says how many lives are left
	FLinearColor ResultColor = FLinearColor::White;
	float ResultMessageTimeLeft = 0.f;

	FString RestartKeyName;			// key that starts a new run, as shown on screen
};

/**
 * Runs the passing drill. Drop one into the level and press Play.
 *
 * Play is continuous: you pass, a team-mate (blue) who gets to the ball passes it back, the ball stops
 * at your feet, and you pass again. Every reception puts everybody else in a new position, so the
 * picture you scanned a second ago is out of date and you have to look again.
 *
 * You have three lives. A pass nobody reaches, a pass an opponent (red) cuts out, or holding on to the
 * ball for too long costs one. At zero the drill ends and shows how many passes you completed.
 *
 * There are five levels, BallsPerLevel passes each. The fifth never ends.
 *   Level 1  Beginner      nobody moves. One opponent fewer than team-mates, so one team-mate is free.
 *   Level 2  Amateur       everybody moves: team-mates drift, opponents mark them and chase passes.
 *   Level 3  Semi-Pro      the same, faster.
 *   Level 4  Professional  one more opponent, so everyone is marked: all tightly except one. Scan for him.
 *   Level 5  Championship  endless. Every BallsPerLevel passes the bots get a little quicker, and the
 *                          time allowed on the ball starts to shrink: 3 s, down to 2 s, down to 1 s.
 * The pass a bot plays back to you also gets harder to deal with: ReturnPassSpeedByLevel.
 *
 * The drill waits in the main menu (drawn by AFootballHUD) until StartFromMenu is called.
 */
UCLASS()
class FOOTBALL_API AFootballDrillManager : public AActor
{
	GENERATED_BODY()

public:
	AFootballDrillManager();

	/** The ball. Leave empty to use the physics object nearest to the player when the game starts. */
	UPROPERTY(EditAnywhere, Category = "Drill")
	TObjectPtr<AActor> BallActor;

	/** Passes per level: passes 1 to 10 are level 1, 11 to 20 level 2, and so on. */
	UPROPERTY(EditAnywhere, Category = "Drill", meta = (ClampMin = "1"))
	int32 BallsPerLevel = 10;

	/** The last level, which goes on for ever. */
	UPROPERTY(EditAnywhere, Category = "Drill", meta = (ClampMin = "1"))
	int32 NumLevels = 5;

	/** Wait in the main menu when the game starts. Turn off to drop straight into the drill. */
	UPROPERTY(EditAnywhere, Category = "Drill")
	bool bStartInMenu = true;

	/** Mistakes allowed before the drill ends. */
	UPROPERTY(EditAnywhere, Category = "Drill", meta = (ClampMin = "1", ClampMax = "9"))
	int32 NumLives = 3;

	/** Key that starts a new run once the drill has ended. */
	UPROPERTY(EditAnywhere, Category = "Drill")
	FKey RestartKey = EKeys::R;

	/**
	 * Seconds on the ball. It stays at DecisionTimeStart until ClockShrinkStartPass passes have been
	 * played (by default the moment the endless level begins), shrinks evenly to DecisionTimeMid by
	 * ClockMidPass, then to DecisionTimeMin by ClockMinPass.
	 */
	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "0.3"))
	float DecisionTimeStart = 3.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "0.3"))
	float DecisionTimeMid = 2.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "0.3"))
	float DecisionTimeMin = 1.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "0"))
	int32 ClockShrinkStartPass = 40;

	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "1"))
	int32 ClockMidPass = 100;

	UPROPERTY(EditAnywhere, Category = "Drill|Clock", meta = (ClampMin = "1"))
	int32 ClockMinPass = 200;

	UPROPERTY(EditAnywhere, Category = "Drill|Teams", meta = (ClampMin = "1", ClampMax = "11"))
	int32 NumTeammates = 7;

	/** Opponents on the early levels. One more joins on level 4. Never more than there are team-mates. */
	UPROPERTY(EditAnywhere, Category = "Drill|Teams", meta = (ClampMin = "0", ClampMax = "11"))
	int32 NumOpponents = 6;

	/** Distance of the team-mates from the player, in cm. Each one picks a random distance in this range. */
	UPROPERTY(EditAnywhere, Category = "Drill|Layout")
	float MinPassDistance = 800.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Layout")
	float MaxPassDistance = 2600.f;

	/**
	 * Half of the angle the team-mates are spread over, in degrees. 180 puts them all the way around
	 * the player, which is what the scan is for; a smaller value keeps them in front.
	 */
	UPROPERTY(EditAnywhere, Category = "Drill|Layout", meta = (ClampMin = "10.0", ClampMax = "180.0"))
	float SpreadHalfAngle = 180.f;

	/** How far an opponent stands from the team-mate he marks tightly / loosely, in cm. */
	UPROPERTY(EditAnywhere, Category = "Drill|Layout")
	float TightMarkDistance = 200.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Layout")
	float LooseMarkDistance = 750.f;

	/** Speeds at level 2 in cm/s. They grow by SpeedGainPerLevel each level after that. */
	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	float FriendlySpeed = 260.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	float EnemySpeed = 300.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Bots", meta = (ClampMin = "0.0"))
	float SpeedGainPerLevel = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Drill|Bots", meta = (ClampMin = "1.0"))
	float MaxSpeedScale = 2.0f;

	/** How close the ball has to come for a bot to take it, in cm. Team-mates get more when nobody moves. */
	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	float FriendlyReach = 150.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	float FriendlyReachStanding = 220.f;

	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	float EnemyReach = 130.f;

	/** Seconds a bot holds the ball before passing it back. */
	UPROPERTY(EditAnywhere, Category = "Drill|Bots", meta = (ClampMin = "0.0"))
	float HoldTime = 0.7f;

	/**
	 * How hard the bots pass back, per level, as a multiple of the level 1 pace. One entry per level;
	 * levels beyond the end of the list use the last entry.
	 */
	UPROPERTY(EditAnywhere, Category = "Drill|Bots")
	TArray<float> ReturnPassSpeedByLevel;

	/** Looping music for the whole game. Tick "Looping" on the sound asset itself. Keeps playing in the menus. */
	UPROPERTY(EditAnywhere, Category = "Drill|Sound")
	TObjectPtr<USoundBase> BackgroundMusic;

	UPROPERTY(EditAnywhere, Category = "Drill|Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MusicVolume = 0.5f;

	/** Played when a bot passes the ball back. Leave empty to reuse the Kick Sound of the player's Ball Kick component. */
	UPROPERTY(EditAnywhere, Category = "Drill|Sound")
	TObjectPtr<USoundBase> BotKickSound;

	/** Looks of the two teams. Defaults point at the mannequins that came with the project template. */
	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	TSoftObjectPtr<USkeletalMesh> FriendlyMesh;

	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	TSoftObjectPtr<USkeletalMesh> EnemyMesh;

	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	TSoftClassPtr<UAnimInstance> BotAnimClass;

	/** Paint the opponents' whole body in EnemyBodyColor, so they stand out from the team-mates. */
	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	bool bPaintEnemies = true;

	UPROPERTY(EditAnywhere, Category = "Drill|Looks", meta = (EditCondition = "bPaintEnemies"))
	FLinearColor EnemyBodyColor = FLinearColor(0.75f, 0.03f, 0.02f);

	/**
	 * Material for the body paint: a vector parameter named "Color" into Base Color, and "Used with
	 * Skeletal Mesh" ticked in its details. Without that tick a packaged game shows the bots grey. The
	 * default path is in a folder that is always cooked (see DefaultGame.ini). If it does not exist, a
	 * built-in material is used, which works in the editor only.
	 */
	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	TSoftObjectPtr<UMaterialInterface> BodyPaintMaterial;

	/** The same for the team-mates. Off by default: they keep the mannequin's own look. */
	UPROPERTY(EditAnywhere, Category = "Drill|Looks")
	bool bPaintTeammates = false;

	UPROPERTY(EditAnywhere, Category = "Drill|Looks", meta = (EditCondition = "bPaintTeammates"))
	FLinearColor TeammateBodyColor = FLinearColor(0.03f, 0.15f, 0.75f);

	/**
	 * How fast an opponent closes down the team-mate who has just passed back to the player, as a
	 * multiple of the opponents' normal speed. Stops the player passing to the same free man again and again.
	 */
	UPROPERTY(EditAnywhere, Category = "Drill|Opponents", meta = (ClampMin = "1.0"))
	float CloseDownSpeedScale = 1.8f;

	/** Gives the player the drill HUD (crosshair, power bar, lives, clock) when the game starts. */
	UPROPERTY(EditAnywhere, Category = "Drill|HUD")
	bool bInstallHud = true;

	/** Which HUD to install. Defaults to AFootballHUD. */
	UPROPERTY(EditAnywhere, Category = "Drill|HUD", meta = (EditCondition = "bInstallHud"))
	TSubclassOf<AHUD> HudClass;

	/** The drill running in this world, if there is one. */
	static AFootballDrillManager* Get(const UWorld* World);

	const FFootballDrillHudState& GetHudState() const { return HudState; }

	/** Starts a fresh run: lives, level and score back to the beginning. */
	UFUNCTION(BlueprintCallable, Category = "Drill")
	void RestartDrill();

	/** Called by the main menu's Start button. */
	UFUNCTION(BlueprintCallable, Category = "Drill")
	void StartFromMenu();

	/** Ends the current run and puts everything away, ready for the main menu. */
	UFUNCTION(BlueprintCallable, Category = "Drill")
	void ReturnToMenu();

	/** True once the player and the ball have been found and both teams exist. */
	bool IsReady() const { return bInitialized; }

	/** True while the drill is waiting for the main menu. */
	bool IsWaitingInMenu() const { return State == EDrillState::InMenu; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EDrillState : uint8
	{
		NotStarted,
		InMenu,			// set up and waiting for the main menu's Start button
		PlayerHasBall,	// ball at the player's feet, clock running
		BallInPlay,		// passed, nobody has it yet
		BotHasBall,		// a bot has it and is about to give it back
		Returning,		// on its way back to the player
		GameOver
	};

	struct FBotSlot
	{
		AFootballBot* Bot = nullptr;				// kept alive by SpawnedBots
		FVector Home = FVector::ZeroVector;			// spot on the ground this bot belongs to for now
		FVector WanderTarget = FVector::ZeroVector;
		float NextWanderTime = 0.f;
		int32 MarkedFriend = INDEX_NONE;			// opponents: index of the team-mate they mark
		float MarkDistance = 0.f;
		float MarkSide = 0.f;						// sideways offset from the passing lane
		bool bClosingDown = false;					// opponents: hurrying back into the passing lane
	};

	/** Team-mate who last passed the ball back to the player, until the next layout. */
	int32 LastPasserFriend = INDEX_NONE;

	bool Initialize();
	/** New positions for everybody except the bot holding the ball (either index may be INDEX_NONE). */
	void LayoutBots(int32 KeepFriend, int32 KeepEnemy);
	/** Finds the ground under the player with a trace, so nothing is placed in mid air. */
	void UpdateGroundZ();
	/** Where the centre of the ball sits while the player has it. */
	FVector GetHoldLocation() const;
	/** Puts the ball at the player's feet instantly. Only for when it is nowhere near him. */
	void PutBallAtPlayersFeet();
	/** Keeps the ball at the player's feet while he has it, easing it in from wherever it arrived. */
	void HoldBall(float DeltaSeconds);
	/**
	 * The player has the ball and the clock starts. With bBringBall the ball is fetched to his feet
	 * (start of the drill, or after it was lost); without, he takes over the ball where it is.
	 */
	void GiveBallToPlayer(bool bBringBall);
	void HandlePlayerKick(UPrimitiveComponent* KickedBody, float PowerScale);
	void BeginPass();
	/** Takes a life, shows why, and ends the drill if that was the last one. */
	void LoseLife(EFootballDrillMessage Reason);
	void UpdateBots(float DeltaSeconds);
	void UpdateState(float DeltaSeconds);
	void UpdateHudState(float DeltaSeconds);
	void ShowResult(EFootballDrillMessage Message, const FLinearColor& Color, int32 LivesLeftToShow = -1);

	/** How many full sets of BallsPerLevel passes have been played. Keeps counting in the endless level. */
	int32 GetTier() const { return PassesPlayed / FMath::Max(BallsPerLevel, 1); }
	/** Level, counting from 0, capped at the endless level. */
	int32 GetLevel() const { return FMath::Min(GetTier(), FMath::Max(NumLevels, 1) - 1); }
	bool IsEndlessLevel() const { return GetLevel() >= FMath::Max(NumLevels, 1) - 1; }
	bool IsMovingLevel() const { return GetLevel() >= 1; }
	bool IsEveryoneMarkedLevel() const { return GetLevel() >= 3; }
	float GetReturnPassScale() const;
	void PutBotsAway();
	float GetSpeedScale() const;
	float GetDecisionTime() const;
	FVector GetPlayerGroundLocation() const;
	FVector GetBallLocation() const;
	FVector GetBallVelocity() const;
	void StopBall();
	/** Closest point to Location on the path the ball is expected to roll along. */
	FVector GetPointOnBallPath(const FVector& Location) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AFootballBot>> SpawnedBots;	// keeps the bots referenced for the garbage collector

	UPROPERTY(Transient)
	TObjectPtr<APawn> Player;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> BallBody;

	TArray<FBotSlot> Friends;
	TArray<FBotSlot> Enemies;

	FFootballDrillHudState HudState;

	EDrillState State = EDrillState::NotStarted;
	float StateTime = 0.f;			// seconds spent in the current state
	float GroundZ = 0.f;
	float DecisionTimeLeft = 0.f;
	float DecisionTimeTotal = 0.f;
	int32 PassesPlayed = 0;
	int32 PassesCompleted = 0;
	int32 LivesLeft = 3;
	int32 HolderFriend = INDEX_NONE;	// team-mate holding the ball
	int32 HolderEnemy = INDEX_NONE;		// opponent holding the ball
	bool bKickEventsAvailable = false;	// the player has a Ball Kick component we can listen to
	bool bBallSettled = false;			// used to spot a pass when there is no Ball Kick component
	bool bInitialized = false;
	bool bGaveUp = false;
};
