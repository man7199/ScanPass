using UnrealBuildTool;
using System.Collections.Generic;

public class footballEditorTarget : TargetRules
{
	public footballEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("football");
	}
}
