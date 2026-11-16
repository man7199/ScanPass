#pragma once

#include "CoreMinimal.h"

class FTextureResource;

/** Render thread copy of everything UProceduralGrassComponent exposes. */
struct FGrassSettings
{
	// Layout
	FVector2f FieldSize = FVector2f(10500.f, 6800.f);
	float TileSize = 280.f;
	int32 BladesPerTileSide = 256;	// power of two
	int32 NumLods = 5;
	float Lod0Distance = 400.f;
	float MaxDrawDistance = 9000.f;
	float WidthLodScale = 0.8f;

	// Blade shape
	float BladeHeight = 3.5f;
	float HeightVariation = 0.15f;
	float BladeWidth = 1.3f;
	float Tilt = 0.3f;
	float TiltVariation = 0.15f;
	float Bend = 0.25f;

	// Clumping
	float ClumpSize = 30.f;
	float ClumpPull = 0.04f;
	float ClumpHeightVariation = 0.08f;
	float ClumpFacing = 0.3f;

	// Mowing stripes and colour
	float StripeWidth = 550.f;
	float MowStrength = 0.5f;
	float StripeContrast = 0.12f;
	float ColorVariation = 0.2f;
	FLinearColor RootColor = FLinearColor(0.012f, 0.045f, 0.008f);
	FLinearColor TipColor = FLinearColor(0.07f, 0.20f, 0.03f);

	// Wind
	FVector2f WindDirection = FVector2f(1.f, 0.f);
	float WindStrength = 0.15f;
	float WindSpeed = 300.f;
	float WindNoiseScale = 0.002f;
	float BobAmplitude = 0.15f;
	float BobSpeed = 4.f;

	// Shading helpers
	float NormalRounding = 0.35f;
	float ViewThickening = 1.f;

	float GetMaxBladeHeight() const
	{
		return BladeHeight * (1.f + HeightVariation) * (1.f + ClumpHeightVariation);
	}
};

/**
 * Where the compute shader finds the depth capture of the interacting objects, and how to read it.
 * Written by the game thread every frame, read by the render thread.
 */
struct FGrassConformData
{
	/** Render target the capture draws into. Owned by the grass component, which outlives its scene proxy. */
	const FTextureResource* DepthResource = nullptr;
	/** dot((LocalPosition, 1), Plane) gives the U / V coordinate of a point in the capture. */
	FVector4f UPlane = FVector4f(0, 0, 0, 0);
	FVector4f VPlane = FVector4f(0, 0, 0, 0);
	/** How far below the pitch the capture camera sits. Captured depth minus this is height above the pitch. */
	float CameraDistance = 0.f;
	bool bEnabled = false;
};
