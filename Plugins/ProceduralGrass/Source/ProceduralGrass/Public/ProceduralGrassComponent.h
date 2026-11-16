#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ProceduralGrassComponent.generated.h"

class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FGrassSettings;

/**
 * Renders a rectangular field of procedural grass, centred on the component and lying in its local XY plane.
 *
 * Blades are generated on the GPU every frame by a compute shader (position, clumping, mowing stripes,
 * wind, interaction, LOD and frustum culling) and drawn with a custom vertex factory that builds each
 * blade from a cubic Bezier curve. The approach follows "Procedural Grass in Ghost of Tsushima" (GDC 2021).
 *
 * All distances are in component local units, so keep the component scale at 1.
 */
UCLASS(Blueprintable, ClassGroup = Rendering, meta = (BlueprintSpawnableComponent), hideCategories = (Activation, Collision, Cooking, HLOD, Navigation, Mobility, Object, Physics, VirtualTexture))
class PROCEDURALGRASS_API UProceduralGrassComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UProceduralGrassComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * Material for the blades. Press "Create Default Material" to get a ready made one.
	 * For your own material, use Vertex Color as Base Color (the component already bakes the root/tip
	 * gradient, brightness variation and stripes into it). Should be Two Sided. The material usage flag
	 * "Used with Virtual Heightfield Mesh" is borrowed to compile the grass shaders; the editor sets it
	 * automatically the first time the material is used here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	// ---- Layout ----

	/** Size of the field in cm. Default is a 105 m x 68 m football pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Layout")
	FVector2D FieldSize = FVector2D(10500.0, 6800.0);

	/** Size of one grass tile in cm. Tiles are culled and given a LOD on the CPU. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Layout", meta = (ClampMin = "50.0"))
	float TileSize = 280.f;

	/** Blades along one side of a tile at the highest LOD. Rounded up to a power of two. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Layout", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 BladesPerTileSide = 256;

	// ---- LOD ----

	/** Number of LODs. Each LOD keeps a quarter of the blades of the one before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|LOD", meta = (ClampMin = "1", ClampMax = "6"))
	int32 NumLods = 5;

	/** Distance in cm up to which every blade is drawn. Each following LOD covers twice the distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|LOD", meta = (ClampMin = "50.0"))
	float Lod0Distance = 400.f;

	/** Blades fade out towards this distance in cm. The ground under the grass should carry a matching colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|LOD", meta = (ClampMin = "100.0"))
	float MaxDrawDistance = 9000.f;

	/** How much blades widen per LOD to make up for the blades that were dropped. 1 doubles the width per LOD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|LOD", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float WidthLodScale = 0.8f;

	// ---- Blade shape ----

	/** Blade length in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.1"))
	float BladeHeight = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightVariation = 0.15f;

	/** Blade width at the root in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.01"))
	float BladeWidth = 1.3f;

	/** How far blades lean over. 0 is vertical, 1 is flat on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Tilt = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TiltVariation = 0.15f;

	/** Curvature of the blade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Bend = 0.25f;

	/** Tilts the normal outwards at the blade edges so flat blades shade as if they were rounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalRounding = 0.35f;

	/** Widens blades that are seen edge-on so they do not shrink below a pixel. 0 turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Blade", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float ViewThickening = 1.f;

	// ---- Clumping ----

	/** Average distance between clump centres in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Clumping", meta = (ClampMin = "1.0"))
	float ClumpSize = 30.f;

	/** How strongly blades are pulled towards their clump centre. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Clumping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClumpPull = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Clumping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClumpHeightVariation = 0.08f;

	/** How strongly blades of one clump share a facing direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Clumping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClumpFacing = 0.3f;

	// ---- Pitch look ----

	/** Width of one mowing stripe in cm. Stripes alternate along local X. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch", meta = (ClampMin = "10.0"))
	float StripeWidth = 550.f;

	/**
	 * How strongly the whole pitch is combed one way (the same for every stripe). Adds a little sheen
	 * that changes with the viewing direction. Keep it low; the stripes themselves come from Stripe Contrast.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MowStrength = 0.15f;

	/** Brightness difference between neighbouring mowing stripes. The same from every viewing direction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StripeContrast = 0.25f;

	/** Random brightness variation per clump and per blade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorVariation = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch")
	FLinearColor RootColor = FLinearColor(0.012f, 0.045f, 0.008f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Pitch")
	FLinearColor TipColor = FLinearColor(0.07f, 0.20f, 0.03f);

	// ---- Wind ----

	/** Wind direction in the component's local XY plane. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind")
	FVector2D WindDirection = FVector2D(1.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WindStrength = 0.15f;

	/** Speed in cm/s at which gusts travel across the field. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind", meta = (ClampMin = "0.0"))
	float WindSpeed = 300.f;

	/** Size of the gusts. Smaller values give larger gusts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind", meta = (ClampMin = "0.00001"))
	float WindNoiseScale = 0.002f;

	/** How much blades bob in the wind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float BobAmplitude = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Wind", meta = (ClampMin = "0.0"))
	float BobSpeed = 4.f;

	// ---- Interaction ----

	/**
	 * When on, the grass looks for Grass Interactor components in the level every frame and bends around
	 * the (up to 16) ones closest to the ground. Add a Grass Interactor to the ball and to each foot of a
	 * character and it just works. Turn this off if you want to drive the grass yourself with Set Interactors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Interaction")
	bool bAutoCollectInteractors = true;

	/**
	 * Presses the grass down under the exact shape of every actor that has a Grass Interactor component,
	 * so blades never poke through a foot, a ball or a sliding player. Works by capturing the depth of
	 * those actors from below the pitch each frame (game only, not in the editor viewport).
	 * Assumes the pitch is horizontal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass|Interaction")
	bool bConformToObjects = true;

	/** Side length in cm of the square around the player in which grass conforms to objects. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Interaction", meta = (ClampMin = "200.0", EditCondition = "bConformToObjects"))
	float ConformAreaSize = 2000.f;

	/** Resolution of the depth capture. Area size / resolution is the precision of the fit (default about 2 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grass|Interaction", meta = (ClampMin = "128", ClampMax = "2048", EditCondition = "bConformToObjects"))
	int32 ConformResolution = 1024;

	/**
	 * Objects that push the grass down, for example the ball and the players' feet. Up to 16 are used.
	 * Only needed when Auto Collect Interactors is off. Call this every frame the objects move.
	 * @param WorldLocations	Centres of the objects in world space.
	 * @param Radius			Radius of influence in cm.
	 */
	UFUNCTION(BlueprintCallable, Category = "Grass")
	void SetInteractors(const TArray<FVector>& WorldLocations, float Radius = 40.f);

	UFUNCTION(BlueprintCallable, Category = "Grass")
	void ClearInteractors();

