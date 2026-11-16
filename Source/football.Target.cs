using UnrealBuildTool;
using System.Collections.Generic;

public class footballTarget : TargetRules
{
	public footballTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("football");
	}
}
