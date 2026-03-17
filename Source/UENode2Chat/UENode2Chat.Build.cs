using UnrealBuildTool;

public class UENode2Chat : ModuleRules
{
    public UENode2Chat(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "Slate",
    "SlateCore",
    "GraphEditor",
    "Kismet",
    "KismetCompiler",
    "UnrealEd",
       "InputCore",
    "ApplicationCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "UnrealEd",       // Needed for Blueprint editor integration
            "Kismet",         // Blueprint nodes
            "KismetCompiler", // optional if handling node compilation
            "BlueprintGraph", // UEdGraph nodes
            "Slate",          // FUICommandList implementation
            "SlateCore",      // Base Slate functionality
            "EditorStyle",    // Needed for UI_COMMAND macro / MapAction
            "LevelEditor"     // Optional if you want menu/toolbar buttons
        });

    }
}
