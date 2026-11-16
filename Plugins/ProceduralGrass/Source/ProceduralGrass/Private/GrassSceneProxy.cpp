#include "GrassSceneProxy.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Engine.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "GrassVertexFactory.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "Misc/ScopeLock.h"
#include "PrimitiveViewRelevance.h"
#include "ProceduralGrassComponent.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "ShaderParameterStruct.h"
#include "TextureResource.h"

DECLARE_GPU_STAT(ProceduralGrass);

static TAutoConsoleVariable<int32> CVarGrassMaxBladesHigh(
	TEXT("r.ProceduralGrass.MaxBladesHigh"),
	900000,
	TEXT("Capacity of the instance buffer for high detail (near) blades, per view. Takes effect when buffers are next allocated."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGrassMaxBladesLow(
	TEXT("r.ProceduralGrass.MaxBladesLow"),
	1400000,
	TEXT("Capacity of the instance buffer for low detail (far) blades, per view. Takes effect when buffers are next allocated."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarGrassEnable(
	TEXT("r.ProceduralGrass.Enable"),
	1,
	TEXT("Set to 0 to stop drawing procedural grass. Useful for before/after comparisons."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarGrassLodScale(
	TEXT("r.ProceduralGrass.LodScale"),
	1.0f,
	TEXT("Scales the LOD 0 distance of procedural grass. Higher means more blades."),
	ECVF_RenderThreadSafe);

namespace ProceduralGrass
{
	/** One blade in the instance buffer. Keep in sync with FGrassBlade in GrassCommon.ush. */
	struct FGrassBladeGPU
	{
		FVector3f Position;
		float Height;
		FVector2f Facing;
		float Width;
		float Tilt;
		float Bend;
		float Hash;
		float Wind;
		uint32 Packed;
	};
	static_assert(sizeof(FGrassBladeGPU) == 48, "FGrassBladeGPU has to match FGrassBlade in GrassCommon.ush");

	/** Two indexed indirect draws (high and low detail), 5 uints each. Keep in sync with GrassCommon.ush. */
	static const int32 NumIndirectArgs = 10;
	static const uint32 IndirectArgsOffsetHigh = 0;
	static const uint32 IndirectArgsOffsetLow = 5 * sizeof(uint32);

	static const int32 MaxInteractors = 16;
	static const int32 MaxLods = 6;

	/** Buffers filled on the GPU each frame and consumed by the two grass draws. Pooled across frames. */
	struct FDrawBuffers
	{
		FBufferRHIRef InstanceBuffer;
		FUnorderedAccessViewRHIRef InstanceBufferUAV;
		FShaderResourceViewRHIRef InstanceBufferSRV;

		FBufferRHIRef IndirectArgsBuffer;
		FUnorderedAccessViewRHIRef IndirectArgsBufferUAV;

		uint32 MaxBladesHigh = 0;
		uint32 MaxBladesLow = 0;
	};

	static void InitializeDrawBuffers(FRHICommandListBase& RHICmdList, FDrawBuffers& InBuffers)
	{
		InBuffers.MaxBladesHigh = (uint32)FMath::Clamp(CVarGrassMaxBladesHigh.GetValueOnAnyThread(), 1024, 8 * 1024 * 1024);
		InBuffers.MaxBladesLow = (uint32)FMath::Clamp(CVarGrassMaxBladesLow.GetValueOnAnyThread(), 1024, 8 * 1024 * 1024);

		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateStructured<FGrassBladeGPU>(TEXT("ProceduralGrass.InstanceBuffer"), InBuffers.MaxBladesHigh + InBuffers.MaxBladesLow)
				.AddUsage(EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::ShaderResource)
				.SetInitialState(ERHIAccess::SRVMask);

			InBuffers.InstanceBuffer = RHICmdList.CreateBuffer(CreateDesc);
			InBuffers.InstanceBufferUAV = RHICmdList.CreateUnorderedAccessView(
				InBuffers.InstanceBuffer,
				FRHIViewDesc::CreateBufferUAV()
					.SetTypeFromBuffer(InBuffers.InstanceBuffer));
			InBuffers.InstanceBufferSRV = RHICmdList.CreateShaderResourceView(
				InBuffers.InstanceBuffer,
				FRHIViewDesc::CreateBufferSRV()
					.SetTypeFromBuffer(InBuffers.InstanceBuffer));
		}
		{
			const FRHIBufferCreateDesc CreateDesc =
				FRHIBufferCreateDesc::CreateVertex<uint32>(TEXT("ProceduralGrass.IndirectArgsBuffer"), NumIndirectArgs)
				.AddUsage(EBufferUsageFlags::UnorderedAccess | EBufferUsageFlags::DrawIndirect)
				.SetInitialState(ERHIAccess::IndirectArgs);

			InBuffers.IndirectArgsBuffer = RHICmdList.CreateBuffer(CreateDesc);
			InBuffers.IndirectArgsBufferUAV = RHICmdList.CreateUnorderedAccessView(
				InBuffers.IndirectArgsBuffer,
				FRHIViewDesc::CreateBufferUAV()
					.SetType(FRHIViewDesc::EBufferType::Typed)
					.SetFormat(PF_R32_UINT));
		}
	}

	static void ReleaseDrawBuffers(FDrawBuffers& InBuffers)
	{
		InBuffers.InstanceBuffer.SafeRelease();
		InBuffers.InstanceBufferUAV.SafeRelease();
		InBuffers.InstanceBufferSRV.SafeRelease();
		InBuffers.IndirectArgsBuffer.SafeRelease();
		InBuffers.IndirectArgsBufferUAV.SafeRelease();
	}

	/** Everything the generation pass needs from a view, copied when the work is queued. */
	struct FViewData
	{
		FVector ViewOrigin = FVector::ZeroVector;
		FMatrix WorldToClip = FMatrix::Identity;
		FConvexVolume ViewFrustum;
		float CullMargin = 1.f;
		float Time = 0.f;
	};

	// ---------------------------------------------------------------------------------------------
	// Compute shaders
	// ---------------------------------------------------------------------------------------------

	class FGrassInitArgsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FGrassInitArgsCS);
		SHADER_USE_PARAMETER_STRUCT(FGrassInitArgsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, IndexCountHigh)
			SHADER_PARAMETER(uint32, IndexCountLow)
			SHADER_PARAMETER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FGrassInitArgsCS, "/Plugin/ProceduralGrass/Private/GrassGenerate.usf", "InitArgsCS", SF_Compute);

	class FGrassFinalizeArgsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FGrassFinalizeArgsCS);
		SHADER_USE_PARAMETER_STRUCT(FGrassFinalizeArgsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, MaxBladesHigh)
			SHADER_PARAMETER(uint32, MaxBladesLow)
			SHADER_PARAMETER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FGrassFinalizeArgsCS, "/Plugin/ProceduralGrass/Private/GrassGenerate.usf", "FinalizeArgsCS", SF_Compute);

	class FGrassGenerateBladesCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FGrassGenerateBladesCS);
		SHADER_USE_PARAMETER_STRUCT(FGrassGenerateBladesCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, TileBuffer)
			SHADER_PARAMETER_UAV(RWStructuredBuffer<FGrassBlade>, RWInstanceBuffer)
			SHADER_PARAMETER_UAV(RWBuffer<uint>, RWIndirectArgsBuffer)

			SHADER_PARAMETER(FMatrix44f, GrassLocalToClip)
			SHADER_PARAMETER(FVector3f, GrassCameraPosition)
			SHADER_PARAMETER(float, GrassCullMargin)
			SHADER_PARAMETER(float, GrassTime)

			SHADER_PARAMETER(FVector2f, FieldMin)
			SHADER_PARAMETER(FVector2f, FieldMax)
			SHADER_PARAMETER(float, TileSize)
			SHADER_PARAMETER(uint32, BladesPerSideLod0)
			SHADER_PARAMETER(uint32, DispatchLod)
			SHADER_PARAMETER(uint32, TileOffset)
			SHADER_PARAMETER(uint32, MaxLod)
			SHADER_PARAMETER(float, Lod0Distance)
			SHADER_PARAMETER(float, MaxDrawDistance)
			SHADER_PARAMETER(float, WidthLodScale)
			SHADER_PARAMETER(uint32, MaxBladesHigh)
			SHADER_PARAMETER(uint32, MaxBladesLow)

			SHADER_PARAMETER(float, BladeHeight)
			SHADER_PARAMETER(float, HeightVariation)
			SHADER_PARAMETER(float, BladeWidth)
			SHADER_PARAMETER(float, BaseTilt)
			SHADER_PARAMETER(float, TiltVariation)
			SHADER_PARAMETER(float, BaseBend)

			SHADER_PARAMETER(float, ClumpSize)
			SHADER_PARAMETER(float, ClumpPull)
			SHADER_PARAMETER(float, ClumpHeightVariation)
			SHADER_PARAMETER(float, ClumpFacing)

			SHADER_PARAMETER(float, StripeWidth)
			SHADER_PARAMETER(float, MowStrength)
			SHADER_PARAMETER(float, StripeContrast)
			SHADER_PARAMETER(float, ColorVariation)

			SHADER_PARAMETER(FVector2f, WindDirection)
			SHADER_PARAMETER(float, WindStrength)
			SHADER_PARAMETER(float, WindSpeed)
			SHADER_PARAMETER(float, WindNoiseScale)

			SHADER_PARAMETER_ARRAY(FVector4f, Interactors, [16])
			SHADER_PARAMETER_ARRAY(FVector4f, InteractorEnds, [16])
			SHADER_PARAMETER(uint32, NumInteractors)

			SHADER_PARAMETER_TEXTURE(Texture2D, ConformDepthTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, ConformDepthSampler)
			SHADER_PARAMETER(FVector4f, ConformUPlane)
			SHADER_PARAMETER(FVector4f, ConformVPlane)
			SHADER_PARAMETER(FVector4f, ConformParams)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FGrassGenerateBladesCS, "/Plugin/ProceduralGrass/Private/GrassGenerate.usf", "GenerateBladesCS", SF_Compute);

	// ---------------------------------------------------------------------------------------------
	// CPU tile culling and LOD selection
	// ---------------------------------------------------------------------------------------------

	/** Visible tiles sorted by LOD. Each entry is xy = tile min corner (local), zw = tile coordinate. */
	struct FVisibleTiles
	{
		TArray<FVector4f> Tiles;
		int32 Offsets[MaxLods] = {};
		int32 Counts[MaxLods] = {};
	};

	static void GatherVisibleTiles(const FGrassSettings& Settings, const FMatrix& LocalToWorld, const FViewData& ViewData, const FVector& LocalViewOrigin, float Lod0Distance, FVisibleTiles& Out)
	{
		const int32 NumTilesX = FMath::Max(FMath::CeilToInt32(Settings.FieldSize.X / Settings.TileSize), 1);
		const int32 NumTilesY = FMath::Max(FMath::CeilToInt32(Settings.FieldSize.Y / Settings.TileSize), 1);
		const FVector2D FieldMin(-0.5 * Settings.FieldSize.X, -0.5 * Settings.FieldSize.Y);
		const double TileSize = Settings.TileSize;
		const double MaxHeight = (double)Settings.GetMaxBladeHeight() + 1.0;
		const int32 MaxLod = Settings.NumLods - 1;

		TArray<FVector4f> PerLod[MaxLods];

		for (int32 Y = 0; Y < NumTilesY; ++Y)
		{
			for (int32 X = 0; X < NumTilesX; ++X)
			{
				const FVector TileMin(FieldMin.X + X * TileSize, FieldMin.Y + Y * TileSize, 0.0);
				const FVector TileMax(TileMin.X + TileSize, TileMin.Y + TileSize, MaxHeight);
				const FBox LocalBox(TileMin, TileMax);

				// Distance from the camera to the closest point of the tile. The shader works out a LOD per
				// blade from the same formula, so using the closest point guarantees the tile is generated
				// at a LOD that contains every blade the shader wants to keep.
				const FVector Closest = LocalBox.GetClosestPointTo(LocalViewOrigin);
				const double Distance = FVector::Dist(Closest, LocalViewOrigin);
				if (Distance > Settings.MaxDrawDistance)
				{
					continue;
				}

				const FBox WorldBox = LocalBox.TransformBy(LocalToWorld);
				if (!ViewData.ViewFrustum.IntersectBox(WorldBox.GetCenter(), WorldBox.GetExtent()))
				{
					continue;
				}

				const double LodF = FMath::Max(FMath::Log2(FMath::Max(Distance, 1.0) / (double)Lod0Distance), 0.0);
				const int32 Lod = FMath::Clamp(FMath::FloorToInt32(LodF), 0, MaxLod);

				PerLod[Lod].Add(FVector4f((float)TileMin.X, (float)TileMin.Y, (float)X, (float)Y));
			}
		}

		for (int32 Lod = 0; Lod <= MaxLod; ++Lod)
		{
			Out.Offsets[Lod] = Out.Tiles.Num();
			Out.Counts[Lod] = PerLod[Lod].Num();
			Out.Tiles.Append(PerLod[Lod]);
		}
	}
}