#if WITH_EDITOR
	/**
	 * Editor button. Creates /Game/Grass/M_ProceduralGrass (or reuses it if it already exists) and assigns it.
	 * The material is built in C++, see GrassDefaultMaterial.cpp. Save it afterwards with File > Save All.
	 */
	UFUNCTION(CallInEditor, Category = "Grass")
	void CreateDefaultMaterial();
#endif

	/** Copies the properties into the struct used by the renderer. */
	void BuildSettings(FGrassSettings& OutSettings) const;

	/** The grass field under a world position, if any. Used by gameplay such as rolling resistance. */
	static UProceduralGrassComponent* FindGrassAt(const UWorld* World, const FVector& WorldLocation, float& OutHeightAboveGround);

	/**
	 * Current interactors in component local space, two entries each: (start xyz, radius) and
	 * (end xyz, vertical reach). Start and end are the same point for a sphere.
	 */
	const TArray<FVector4f>& GetLocalInteractors() const { return LocalInteractors; }

	static constexpr int32 MaxInteractors = 16;

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	//~ Begin UPrimitiveComponent Interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial) override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override { return Material; }
	virtual int32 GetNumMaterials() const override { return 1; }
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	//~ End UPrimitiveComponent Interface

private:
	void UpdateConformCapture();
	void DestroyConformCapture();
	void CollectInteractors();
	void SetLocalInteractors(TArray<FVector4f>&& NewInteractors);
	void SendInteractorsToRenderThread();

	TArray<FVector4f> LocalInteractors;

	/** Looks up at the interacting actors from below the pitch. Created at runtime, never saved. */
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> ConformCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ConformDepthTarget;
};
