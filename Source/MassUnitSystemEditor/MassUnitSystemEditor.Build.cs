// Copyright Digi Logic Labs LLC. All Rights Reserved.

using UnrealBuildTool;

public class MassUnitSystemEditor : ModuleRules
{
	public MassUnitSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MassUnitSystemRuntime"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"UnrealEd",
				"MassEntity",
				"GameplayTags",
				"Slate",
				"SlateCore"
			}
		);
	}
}
