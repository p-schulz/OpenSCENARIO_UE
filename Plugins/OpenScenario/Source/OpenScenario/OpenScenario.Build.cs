using UnrealBuildTool;

public class OpenScenario : ModuleRules
{
	public OpenScenario(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"OpenDrive"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"XmlParser"
		});
	}
}