// -------------------------------------------------------------------------------------------------
// Renderer extension
// -------------------------------------------------------------------------------------------------

/**
 * Owns the pooled draw buffers and adds the GPU generation passes to the frame.
 *
 * GetDynamicMeshElements() runs while the renderer gathers visible primitives. At that point it only
 * reserves buffers (AddWork). The engine's pre render delegate fires afterwards, still before any mesh
 * pass draws, and that is where the compute passes are added to the render graph (SubmitWork).
 * This is the same structure the engine's Virtual Heightfield Mesh plugin uses.
 */
class FGrassRendererExtension : public FRenderResource
{
public:
	/** Call once to register this extension. */
	void RegisterExtension();

	/** Are we inside a BeginFrame()/EndFrame() scope? */
	bool IsInFrame() const { return bInFrame; }

	/** Reserve draw buffers for a proxy/view pair and queue the work that fills them. Returns the buffer index. */
	int32 AddWork(FRHICommandListBase& RHICmdList, const FGrassSceneProxy* InProxy, const FSceneView* InView, const ProceduralGrass::FViewData& InViewData);

	const ProceduralGrass::FDrawBuffers& GetBuffers(int32 Index) const { return Buffers[Index]; }

	FCriticalSection& GetLock() { return Lock; }

protected:
	//~ Begin FRenderResource Interface
	virtual void ReleaseRHI() override;
	//~ End FRenderResource Interface

private:
	void BeginFrame(FRDGBuilder& GraphBuilder);
	void EndFrame(FRDGBuilder& GraphBuilder);
	void ResetFrame();
	void SubmitWork(FRDGBuilder& GraphBuilder);

