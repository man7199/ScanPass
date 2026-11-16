using UnrealBuildTool;

public class ProceduralGrass : ModuleRules
{
	public ProceduralGrass(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"AssetRegistry",
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
		});
	}
}
