#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "FootballBot.generated.h"

class UGrassInteractorComponent;
class UMaterialInterface;
class UStaticMeshComponent;
class USkeletalMesh;

UENUM(BlueprintType)
enum class EFootballTeam : uint8
{
	Friendly,
	Enemy
};

/**
 * A simple AI footballer. It has no brain of its own: AFootballDrillManager tells it where to go and
 * what to look at each frame, and this class turns that into movement.
 *
 * It walks with the normal character movement component and has no controller.
 */
UCLASS()
class FOOTBALL_API AFootballBot : public ACharacter
{
	GENERATED_BODY()

public:
	AFootballBot();

	/** Team, look (mesh and animation) and marker colour. */
	void Setup(EFootballTeam InTeam, USkeletalMesh* InMesh, UClass* InAnimClass);

	/**
	 * Paints the whole body in one colour, so the team can be told apart at a glance. Call after Setup.
	 * Material needs a "Color" vector parameter and "Used with Skeletal Mesh" ticked, or a packaged game
	 * draws the body in the default grey instead. Null uses the marker's material, which is fine in the
	 * editor only.
	 */
	void PaintBody(const FLinearColor& Color, UMaterialInterface* Material = nullptr);

	/** Hidden, non-colliding and frozen when inactive. Used for the fourth opponent before it joins. */
	void SetBotActive(bool bActive);
	bool IsBotActive() const { return bBotActive; }

	/** Walk to a spot on the ground at the given speed (cm/s). */
	void MoveToward(const FVector& Target, float Speed);
	void StopMoving();

	/** Turn to face this point while standing still. */
	void SetFaceTarget(const FVector& Target) { FaceTarget = Target; }

	/** Put the bot somewhere instantly. Location is a point on the ground. */
	void PlaceAt(const FVector& GroundLocation, const FVector& LookAt);

	EFootballTeam GetTeam() const { return Team; }

	virtual void Tick(float DeltaSeconds) override;

private:
	/** Coloured ball above the head: blue for team-mates, red for opponents. */
	UPROPERTY(VisibleAnywhere, Category = "Bot")
	TObjectPtr<UStaticMeshComponent> TeamMarker;

	/** Plain material with a "Color" parameter, used for the marker and for PaintBody. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PaintMaterial;

	/** Lets the grass press down under this bot's feet. */
	UPROPERTY(VisibleAnywhere, Category = "Bot")
	TObjectPtr<UGrassInteractorComponent> LeftFootGrassInteractor;
	TObjectPtr<UGrassInteractorComponent> RightFootGrassInteractor;

	EFootballTeam Team = EFootballTeam::Friendly;
	FVector MoveTarget = FVector::ZeroVector;
	FVector FaceTarget = FVector::ZeroVector;
	bool bHasMoveTarget = false;
	bool bBotActive = true;
};
