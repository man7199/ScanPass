#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FProceduralGrassModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Makes the plugin's Shaders folder visible to the shader compiler as /Plugin/ProceduralGrass.
		// This is why the module loads at PostConfigInit: the mapping has to exist before shader types are compiled.
		const FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("ProceduralGrass"))->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/ProceduralGrass"), PluginShaderDir);
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FProceduralGrassModule, ProceduralGrass);
