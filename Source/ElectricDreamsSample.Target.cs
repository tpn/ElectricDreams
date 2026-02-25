// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class ElectricDreamsSampleTarget : TargetRules
{
	public ElectricDreamsSampleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		bOverrideBuildEnvironment = true;
		bUseConsoleInShipping = true;

		ExtraModuleNames.AddRange( new string[] { "ElectricDreamsSample" } );
	}
}
