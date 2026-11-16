// Builds the default grass material in code, so the plugin needs no content and no scripts.
// Editor only: material graphs do not exist in packaged builds, the saved asset is what ships.

#include "ProceduralGrassComponent.h"

#if WITH_EDITOR

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace ProceduralGrass
{
	static const TCHAR* DefaultMaterialPackage = TEXT("/Game/Grass/M_ProceduralGrass");
	static const TCHAR* DefaultMaterialName = TEXT("M_ProceduralGrass");

	/** Outputs of the Vertex Color node. */
	static const int32 VertexColorOutput_RGB = 0;
	static const int32 VertexColorOutput_Alpha = 4;

	template <typename ExpressionType>
	static ExpressionType* AddExpression(UMaterial* Material, int32 X, int32 Y)
	{
		ExpressionType* Expression = NewObject<ExpressionType>(Material, NAME_None, RF_Transactional);
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	static UMaterialExpressionConstant* AddConstant(UMaterial* Material, float Value, int32 X, int32 Y)
	{
		UMaterialExpressionConstant* Constant = AddExpression<UMaterialExpressionConstant>(Material, X, Y);
		Constant->R = Value;
		return Constant;
	}

	/**
	 * The vertex factory already bakes the blade colour (root/tip gradient, brightness variation, mowing
	 * stripes) into Vertex Color and stores 0..1 along the blade in its alpha. The material only routes
	 * those into the shading inputs.
	 */
	static UMaterial* CreateDefaultGrassMaterial()
	{
		UPackage* Package = CreatePackage(DefaultMaterialPackage);
		UMaterial* Material = NewObject<UMaterial>(Package, DefaultMaterialName, RF_Public | RF_Standalone | RF_Transactional);

		Material->TwoSided = true;
		Material->SetShadingModel(MSM_TwoSidedFoliage);
		// The grass shaders are compiled for materials with this usage flag, see FGrassVertexFactory::ShouldCompilePermutation.
		Material->bUsedWithVirtualHeightfieldMesh = true;

		UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();

		UMaterialExpressionVertexColor* VertexColor = AddExpression<UMaterialExpressionVertexColor>(Material, -700, 0);

		// Base colour straight from the vertex colour.
		Inputs->BaseColor.Connect(VertexColorOutput_RGB, VertexColor);

		// Light coming through the blade from behind: a brighter, yellower version of the base colour.
		UMaterialExpressionConstant3Vector* Tint = AddExpression<UMaterialExpressionConstant3Vector>(Material, -700, 250);
		Tint->Constant = FLinearColor(1.6f, 1.8f, 0.6f);

		UMaterialExpressionMultiply* Subsurface = AddExpression<UMaterialExpressionMultiply>(Material, -400, 200);
		Subsurface->A.Connect(VertexColorOutput_RGB, VertexColor);
		Subsurface->B.Connect(0, Tint);
		Inputs->SubsurfaceColor.Connect(0, Subsurface);

		// Darker towards the root, where neighbouring blades block the sky.
		UMaterialExpressionLinearInterpolate* Occlusion = AddExpression<UMaterialExpressionLinearInterpolate>(Material, -400, 450);
		Occlusion->ConstA = 0.35f;
		Occlusion->ConstB = 1.0f;
		Occlusion->Alpha.Connect(VertexColorOutput_Alpha, VertexColor);
		Inputs->AmbientOcclusion.Connect(0, Occlusion);

		Inputs->Roughness.Connect(0, AddConstant(Material, 0.55f, -400, 650));
		Inputs->Specular.Connect(0, AddConstant(Material, 0.25f, -400, 720));

		// Compiles the material.
		Material->PostEditChange();

		FAssetRegistryModule::AssetCreated(Material);
		Package->MarkPackageDirty();

		return Material;
	}
}

void UProceduralGrassComponent::CreateDefaultMaterial()
{
	using namespace ProceduralGrass;

	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), DefaultMaterialPackage, DefaultMaterialName);

	// Reuse the material if it is already in memory or on disk, so edits made to it are not thrown away.
	UMaterial* GrassMaterial = FindObject<UMaterial>(nullptr, *ObjectPath);
	if (GrassMaterial == nullptr && FPackageName::DoesPackageExist(DefaultMaterialPackage))
	{
		GrassMaterial = LoadObject<UMaterial>(nullptr, *ObjectPath);
	}
	if (GrassMaterial == nullptr)
	{
		GrassMaterial = CreateDefaultGrassMaterial();
		UE_LOG(LogTemp, Display, TEXT("ProceduralGrass: created %s. Use File > Save All to keep it."), DefaultMaterialPackage);
	}

	Modify();
	SetMaterial(0, GrassMaterial);
}

#endif // WITH_EDITOR
