#include "ProceduralGrassComponent.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/CollisionProfile.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GrassInteractorComponent.h"
#include "GrassSceneProxy.h"
#include "GrassSettings.h"
#include "Materials/MaterialInterface.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ProceduralGrassComponent)

UProceduralGrassComponent::UProceduralGrassComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Individual blades do not go into shadow maps (same choice as Ghost of Tsushima). They still receive shadows.
	CastShadow = false;
	bCastContactShadow = false;
	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;
	bNeverDistanceCull = true;
	bEnableAutoLODGeneration = false;
	Mobility = EComponentMobility::Static;

	// Ticks late in the frame so the ball and the animated feet are already at their final positions.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
}

void UProceduralGrassComponent::BuildSettings(FGrassSettings& Out) const
{
	Out.FieldSize = FVector2f((float)FMath::Max(FieldSize.X, 1.0), (float)FMath::Max(FieldSize.Y, 1.0));
	Out.TileSize = FMath::Max(TileSize, 50.f);
	Out.NumLods = FMath::Clamp(NumLods, 1, 6);

	// Power of two, and large enough that the lowest LOD still has at least one blade per tile side.
	const int32 MinBlades = 1 << (Out.NumLods - 1);
	Out.BladesPerTileSide = (int32)FMath::RoundUpToPowerOfTwo((uint32)FMath::Clamp(BladesPerTileSide, MinBlades, 1024));

	Out.Lod0Distance = FMath::Max(Lod0Distance, 50.f);
	Out.MaxDrawDistance = FMath::Max(MaxDrawDistance, 100.f);
	Out.WidthLodScale = WidthLodScale;

	Out.BladeHeight = BladeHeight;
	Out.HeightVariation = HeightVariation;
	Out.BladeWidth = BladeWidth;
	Out.Tilt = Tilt;
	Out.TiltVariation = TiltVariation;
	Out.Bend = Bend;

	Out.ClumpSize = FMath::Max(ClumpSize, 1.f);
	Out.ClumpPull = ClumpPull;
	Out.ClumpHeightVariation = ClumpHeightVariation;
	Out.ClumpFacing = ClumpFacing;

	Out.StripeWidth = FMath::Max(StripeWidth, 10.f);
	Out.MowStrength = MowStrength;
	Out.StripeContrast = StripeContrast;
	Out.ColorVariation = ColorVariation;
	Out.RootColor = RootColor;
	Out.TipColor = TipColor;

	Out.WindDirection = FVector2f(WindDirection.GetSafeNormal());
	Out.WindStrength = WindStrength;
	Out.WindSpeed = WindSpeed;
	Out.WindNoiseScale = WindNoiseScale;
	Out.BobAmplitude = BobAmplitude;
	Out.BobSpeed = BobSpeed;

	Out.NormalRounding = NormalRounding;
	Out.ViewThickening = ViewThickening;
}

FPrimitiveSceneProxy* UProceduralGrassComponent::CreateSceneProxy()
{
	return new FGrassSceneProxy(this);
}

FBoxSphereBounds UProceduralGrassComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FGrassSettings Settings;
	BuildSettings(Settings);

	const FVector HalfSize((double)Settings.FieldSize.X * 0.5, (double)Settings.FieldSize.Y * 0.5, 0.0);
	const FBox LocalBox(-HalfSize, HalfSize + FVector(0.0, 0.0, (double)Settings.GetMaxBladeHeight() * 1.5 + 1.0));
	return FBoxSphereBounds(LocalBox).TransformBy(LocalToWorld);
}

void UProceduralGrassComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	if (ElementIndex == 0 && Material != InMaterial)
	{
		Material = InMaterial;
		MarkRenderStateDirty();
	}
}

void UProceduralGrassComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (Material != nullptr)
	{
		OutMaterials.Add(Material);
	}
}

void UProceduralGrassComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAutoCollectInteractors)
	{
		CollectInteractors();
	}

	UpdateConformCapture();
}

namespace ProceduralGrass
{
	/** How far below the pitch the conform capture camera sits. */
	static const float ConformCameraDistance = 500.f;

	static TArray<TWeakObjectPtr<UProceduralGrassComponent>>& GetGrassRegistry()
	{
		static TArray<TWeakObjectPtr<UProceduralGrassComponent>> Registry;
		return Registry;
	}
}

void UProceduralGrassComponent::OnRegister()
{
	Super::OnRegister();
	ProceduralGrass::GetGrassRegistry().AddUnique(this);
}

void UProceduralGrassComponent::OnUnregister()
{
	ProceduralGrass::GetGrassRegistry().RemoveSwap(this);
	DestroyConformCapture();
	Super::OnUnregister();
}