	struct FWorkDesc
	{
		const FGrassSceneProxy* Proxy = nullptr;
		const FSceneView* View = nullptr;	// only used to recognise the same view again within the frame
		ProceduralGrass::FViewData ViewData;
		int32 BufferIndex = INDEX_NONE;
	};

	bool bInFrame = false;
	FCriticalSection Lock;

	/** Buffers persist between frames to avoid reallocating, their contents do not. */
	TArray<ProceduralGrass::FDrawBuffers> Buffers;
	/** Per buffer frame stamp of last use. */
	TArray<uint32> DiscardIds;
	uint32 DiscardId = 0;

	TArray<FWorkDesc> WorkDescs;
};

/** Single global instance of the grass renderer extension. */
static TGlobalResource<FGrassRendererExtension> GGrassRendererExtension;

void FGrassRendererExtension::RegisterExtension()
{
	static bool bInit = false;
	if (!bInit)
	{
		GEngine->GetPreRenderDelegateEx().AddRaw(this, &FGrassRendererExtension::BeginFrame);
		GEngine->GetPostRenderDelegateEx().AddRaw(this, &FGrassRendererExtension::EndFrame);
		bInit = true;
	}
}

void FGrassRendererExtension::ReleaseRHI()
{
	Buffers.Empty();
	DiscardIds.Empty();
}

