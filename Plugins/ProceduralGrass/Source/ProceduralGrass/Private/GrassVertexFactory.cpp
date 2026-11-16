#include "GrassVertexFactory.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "MeshBatch.h"
#include "MeshDrawShaderBindings.h"
#include "MeshMaterialShader.h"
#include "RHIResourceUtils.h"

void FGrassBladeIndexBuffer::InitRHI(FRHICommandListBase& RHICmdList)
{
	// Vertex 2i is the left edge and 2i+1 the right edge of row i; the last vertex is the tip.
	TArray<uint16> Indices;
	Indices.Reserve(GetNumIndices());

	for (int32 Segment = 0; Segment < NumSegments - 1; ++Segment)
	{
		const int32 Base = Segment * 2;
		Indices.Add((uint16)(Base));
		Indices.Add((uint16)(Base + 1));
		Indices.Add((uint16)(Base + 2));

		Indices.Add((uint16)(Base + 1));
		Indices.Add((uint16)(Base + 3));
		Indices.Add((uint16)(Base + 2));
	}

	const int32 LastRow = (NumSegments - 1) * 2;
	Indices.Add((uint16)(LastRow));
	Indices.Add((uint16)(LastRow + 1));
	Indices.Add((uint16)(LastRow + 2));

	check(Indices.Num() == GetNumIndices());

	IndexBufferRHI = UE::RHIResourceUtils::CreateIndexBufferFromArray(RHICmdList, TEXT("ProceduralGrass.BladeIndexBuffer"), EBufferUsageFlags::Static, MakeConstArrayView(Indices));
}

/** Binds the per draw data from FGrassBatchUserData. */
class FGrassVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FGrassVertexFactoryShaderParameters, NonVirtual);

public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		InstanceBufferParameter.Bind(ParameterMap, TEXT("GrassInstanceBuffer"));
		DrawParamsParameter.Bind(ParameterMap, TEXT("GrassDrawParams"));
		ShapeParamsParameter.Bind(ParameterMap, TEXT("GrassShapeParams"));
		RootColorParameter.Bind(ParameterMap, TEXT("GrassRootColor"));
		TipColorParameter.Bind(ParameterMap, TEXT("GrassTipColor"));
		FieldParamsParameter.Bind(ParameterMap, TEXT("GrassFieldParams"));
		WindParamsParameter.Bind(ParameterMap, TEXT("GrassWindParams"));
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* InVertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FGrassBatchUserData* UserData = (const FGrassBatchUserData*)BatchElement.UserData;
		if (UserData == nullptr)
		{
			return;
		}

		ShaderBindings.Add(InstanceBufferParameter, UserData->InstanceBufferSRV);
		ShaderBindings.Add(DrawParamsParameter, UserData->DrawParams);
		ShaderBindings.Add(ShapeParamsParameter, UserData->ShapeParams);
		ShaderBindings.Add(RootColorParameter, UserData->RootColor);
		ShaderBindings.Add(TipColorParameter, UserData->TipColor);
		ShaderBindings.Add(FieldParamsParameter, UserData->FieldParams);
		ShaderBindings.Add(WindParamsParameter, UserData->WindParams);
	}

protected:
	LAYOUT_FIELD(FShaderResourceParameter, InstanceBufferParameter);
	LAYOUT_FIELD(FShaderParameter, DrawParamsParameter);
	LAYOUT_FIELD(FShaderParameter, ShapeParamsParameter);
	LAYOUT_FIELD(FShaderParameter, RootColorParameter);
	LAYOUT_FIELD(FShaderParameter, TipColorParameter);
	LAYOUT_FIELD(FShaderParameter, FieldParamsParameter);
	LAYOUT_FIELD(FShaderParameter, WindParamsParameter);
};

IMPLEMENT_TYPE_LAYOUT(FGrassVertexFactoryShaderParameters);

IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGrassVertexFactory, SF_Vertex, FGrassVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FGrassVertexFactory, SF_Pixel, FGrassVertexFactoryShaderParameters);

FGrassVertexFactory::FGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel)
	: FVertexFactory(InFeatureLevel)
	, HighDetailIndexBuffer(GrassBlade::NumSegmentsHigh)
	, LowDetailIndexBuffer(GrassBlade::NumSegmentsLow)
{
}

void FGrassVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	HighDetailIndexBuffer.InitResource(RHICmdList);
	LowDetailIndexBuffer.InitResource(RHICmdList);

	// No vertex buffers: the vertex shader only uses SV_VertexID and SV_InstanceID.
	FVertexStream NullVertexStream;
	NullVertexStream.VertexBuffer = nullptr;
	NullVertexStream.Stride = 0;
	NullVertexStream.Offset = 0;
	NullVertexStream.VertexStreamUsage = EVertexStreamUsage::ManualFetch;

	check(Streams.Num() == 0);
	Streams.Add(NullVertexStream);

	FVertexDeclarationElementList Elements;
	InitDeclaration(Elements);
}

void FGrassVertexFactory::ReleaseRHI()
{
	HighDetailIndexBuffer.ReleaseResource();
	LowDetailIndexBuffer.ReleaseResource();

	FVertexFactory::ReleaseRHI();
}

bool FGrassVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	if (!IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5))
	{
		return false;
	}

	// Adding a new material usage flag needs an engine change, so the grass borrows the
	// "Used with Virtual Heightfield Mesh" flag to keep the number of compiled shaders down.
	// The default material is always compiled so there is a fallback.
	return (Parameters.MaterialParameters.MaterialDomain == MD_Surface && Parameters.MaterialParameters.bIsUsedWithVirtualHeightfieldMesh)
		|| Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

void FGrassVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("VF_PROCEDURAL_GRASS"), 1);
}

void FGrassVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FGrassVertexFactory, "/Plugin/ProceduralGrass/Private/GrassVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
);