UProceduralGrassComponent* UProceduralGrassComponent::FindGrassAt(const UWorld* World, const FVector& WorldLocation, float& OutHeightAboveGround)
{
	for (const TWeakObjectPtr<UProceduralGrassComponent>& WeakGrass : ProceduralGrass::GetGrassRegistry())
	{
		UProceduralGrassComponent* Grass = WeakGrass.Get();
		if (Grass == nullptr || Grass->GetWorld() != World)
		{
			continue;
		}

		const FVector Local = Grass->GetComponentTransform().InverseTransformPosition(WorldLocation);
		if (FMath::Abs(Local.X) <= Grass->FieldSize.X * 0.5 && FMath::Abs(Local.Y) <= Grass->FieldSize.Y * 0.5)
		{
			OutHeightAboveGround = (float)Local.Z;
			return Grass;
		}
	}
	return nullptr;
}

void UProceduralGrassComponent::DestroyConformCapture()
{
	if (ConformCapture != nullptr)
	{
		ConformCapture->DestroyComponent();
		ConformCapture = nullptr;
	}
	ConformDepthTarget = nullptr;
}

void UProceduralGrassComponent::UpdateConformCapture()
{
	UWorld* World = GetWorld();
	const bool bWanted = bConformToObjects && World != nullptr && World->IsGameWorld() && GetOwner() != nullptr;

	FGrassConformData ConformData;

	if (!bWanted)
	{
		DestroyConformCapture();
	}
	else
	{
		const int32 Resolution = FMath::Clamp(ConformResolution, 128, 2048);
		const float AreaSize = FMath::Max(ConformAreaSize, 200.f);

		if (ConformDepthTarget == nullptr || ConformDepthTarget->SizeX != Resolution)
		{
			// One float per texel: the distance from the capture camera to the lowest surface above it.
			ConformDepthTarget = NewObject<UTextureRenderTarget2D>(this);
			ConformDepthTarget->RenderTargetFormat = RTF_R32f;
			ConformDepthTarget->ClearColor = FLinearColor::Black;
			ConformDepthTarget->bAutoGenerateMips = false;
			ConformDepthTarget->InitAutoFormat(Resolution, Resolution);
			ConformDepthTarget->UpdateResourceImmediate(true);
		}

		if (ConformCapture == nullptr)
		{
			ConformCapture = NewObject<USceneCaptureComponent2D>(GetOwner());
			ConformCapture->ProjectionType = ECameraProjectionMode::Orthographic;
			ConformCapture->CaptureSource = SCS_SceneDepth;
			// Only the actors that carry a Grass Interactor are drawn, so the ground and the grass itself are not in the way.
			ConformCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			ConformCapture->bCaptureEveryFrame = true;
			ConformCapture->bCaptureOnMovement = false;
			ConformCapture->bUseRayTracingIfEnabled = false;

			// Only depth is needed, so switch off everything expensive.
			ConformCapture->ShowFlags.DisableAdvancedFeatures();
			ConformCapture->ShowFlags.SetDynamicShadows(false);
			ConformCapture->ShowFlags.SetGlobalIllumination(false);
			ConformCapture->ShowFlags.SetLumenGlobalIllumination(false);
			ConformCapture->ShowFlags.SetLumenReflections(false);
			ConformCapture->ShowFlags.SetAmbientOcclusion(false);
			ConformCapture->ShowFlags.SetFog(false);
			ConformCapture->ShowFlags.SetVolumetricFog(false);
			ConformCapture->ShowFlags.SetAtmosphere(false);
			ConformCapture->ShowFlags.SetSkyLighting(false);
			ConformCapture->ShowFlags.SetTranslucency(false);
			ConformCapture->ShowFlags.SetDecals(false);

			ConformCapture->RegisterComponent();
		}

		ConformCapture->TextureTarget = ConformDepthTarget;
		ConformCapture->OrthoWidth = AreaSize;

		// Every actor with an enabled Grass Interactor presses the grass with its real shape.
		ConformCapture->ShowOnlyActors.Reset();
		for (const TWeakObjectPtr<UGrassInteractorComponent>& WeakInteractor : UGrassInteractorComponent::GetAllInteractors())
		{
			const UGrassInteractorComponent* Interactor = WeakInteractor.Get();
			if (Interactor != nullptr && Interactor->GetWorld() == World && Interactor->bInteractorEnabled && Interactor->GetOwner() != nullptr)
			{
				ConformCapture->ShowOnlyActors.AddUnique(Interactor->GetOwner());
			}
		}

		// Centre the capture on the viewer, snapped to whole texels so the result does not shimmer as they move.
		// In play that is the player's own camera; with a recording camera (ABallFollowCamera) as the view
		// target, the fitting follows that camera instead of staying around the player.
		const FTransform& GrassTransform = GetComponentTransform();
		FVector FocusWorld = GrassTransform.GetLocation();
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FRotator ViewRotation;
			PlayerController->GetPlayerViewPoint(FocusWorld, ViewRotation);
		}

		const double TexelSize = (double)AreaSize / (double)Resolution;
		FVector FocusLocal = GrassTransform.InverseTransformPosition(FocusWorld);
		FocusLocal.X = FMath::RoundToDouble(FocusLocal.X / TexelSize) * TexelSize;
		FocusLocal.Y = FMath::RoundToDouble(FocusLocal.Y / TexelSize) * TexelSize;
		FocusLocal.Z = 0.0;

		// The camera sits under the pitch and looks straight up along the pitch normal.
		const FVector PitchUp = GrassTransform.GetUnitAxis(EAxis::Z);
		const FVector CenterWorld = GrassTransform.TransformPosition(FocusLocal);
		const FVector CameraWorld = CenterWorld - PitchUp * ProceduralGrass::ConformCameraDistance;
		ConformCapture->SetWorldLocationAndRotation(CameraWorld, FRotationMatrix::MakeFromX(PitchUp).Rotator());

		// Image U runs along the camera's right vector and V runs down, opposite to its up vector.
		// Both are linear in the local position, so they reduce to one plane equation each.
		const FVector CaptureRight = ConformCapture->GetRightVector();
		const FVector CaptureUp = ConformCapture->GetUpVector();
		auto ComputeU = [&](const FVector& Local) { return FVector::DotProduct(GrassTransform.TransformPosition(Local) - CameraWorld, CaptureRight) / AreaSize + 0.5; };
		auto ComputeV = [&](const FVector& Local) { return 0.5 - FVector::DotProduct(GrassTransform.TransformPosition(Local) - CameraWorld, CaptureUp) / AreaSize; };

		const double U0 = ComputeU(FVector::ZeroVector);
		const double V0 = ComputeV(FVector::ZeroVector);
		ConformData.UPlane = FVector4f((float)(ComputeU(FVector::XAxisVector) - U0), (float)(ComputeU(FVector::YAxisVector) - U0), (float)(ComputeU(FVector::ZAxisVector) - U0), (float)U0);
		ConformData.VPlane = FVector4f((float)(ComputeV(FVector::XAxisVector) - V0), (float)(ComputeV(FVector::YAxisVector) - V0), (float)(ComputeV(FVector::ZAxisVector) - V0), (float)V0);
		ConformData.DepthResource = ConformDepthTarget->GetResource();
		ConformData.CameraDistance = ProceduralGrass::ConformCameraDistance;
		ConformData.bEnabled = ConformData.DepthResource != nullptr && ConformCapture->ShowOnlyActors.Num() > 0;
	}

	if (SceneProxy != nullptr)
	{
		FGrassSceneProxy* GrassProxy = static_cast<FGrassSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(ProceduralGrassSetConformData)(
			[GrassProxy, ConformData](FRHICommandListImmediate&)
			{
				GrassProxy->SetConformData_RenderThread(ConformData);
			});
	}
}

