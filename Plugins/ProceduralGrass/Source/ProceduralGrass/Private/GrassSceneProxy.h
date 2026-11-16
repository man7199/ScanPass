#pragma once

#include "CoreMinimal.h"
#include "GrassSettings.h"
#include "Materials/MaterialRelevance.h"
#include "PrimitiveSceneProxy.h"

class FGrassVertexFactory;
class FMaterialRenderProxy;
class UProceduralGrassComponent;

/** Render thread representation of a UProceduralGrassComponent. */
class FGrassSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FGrassSceneProxy(UProceduralGrassComponent* InComponent);

	//~ Begin FPrimitiveSceneProxy Interface
	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	//~ End FPrimitiveSceneProxy Interface

	void SetInteractors_RenderThread(const TArray<FVector4f>& InInteractors);
	void SetConformData_RenderThread(const FGrassConformData& InConformData);

	const FGrassSettings& GetSettings() const { return Settings; }
	const TArray<FVector4f>& GetInteractors() const { return Interactors; }
	const FGrassConformData& GetConformData() const { return ConformData; }

private:
	FGrassSettings Settings;
	TArray<FVector4f> Interactors;
	FGrassConformData ConformData;

	FMaterialRenderProxy* Material = nullptr;
	FMaterialRelevance MaterialRelevance;

	FGrassVertexFactory* VertexFactory = nullptr;
};