int32 FGrassRendererExtension::AddWork(FRHICommandListBase& RHICmdList, const FGrassSceneProxy* InProxy, const FSceneView* InView, const ProceduralGrass::FViewData& InViewData)
{
	// Callers hold the lock.

	// Reuse the buffers if this proxy/view pair was already queued this frame.
	for (const FWorkDesc& Existing : WorkDescs)
	{
		if (Existing.Proxy == InProxy && Existing.View == InView)
		{
			return Existing.BufferIndex;
		}
	}

	FWorkDesc WorkDesc;
	WorkDesc.Proxy = InProxy;
	WorkDesc.View = InView;
	WorkDesc.ViewData = InViewData;

	// Try to recycle buffers that were not used yet this frame.
	for (int32 BufferIndex = 0; BufferIndex < Buffers.Num(); ++BufferIndex)
	{
		if (DiscardIds[BufferIndex] < DiscardId)
		{
			DiscardIds[BufferIndex] = DiscardId;
			WorkDesc.BufferIndex = BufferIndex;
			break;
		}
	}

	// Allocate new buffers if necessary.
	if (WorkDesc.BufferIndex == INDEX_NONE)
	{
		DiscardIds.Add(DiscardId);
		WorkDesc.BufferIndex = Buffers.AddDefaulted();
		ProceduralGrass::InitializeDrawBuffers(RHICmdList, Buffers[WorkDesc.BufferIndex]);
	}

	WorkDescs.Add(WorkDesc);
	return WorkDesc.BufferIndex;
}

void FGrassRendererExtension::BeginFrame(FRDGBuilder& GraphBuilder)
{
	FScopeLock ScopeLock(&Lock);

	if (!ensure(!bInFrame))
	{
		ResetFrame();
	}
	bInFrame = true;

	if (WorkDescs.Num() > 0)
	{
		SubmitWork(GraphBuilder);
	}
}

void FGrassRendererExtension::ResetFrame()
{
	FScopeLock ScopeLock(&Lock);

	bInFrame = false;
	WorkDescs.Reset();

	// Release buffers that have not been used for a few frames.
	DiscardId++;

	for (int32 Index = 0; Index < DiscardIds.Num();)
	{
		if (DiscardId - DiscardIds[Index] > 4u)
		{
			ProceduralGrass::ReleaseDrawBuffers(Buffers[Index]);
			Buffers.RemoveAtSwap(Index);
			DiscardIds.RemoveAtSwap(Index);
		}
		else
		{
			++Index;
		}
	}
}

void FGrassRendererExtension::EndFrame(FRDGBuilder& GraphBuilder)
{
	ResetFrame();
}