void UProceduralGrassComponent::CollectInteractors()
{
	FGrassSettings Settings;
	BuildSettings(Settings);

	const FTransform& ComponentTransform = GetComponentTransform();
	const float HalfSizeX = Settings.FieldSize.X * 0.5f;
	const float HalfSizeY = Settings.FieldSize.Y * 0.5f;
	const float MaxBladeHeight = Settings.GetMaxBladeHeight();
	const UWorld* World = GetWorld();

	struct FFoundInteractor
	{
		FVector4f Start;	// xyz, radius
		FVector4f End;		// xyz, vertical reach
		int32 Priority;
	};

	TArray<FFoundInteractor> Found;
	for (const TWeakObjectPtr<UGrassInteractorComponent>& WeakInteractor : UGrassInteractorComponent::GetAllInteractors())
	{
		const UGrassInteractorComponent* Interactor = WeakInteractor.Get();
		if (Interactor == nullptr || Interactor->GetWorld() != World || !Interactor->bInteractorEnabled || !Interactor->bPushGrass)
		{
			continue;
		}

		FVector StartWorld;
		FVector EndWorld;
		Interactor->GetShapeSegment(StartWorld, EndWorld);
		const FVector Start = ComponentTransform.InverseTransformPosition(StartWorld);
		const FVector End = ComponentTransform.InverseTransformPosition(EndWorld);
		const float Radius = FMath::Max(Interactor->Radius, 1.f);
		const float Reach = FMath::Max(Interactor->GetVerticalReach(), 0.f);

		// Skip anything that is off the field or whose lowest point is above the grass.
		const float MinX = (float)FMath::Min(Start.X, End.X);
		const float MaxX = (float)FMath::Max(Start.X, End.X);
		const float MinY = (float)FMath::Min(Start.Y, End.Y);
		const float MaxY = (float)FMath::Max(Start.Y, End.Y);
		const float LowZ = (float)FMath::Min(Start.Z, End.Z);
		const float HighZ = (float)FMath::Max(Start.Z, End.Z);
		const bool bOnField = MaxX >= -HalfSizeX - Radius && MinX <= HalfSizeX + Radius && MaxY >= -HalfSizeY - Radius && MinY <= HalfSizeY + Radius;
		const bool bTouchesGrass = LowZ - Reach <= MaxBladeHeight && HighZ + Reach >= 0.f;
		if (bOnField && bTouchesGrass)
		{
			FFoundInteractor& Entry = Found.AddDefaulted_GetRef();
			Entry.Start = FVector4f((float)Start.X, (float)Start.Y, (float)Start.Z, Radius);
			Entry.End = FVector4f((float)End.X, (float)End.Y, (float)End.Z, Reach);
			Entry.Priority = Interactor->PushPriority;
		}
	}

	// The shader handles a fixed number of interactors. With more than that (a pitch full of players with
	// one per foot), keep the highest priority ones, then the ones nearest the player, where the effect
	// can actually be seen.
	if (Found.Num() > MaxInteractors)
	{
		FVector FocusWorld = ComponentTransform.GetLocation();
		if (World != nullptr)
		{
			if (const APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				FRotator UnusedRotation;
				PlayerController->GetPlayerViewPoint(FocusWorld, UnusedRotation);
			}
		}
		const FVector FocusLocal = ComponentTransform.InverseTransformPosition(FocusWorld);
		const FVector2f Focus((float)FocusLocal.X, (float)FocusLocal.Y);

		Found.Sort([Focus](const FFoundInteractor& A, const FFoundInteractor& B)
		{
			if (A.Priority != B.Priority)
			{
				return A.Priority > B.Priority;
			}
			return FVector2f::DistSquared(FVector2f(A.Start.X, A.Start.Y), Focus) < FVector2f::DistSquared(FVector2f(B.Start.X, B.Start.Y), Focus);
		});
		Found.SetNum(MaxInteractors);
	}

	TArray<FVector4f> Packed;
	Packed.Reserve(Found.Num() * 2);
	for (const FFoundInteractor& Entry : Found)
	{
		Packed.Add(Entry.Start);
		Packed.Add(Entry.End);
	}
	SetLocalInteractors(MoveTemp(Packed));
}

