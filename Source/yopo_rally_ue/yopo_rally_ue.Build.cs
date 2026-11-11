// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class yopo_rally_ue : ModuleRules
{
	public yopo_rally_ue(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "ROSIntegration", "ScoutMiniSimulation" });
		// Project-local message converters extend the existing plugin without editing it.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "../../Plugins/ROSIntegration/Source/ROSIntegration/Private"));
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "../../Plugins/ROSIntegration/Source/ROSIntegration/Private/rosbridge2cpp"));

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