namespace ProceduralGrass
{
	/** Transition the draw buffers between "written by compute" and "read by the draws". */
	static void AddPass_TransitionDrawBuffers(FRDGBuilder& GraphBuilder, const TArray<FDrawBuffers>& Buffers, TConstArrayView<int32> BufferIndices, bool bToWrite)
	{
		TArray<FRHITransitionInfo> TransitionInfos;
		TransitionInfos.Reserve(BufferIndices.Num() * 2);

		for (int32 BufferIndex : BufferIndices)
		{
			FRHIUnorderedAccessView* IndirectArgsBufferUAV = Buffers[BufferIndex].IndirectArgsBufferUAV;
			FRHIUnorderedAccessView* InstanceBufferUAV = Buffers[BufferIndex].InstanceBufferUAV;

			TransitionInfos.Add(FRHITransitionInfo(IndirectArgsBufferUAV, bToWrite ? ERHIAccess::IndirectArgs : ERHIAccess::UAVMask, bToWrite ? ERHIAccess::UAVMask : ERHIAccess::IndirectArgs));
			TransitionInfos.Add(FRHITransitionInfo(InstanceBufferUAV, bToWrite ? ERHIAccess::SRVMask : ERHIAccess::UAVMask, bToWrite ? ERHIAccess::UAVMask : ERHIAccess::SRVMask));
		}

		AddPass(GraphBuilder, RDG_EVENT_NAME("ProceduralGrass.TransitionDrawBuffers"), [TransitionInfos](FRHICommandList& InRHICmdList)
		{
			InRHICmdList.Transition(TransitionInfos);
		});
	}

	/**
	 * Marks the start or the end of the blade generation dispatches for one set of draw buffers.
	 *
	 * The generation dispatches all append through the same counters with atomics, so they are allowed to
	 * overlap on the GPU (UAV overlap). The passes before and after them are different: InitArgs has to
	 * finish resetting the counters before anything is appended, and FinalizeArgs must only read the
	 * counters once every append has landed. A UAV to UAV transition is a UAV barrier and enforces that.
	 * Without it the two can race with the appends and a different handful of blades goes missing each frame.
	 */
	static void AddPass_GenerationScope(FRDGBuilder& GraphBuilder, const FDrawBuffers& DrawBuffers, bool bBegin)
	{
		FRHIUnorderedAccessView* IndirectArgsBufferUAV = DrawBuffers.IndirectArgsBufferUAV;
		FRHIUnorderedAccessView* InstanceBufferUAV = DrawBuffers.InstanceBufferUAV;

		AddPass(GraphBuilder, RDG_EVENT_NAME("ProceduralGrass.GenerationScope"), [IndirectArgsBufferUAV, InstanceBufferUAV, bBegin](FRHICommandList& InRHICmdList)
		{
			FRHIUnorderedAccessView* OverlapUAVs[] = { IndirectArgsBufferUAV, InstanceBufferUAV };
			const FRHITransitionInfo Barriers[] =
			{
				FRHITransitionInfo(IndirectArgsBufferUAV, ERHIAccess::UAVMask, ERHIAccess::UAVMask),
				FRHITransitionInfo(InstanceBufferUAV, ERHIAccess::UAVMask, ERHIAccess::UAVMask),
			};

			if (bBegin)
			{
				InRHICmdList.Transition(MakeArrayView(Barriers));
				InRHICmdList.BeginUAVOverlap(MakeArrayView(OverlapUAVs));
			}
			else
			{
				InRHICmdList.EndUAVOverlap(MakeArrayView(OverlapUAVs));
				InRHICmdList.Transition(MakeArrayView(Barriers));
			}
		});
	}
}