void UProceduralGrassComponent::SetLocalInteractors(TArray<FVector4f>&& NewInteractors)
{
	// Nothing to send to the render thread if nothing moved.
	if (NewInteractors != LocalInteractors)
	{
		LocalInteractors = MoveTemp(NewInteractors);
		SendInteractorsToRenderThread();
	}
}

void UProceduralGrassComponent::SetInteractors(const TArray<FVector>& WorldLocations, float Radius)
{
	const FTransform& ComponentTransform = GetComponentTransform();

	TArray<FVector4f> NewInteractors;
	for (const FVector& WorldLocation : WorldLocations)
	{
		if (NewInteractors.Num() >= MaxInteractors * 2)
		{
			break;
		}
		// A sphere: start and end at the same point, vertical reach equal to the radius.
		const FVector Local = ComponentTransform.InverseTransformPosition(WorldLocation);
		const float SafeRadius = FMath::Max(Radius, 1.f);
		NewInteractors.Add(FVector4f((float)Local.X, (float)Local.Y, (float)Local.Z, SafeRadius));
		NewInteractors.Add(FVector4f((float)Local.X, (float)Local.Y, (float)Local.Z, SafeRadius));
	}

	SetLocalInteractors(MoveTemp(NewInteractors));
}

void UProceduralGrassComponent::ClearInteractors()
{
	SetLocalInteractors(TArray<FVector4f>());
}

void UProceduralGrassComponent::SendInteractorsToRenderThread()
{
	if (SceneProxy != nullptr)
	{
		FGrassSceneProxy* GrassProxy = static_cast<FGrassSceneProxy*>(SceneProxy);
		ENQUEUE_RENDER_COMMAND(ProceduralGrassSetInteractors)(
			[GrassProxy, Interactors = LocalInteractors](FRHICommandListImmediate&)
			{
				GrassProxy->SetInteractors_RenderThread(Interactors);
			});
	}
}
