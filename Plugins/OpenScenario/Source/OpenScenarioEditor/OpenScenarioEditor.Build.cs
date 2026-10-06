using UnrealBuildTool;

public class OpenScenarioEditor : ModuleRules
{
	public OpenScenarioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"OpenScenario",
			"OpenDrive"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"EditorFramework",
			"PropertyEditor",
			"DesktopPlatform",
			"AssetTools",
			"AssetRegistry",
			"AssetDefinition",
			"WorkspaceMenuStructure",
			"ClassViewer",
			"OpenDriveEditor"
		});
	}
}