void FGrassRendererExtension::SubmitWork(FRDGBuilder& GraphBuilder)
{
	using namespace ProceduralGrass;

	RDG_EVENT_SCOPE_STAT(GraphBuilder, ProceduralGrass, "ProceduralGrass");

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	TArray<int32, TInlineAllocator<8>> UsedBufferIndices;
	for (const FWorkDesc& WorkDesc : WorkDescs)
	{
		UsedBufferIndices.AddUnique(WorkDesc.BufferIndex);
	}

	AddPass_TransitionDrawBuffers(GraphBuilder, Buffers, UsedBufferIndices, true);

	const float LodScale = FMath::Max(CVarGrassLodScale.GetValueOnRenderThread(), 0.05f);

	for (const FWorkDesc& WorkDesc : WorkDescs)
	{
		const FGrassSceneProxy* Proxy = WorkDesc.Proxy;
		const FGrassSettings& Settings = Proxy->GetSettings();
		const FViewData& ViewData = WorkDesc.ViewData;
		const FDrawBuffers& DrawBuffers = Buffers[WorkDesc.BufferIndex];

		const FMatrix LocalToWorld = Proxy->GetLocalToWorld();
		const FVector LocalViewOrigin = LocalToWorld.InverseTransformPosition(ViewData.ViewOrigin);
		const float Lod0Distance = Settings.Lod0Distance * LodScale;

		// Reset the two draws. Has to happen even if nothing is visible because the buffers are recycled.
		{
			FGrassInitArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGrassInitArgsCS::FParameters>();
			PassParameters->IndexCountHigh = (uint32)GrassBlade::GetNumIndices(GrassBlade::NumSegmentsHigh);
			PassParameters->IndexCountLow = (uint32)GrassBlade::GetNumIndices(GrassBlade::NumSegmentsLow);
			PassParameters->RWIndirectArgsBuffer = DrawBuffers.IndirectArgsBufferUAV;

			TShaderMapRef<FGrassInitArgsCS> ComputeShader(ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ProceduralGrass.InitArgs"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
		}

		FVisibleTiles VisibleTiles;
		GatherVisibleTiles(Settings, LocalToWorld, ViewData, LocalViewOrigin, Lod0Distance, VisibleTiles);

		AddPass_GenerationScope(GraphBuilder, DrawBuffers, true);

		if (VisibleTiles.Tiles.Num() > 0)
		{
			FRDGBufferRef TileBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("ProceduralGrass.TileBuffer"), VisibleTiles.Tiles);
			FRDGBufferSRVRef TileBufferSRV = GraphBuilder.CreateSRV(TileBuffer);

			const FMatrix44f LocalToClip = FMatrix44f(LocalToWorld * ViewData.WorldToClip);
			const FVector2f FieldMax = Settings.FieldSize * 0.5f;
			const FVector2f FieldMin = -FieldMax;
			const TArray<FVector4f>& Interactors = Proxy->GetInteractors();
			// Two entries per interactor: start (xyz, radius) and end (xyz, vertical reach).
			const int32 NumInteractors = FMath::Min(Interactors.Num() / 2, MaxInteractors);

			// Depth capture of the interacting objects. Falls back to a dummy texture when it is not available.
			const FGrassConformData& ConformData = Proxy->GetConformData();
			const bool bConform = ConformData.bEnabled && ConformData.DepthResource != nullptr && ConformData.DepthResource->TextureRHI.IsValid();
			FRHITexture* ConformDepthTexture = bConform ? ConformData.DepthResource->TextureRHI.GetReference() : GBlackTexture->TextureRHI.GetReference();
			// Small gap kept between the blades and the object so they do not z-fight with it.
			const float ConformClearance = 0.3f;

			TShaderMapRef<FGrassGenerateBladesCS> ComputeShader(ShaderMap);

			// One dispatch per LOD: X/Y cover the blades of a tile, Z is the tile.
			for (int32 Lod = 0; Lod < Settings.NumLods; ++Lod)
			{
				const int32 NumTiles = VisibleTiles.Counts[Lod];
				if (NumTiles == 0)
				{
					continue;
				}

				FGrassGenerateBladesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGrassGenerateBladesCS::FParameters>();
				PassParameters->TileBuffer = TileBufferSRV;
				PassParameters->RWInstanceBuffer = DrawBuffers.InstanceBufferUAV;
				PassParameters->RWIndirectArgsBuffer = DrawBuffers.IndirectArgsBufferUAV;

				PassParameters->GrassLocalToClip = LocalToClip;
				PassParameters->GrassCameraPosition = FVector3f(LocalViewOrigin);
				PassParameters->GrassCullMargin = ViewData.CullMargin;
				PassParameters->GrassTime = ViewData.Time;

				PassParameters->FieldMin = FieldMin;
				PassParameters->FieldMax = FieldMax;
				PassParameters->TileSize = Settings.TileSize;
				PassParameters->BladesPerSideLod0 = (uint32)Settings.BladesPerTileSide;
				PassParameters->DispatchLod = (uint32)Lod;
				PassParameters->TileOffset = (uint32)VisibleTiles.Offsets[Lod];
				PassParameters->MaxLod = (uint32)(Settings.NumLods - 1);
				PassParameters->Lod0Distance = Lod0Distance;
				PassParameters->MaxDrawDistance = Settings.MaxDrawDistance;
				PassParameters->WidthLodScale = Settings.WidthLodScale;
				PassParameters->MaxBladesHigh = DrawBuffers.MaxBladesHigh;
				PassParameters->MaxBladesLow = DrawBuffers.MaxBladesLow;

				PassParameters->BladeHeight = Settings.BladeHeight;
				PassParameters->HeightVariation = Settings.HeightVariation;
				PassParameters->BladeWidth = Settings.BladeWidth;
				PassParameters->BaseTilt = Settings.Tilt;
				PassParameters->TiltVariation = Settings.TiltVariation;
				PassParameters->BaseBend = Settings.Bend;

				PassParameters->ClumpSize = Settings.ClumpSize;
				PassParameters->ClumpPull = Settings.ClumpPull;
				PassParameters->ClumpHeightVariation = Settings.ClumpHeightVariation;
				PassParameters->ClumpFacing = Settings.ClumpFacing;

				PassParameters->StripeWidth = Settings.StripeWidth;
				PassParameters->MowStrength = Settings.MowStrength;
				PassParameters->StripeContrast = Settings.StripeContrast;
				PassParameters->ColorVariation = Settings.ColorVariation;

				PassParameters->WindDirection = Settings.WindDirection;
				PassParameters->WindStrength = Settings.WindStrength;
				PassParameters->WindSpeed = Settings.WindSpeed;
				PassParameters->WindNoiseScale = Settings.WindNoiseScale;

				for (int32 Index = 0; Index < MaxInteractors; ++Index)
				{
					const bool bUsed = Index < NumInteractors;
					PassParameters->Interactors[Index] = bUsed ? Interactors[Index * 2] : FVector4f(0.f, 0.f, 0.f, 1.f);
					PassParameters->InteractorEnds[Index] = bUsed ? Interactors[Index * 2 + 1] : FVector4f(0.f, 0.f, 0.f, 1.f);
				}
				PassParameters->NumInteractors = (uint32)NumInteractors;

				PassParameters->ConformDepthTexture = ConformDepthTexture;
				PassParameters->ConformDepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PassParameters->ConformUPlane = ConformData.UPlane;
				PassParameters->ConformVPlane = ConformData.VPlane;
				PassParameters->ConformParams = FVector4f(bConform ? 1.f : 0.f, ConformData.CameraDistance, ConformClearance, 0.f);

				const int32 BladesPerSide = FMath::Max(Settings.BladesPerTileSide >> Lod, 1);
				const int32 GroupsPerSide = FMath::DivideAndRoundUp(BladesPerSide, 8);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ProceduralGrass.GenerateBlades LOD%d (%d tiles)", Lod, NumTiles),
					ComputeShader, PassParameters, FIntVector(GroupsPerSide, GroupsPerSide, NumTiles));
			}
		}

		AddPass_GenerationScope(GraphBuilder, DrawBuffers, false);

		// Clamp the instance counts to the buffer capacity.
		{
			FGrassFinalizeArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGrassFinalizeArgsCS::FParameters>();
			PassParameters->MaxBladesHigh = DrawBuffers.MaxBladesHigh;
			PassParameters->MaxBladesLow = DrawBuffers.MaxBladesLow;
			PassParameters->RWIndirectArgsBuffer = DrawBuffers.IndirectArgsBufferUAV;

			TShaderMapRef<FGrassFinalizeArgsCS> ComputeShader(ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ProceduralGrass.FinalizeArgs"), ComputeShader, PassParameters, FIntVector(1, 1, 1));
		}
	}

	AddPass_TransitionDrawBuffers(GraphBuilder, Buffers, UsedBufferIndices, false);
}

