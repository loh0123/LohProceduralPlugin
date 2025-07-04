// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LohProceduralPluginMarching : ModuleRules
{
	public LohProceduralPluginMarching(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
				// ... add public include paths required here ...
			}
		);


		PrivateIncludePaths.AddRange(
			new string[]
			{
				// ... add other private include paths required here ...
			}
		);


		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"LohFunctionPlugin",
				"GameplayTags",
				"GeometryFramework",
				"GeometryCore",
				"LohProceduralPlugin"
				// ... add other public dependencies that you statically link with here ...
			}
		);


		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"LohFunctionPlugin",
				"LohProceduralPlugin",
				"MeshConversionEngineTypes",
				"DynamicMesh"
				// ... add private dependencies that you statically link with here ...
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}