#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "SceneManagement.h"
#include "VertexFactory.h"

class FShaderParameterMap;
struct FShaderCompilerEnvironment;

/** Vertices per blade: pairs going up the blade plus one tip vertex. */
namespace GrassBlade
{
	static constexpr int32 NumSegmentsHigh = 7;	// 15 vertices, as in the talk
	static constexpr int32 NumSegmentsLow = 3;	// 7 vertices

	/** Triangle list indices for a blade with the given number of segments. */
	inline int32 GetNumIndices(int32 NumSegments) { return ((NumSegments - 1) * 2 + 1) * 3; }
}

/** Per frame, per draw data handed to the vertex shader through FMeshBatchElement::UserData. */
struct FGrassBatchUserData : public FOneFrameResource
{
	FRHIShaderResourceView* InstanceBufferSRV = nullptr;
	FVector4f DrawParams = FVector4f(0, 0, 0, 0);	// x = first instance, y = segments, z = time, w = previous time
	FVector4f ShapeParams = FVector4f(0, 0, 0, 0);	// x = normal rounding, y = view thickening, z = bob amplitude, w = bob speed
	FVector4f RootColor = FVector4f(0, 0, 0, 1);
	FVector4f TipColor = FVector4f(0, 0, 0, 1);
	FVector4f FieldParams = FVector4f(0, 0, 1, 1);	// xy = field min, zw = 1 / field size
	FVector4f WindParams = FVector4f(1, 0, 0, 0);	// xy = wind direction, z = bob wave number
};

/** Index buffer for a single blade. There is no vertex buffer; positions come from SV_VertexID. */
class FGrassBladeIndexBuffer : public FIndexBuffer
{
public:
	explicit FGrassBladeIndexBuffer(int32 InNumSegments)
		: NumSegments(InNumSegments)
	{}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	int32 GetNumIndices() const { return GrassBlade::GetNumIndices(NumSegments); }
	int32 GetNumSegments() const { return NumSegments; }

private:
	const int32 NumSegments;
};

/** Vertex factory that builds grass blades in the vertex shader from the GPU generated instance buffer. */
class FGrassVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FGrassVertexFactory);

public:
	explicit FGrassVertexFactory(ERHIFeatureLevel::Type InFeatureLevel);

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);
	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);
	static void ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform, const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors);

	const FGrassBladeIndexBuffer& GetHighDetailIndexBuffer() const { return HighDetailIndexBuffer; }
	const FGrassBladeIndexBuffer& GetLowDetailIndexBuffer() const { return LowDetailIndexBuffer; }

private:
	FGrassBladeIndexBuffer HighDetailIndexBuffer;
	FGrassBladeIndexBuffer LowDetailIndexBuffer;
};