// -------------------------------------------------------------------------------------------------
// Scene proxy
// -------------------------------------------------------------------------------------------------

static const FName NAME_ProceduralGrass(TEXT("ProceduralGrass"));

FGrassSceneProxy::FGrassSceneProxy(UProceduralGrassComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent, NAME_ProceduralGrass)
{
	GGrassRendererExtension.RegisterExtension();

	InComponent->BuildSettings(Settings);
	Interactors = InComponent->GetLocalInteractors();

	// The grass vertex factory does not go through GPU Scene, so it needs the primitive uniform buffer.
	bVFRequiresPrimitiveUniformBuffer = true;
	bHasDeformableMesh = false;

	UMaterialInterface* ComponentMaterial = InComponent->Material;
	const bool bValidMaterial = ComponentMaterial != nullptr && ComponentMaterial->CheckMaterialUsage_Concurrent(MATUSAGE_VirtualHeightfieldMesh);
	Material = bValidMaterial ? ComponentMaterial->GetRenderProxy() : UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
	MaterialRelevance = Material->GetMaterialInterface()->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
}

SIZE_T FGrassSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FGrassSceneProxy::GetMemoryFootprint() const
{
	return (sizeof(*this) + FPrimitiveSceneProxy::GetAllocatedSize());
}

void FGrassSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	VertexFactory = new FGrassVertexFactory(GetScene().GetFeatureLevel());
	VertexFactory->InitResource(RHICmdList);
}

