using UnrealBuildTool;

public class AITools : ModuleRules
{
	public AITools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"AssetRegistry",
			"AssetTools",
			"UnrealEd",
		});
	}
}