void FGrassSceneProxy::DestroyRenderThreadResources()
{
	if (VertexFactory != nullptr)
	{
		VertexFactory->ReleaseResource();
		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

void FGrassSceneProxy::SetInteractors_RenderThread(const TArray<FVector4f>& InInteractors)
{
	Interactors = InInteractors;
}

void FGrassSceneProxy::SetConformData_RenderThread(const FGrassConformData& InConformData)
{
	ConformData = InConformData;
}

FPrimitiveViewRelevance FGrassSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	const bool bValid = VertexFactory != nullptr && CVarGrassEnable.GetValueOnAnyThread() != 0;

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = bValid && IsShown(View);
	Result.bShadowRelevance = false;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = false;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
	return Result;
}

void FGrassSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (VertexFactory == nullptr)
	{
		return;
	}

	FScopeLock ScopeLock(&GGrassRendererExtension.GetLock());

	if (GGrassRendererExtension.IsInFrame())
	{
		// The generation passes for this frame were already submitted, new work cannot be added any more.
		return;
	}

	const float Time = (float)ViewFamily.Time.GetWorldTimeSeconds();
	const float PrevTime = Time - (float)ViewFamily.Time.GetDeltaWorldTimeSeconds();

	const FVector2f FieldMin = Settings.FieldSize * -0.5f;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (!(VisibilityMap & (1 << ViewIndex)))
		{
			continue;
		}

		const FSceneView* View = Views[ViewIndex];

		ProceduralGrass::FViewData ViewData;
		ViewData.ViewOrigin = View->ViewMatrices.GetViewOrigin();
		ViewData.WorldToClip = View->ViewMatrices.GetViewProjectionMatrix();
		ViewData.ViewFrustum = View->ViewFrustum;
		ViewData.Time = Time;

		// Converts a size in world units into a conservative clip space margin for the per blade frustum test.
		const FMatrix& ViewToClip = View->ViewMatrices.GetViewToClip();
		ViewData.CullMargin = FMath::Max3(1.0f, (float)FMath::Abs(ViewToClip.M[0][0]), (float)FMath::Abs(ViewToClip.M[1][1]));

		const int32 BufferIndex = GGrassRendererExtension.AddWork(Collector.GetRHICommandList(), this, View, ViewData);
		const ProceduralGrass::FDrawBuffers& DrawBuffers = GGrassRendererExtension.GetBuffers(BufferIndex);

		// Two draws that share the instance buffer: detailed blades near the camera, simple ones further away.
		for (int32 DrawIndex = 0; DrawIndex < 2; ++DrawIndex)
		{
			const bool bHighDetail = DrawIndex == 0;
			const FGrassBladeIndexBuffer& IndexBuffer = bHighDetail ? VertexFactory->GetHighDetailIndexBuffer() : VertexFactory->GetLowDetailIndexBuffer();

			FMeshBatch& Mesh = Collector.AllocateMesh();
			Mesh.bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
			Mesh.bUseWireframeSelectionColoring = IsSelected();
			Mesh.VertexFactory = VertexFactory;
			Mesh.MaterialRenderProxy = Material;
			Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
			Mesh.bDisableBackfaceCulling = true;
			Mesh.Type = PT_TriangleList;
			Mesh.DepthPriorityGroup = SDPG_World;
			Mesh.bCanApplyViewModeOverrides = true;
			Mesh.bUseForMaterial = true;
			Mesh.CastShadow = false;
			Mesh.bUseForDepthPass = true;

			Mesh.Elements.SetNumZeroed(1);
			{
				FMeshBatchElement& BatchElement = Mesh.Elements[0];

				BatchElement.IndexBuffer = &IndexBuffer;
				BatchElement.IndirectArgsBuffer = DrawBuffers.IndirectArgsBuffer;
				BatchElement.IndirectArgsOffset = bHighDetail ? ProceduralGrass::IndirectArgsOffsetHigh : ProceduralGrass::IndirectArgsOffsetLow;

				BatchElement.FirstIndex = 0;
				BatchElement.NumPrimitives = 0;	// 0 = use the indirect args
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = 0;

				BatchElement.PrimitiveIdMode = PrimID_ForceZero;
				BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();

				FGrassBatchUserData* UserData = &Collector.AllocateOneFrameResource<FGrassBatchUserData>();
				BatchElement.UserData = (void*)UserData;

				UserData->InstanceBufferSRV = DrawBuffers.InstanceBufferSRV;
				UserData->DrawParams = FVector4f(
					bHighDetail ? 0.f : (float)DrawBuffers.MaxBladesHigh,
					(float)IndexBuffer.GetNumSegments(),
					Time,
					PrevTime);
				UserData->ShapeParams = FVector4f(Settings.NormalRounding, Settings.ViewThickening, Settings.BobAmplitude, Settings.BobSpeed);
				UserData->RootColor = FVector4f(Settings.RootColor.R, Settings.RootColor.G, Settings.RootColor.B, 1.f);
				UserData->TipColor = FVector4f(Settings.TipColor.R, Settings.TipColor.G, Settings.TipColor.B, 1.f);
				UserData->FieldParams = FVector4f(FieldMin.X, FieldMin.Y, 1.f / Settings.FieldSize.X, 1.f / Settings.FieldSize.Y);
				// Wave number = angular speed / travel speed, so the bob wave moves with the gusts.
				UserData->WindParams = FVector4f(Settings.WindDirection.X, Settings.WindDirection.Y, Settings.BobSpeed / FMath::Max(Settings.WindSpeed, 1.f), 0.f);
			}

			Collector.AddMesh(ViewIndex, Mesh);
		}
	}
}
