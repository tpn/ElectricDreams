// Copyright Epic Games, Inc. All Rights Reserved.

#include "ElectricDreamsHotkeySubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"

namespace ElectricDreamsLighting
{
	struct FLightingPreset
	{
		const TCHAR* Name;
		float SunPitch;
		float SunIntensity;
		FLinearColor SunColor;
		float SkyIntensity;
		float FogDensity;
		FLinearColor FogColor;
	};

	static const FLightingPreset Presets[] = {
		{ TEXT("Dawn"), -12.0f, 22000.0f, FLinearColor(1.00f, 0.77f, 0.56f), 0.65f, 0.0100f, FLinearColor(0.72f, 0.55f, 0.46f) },
		{ TEXT("Midday"), -58.0f, 95000.0f, FLinearColor(1.00f, 0.97f, 0.92f), 1.10f, 0.0025f, FLinearColor(0.63f, 0.74f, 0.92f) },
		{ TEXT("Dusk"), -2.0f, 12000.0f, FLinearColor(1.00f, 0.58f, 0.36f), 0.40f, 0.0120f, FLinearColor(0.44f, 0.31f, 0.40f) },
		{ TEXT("Night"), 8.0f, 0.35f, FLinearColor(0.36f, 0.48f, 0.78f), 0.18f, 0.0180f, FLinearColor(0.05f, 0.08f, 0.18f) }
	};
}

namespace ElectricDreamsHotkeys
{
	constexpr int32 HelpMessageKey = 9123401;
	constexpr float MinMovementRateMultiplier = 1.0e-3f;
	constexpr float MaxMovementRateMultiplier = 1.0e3f;
	constexpr double VrEnableRetryIntervalSeconds = 0.25;
	constexpr int32 VrEnableRetryAttempts = 24;
	constexpr int32 DlssgDisabledValue = 0;
	constexpr int32 DlssSrDisabledValue = 0;
	constexpr int32 DeepDvcDisabledValue = 0;
	constexpr int32 HiddenAreaMaskDisabledValue = 0;
	constexpr int32 OpenXRDepthLayerDisabledValue = 0;
	const TCHAR* DlssgCVarName = TEXT("r.Streamline.DLSSG.Enable");
	const TCHAR* DlssSrCVarName = TEXT("r.NGX.DLSS.Enable");
	const TCHAR* DeepDvcCVarName = TEXT("r.Streamline.DeepDVC.Enable");
	const TCHAR* HiddenAreaMaskCVarName = TEXT("vr.HiddenAreaMask");
	const TCHAR* OpenXRDepthLayerCVarName = TEXT("xr.OpenXRAllowDepthLayer");
}

TStatId UElectricDreamsHotkeySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UElectricDreamsHotkeySubsystem, STATGROUP_Tickables);
}

void UElectricDreamsHotkeySubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}

	if (!bLightingPresetApplied)
	{
		ApplyLightingPreset(false);
	}

	if (!bAutoVrStartupAttempted)
	{
		bAutoVrStartupAttempted = true;

		bool bShouldStartInVr = false;
		if (GConfig != nullptr &&
			GConfig->GetBool(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("bStartInVR"), bShouldStartInVr, GGameIni) &&
			bShouldStartInVr)
		{
			const FVrRuntimeState CurrentVrState = QueryVrRuntimeState();
			if (!IsVrFullyActive(CurrentVrState))
			{
				StartVrEnableRetry();
			}
		}
	}

	TickVrEnableRetry();
	SyncVrRuntimeCvars();

	if (PlayerController->WasInputKeyJustPressed(EKeys::F1) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_LeftThumbstick))
	{
		ToggleHelpOverlay();
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Equals) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
	{
		AdjustMovementRateMultiplier(1.0f);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Hyphen) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left))
	{
		AdjustMovementRateMultiplier(-1.0f);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::PageDown) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_RightShoulder) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
	{
		CycleLevel(true);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::PageUp) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Left))
	{
		CycleLevel(false);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::F7) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Up))
	{
		CycleLightingPreset(true);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::F6) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Down))
	{
		CycleLightingPreset(false);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::F9) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_RightThumbstick))
	{
		ToggleYAxisInversion(PlayerController);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::F10) ||
		PlayerController->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right))
	{
		ToggleVrMode();
	}

	ApplyVerticalReposition(PlayerController, DeltaTime);

	if (bShowHelpOverlay)
	{
		DrawHelpOverlay();
	}
}

void UElectricDreamsHotkeySubsystem::ToggleHelpOverlay()
{
	bShowHelpOverlay = !bShowHelpOverlay;

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Green,
			FString::Printf(TEXT("Help Overlay %s"), bShowHelpOverlay ? TEXT("Shown") : TEXT("Hidden"))
		);
	}
}

void UElectricDreamsHotkeySubsystem::DrawHelpOverlay() const
{
	if (GEngine == nullptr)
	{
		return;
	}

	const float MovementRateMultiplier = GetMovementRateMultiplier();
	int32 DlssgValue = -1;
	int32 DlssSrValue = -1;
	static IConsoleVariable* DlssgCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssgCVarName);
	static IConsoleVariable* DlssSrCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssSrCVarName);
	if (DlssgCVar != nullptr)
	{
		DlssgValue = DlssgCVar->GetInt();
	}
	if (DlssSrCVar != nullptr)
	{
		DlssSrValue = DlssSrCVar->GetInt();
	}
	const FString HelpText = FString::Printf(
		TEXT("HOTKEYS / CONTROLLER\n")
		TEXT("F1 / L3: Toggle help overlay\n")
		TEXT("PageDown / RB / A: Next level\n")
		TEXT("PageUp / LB / X: Previous level\n")
		TEXT("F7 / DPad Up: Next lighting preset\n")
		TEXT("F6 / DPad Down: Previous lighting preset\n")
		TEXT("F9 / R3: Toggle Y inversion\n")
		TEXT("= / DPad Right: Increase movement rate (x e)\n")
		TEXT("- / DPad Left: Decrease movement rate (x 1/e)\n")
		TEXT("Home / RT: Move up\n")
		TEXT("End / LT: Move down\n")
		TEXT("F10 / Menu: Toggle VR\n")
		TEXT("Movement rate multiplier: %.6g\n")
		TEXT("DLSS Frame Gen in current mode: %s\n")
		TEXT("DLSS Super Resolution in current mode: %s"),
		MovementRateMultiplier,
		DlssgValue >= 0 ? (DlssgValue != 0 ? TEXT("ON") : TEXT("OFF")) : TEXT("Unavailable"),
		DlssSrValue >= 0 ? (DlssSrValue != 0 ? TEXT("ON") : TEXT("OFF")) : TEXT("Unavailable")
	);

	GEngine->AddOnScreenDebugMessage(
		ElectricDreamsHotkeys::HelpMessageKey,
		0.15f,
		FColor::Green,
		HelpText
	);
}

float UElectricDreamsHotkeySubsystem::GetMovementRateMultiplier() const
{
	if (const IConsoleVariable* MovementRateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("HoverDrone.MovementRateMultiplier")))
	{
		return FMath::Clamp(
			MovementRateCVar->GetFloat(),
			ElectricDreamsHotkeys::MinMovementRateMultiplier,
			ElectricDreamsHotkeys::MaxMovementRateMultiplier
		);
	}

	return 1.0f;
}

void UElectricDreamsHotkeySubsystem::AdjustMovementRateMultiplier(float LogDelta)
{
	if (IConsoleVariable* MovementRateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("HoverDrone.MovementRateMultiplier")))
	{
		const float CurrentMultiplier = FMath::Max(MovementRateCVar->GetFloat(), KINDA_SMALL_NUMBER);
		const float NewMultiplier = FMath::Clamp(
			CurrentMultiplier * FMath::Exp(LogDelta),
			ElectricDreamsHotkeys::MinMovementRateMultiplier,
			ElectricDreamsHotkeys::MaxMovementRateMultiplier
		);

		MovementRateCVar->Set(NewMultiplier, ECVF_SetByGameSetting);

		if (GConfig != nullptr)
		{
			GConfig->SetFloat(
				TEXT("/Script/ElectricDreamsSample.Hotkeys"),
				TEXT("HoverDroneMovementRateMultiplier"),
				NewMultiplier,
				GGameUserSettingsIni
			);
			GConfig->Flush(false, GGameUserSettingsIni);
		}

		if (GEngine != nullptr)
		{
		GEngine->AddOnScreenDebugMessage(
			-1,
			3.0f,
			FColor::Green,
			FString::Printf(TEXT("Movement rate multiplier: %.6g"), NewMultiplier)
		);
	}
	}
	else if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("HoverDrone.MovementRateMultiplier cvar not found."));
	}
}

void UElectricDreamsHotkeySubsystem::ToggleYAxisInversion(APlayerController* PlayerController)
{
	if (PlayerController == nullptr)
	{
		return;
	}

	TArray<FString> ChangeDetails;

	if (IConsoleVariable* InvertLookYCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("HoverDrone.InvertLookY")))
	{
		const bool bCurrentInverted = InvertLookYCVar->GetInt() != 0;
		const bool bNewInverted = !bCurrentInverted;
		InvertLookYCVar->Set(bNewInverted ? 1 : 0, ECVF_SetByGameSetting);
		ChangeDetails.Add(FString::Printf(TEXT("HoverDrone.InvertLookY -> %s"), bNewInverted ? TEXT("Inverted") : TEXT("Normal")));
	}
	else
	{
		ChangeDetails.Add(TEXT("HoverDrone.InvertLookY cvar not found."));
	}

	if (GEngine != nullptr)
	{
		const FString Details = ChangeDetails.Num() > 0
			? FString::Join(ChangeDetails, TEXT("; "))
			: FString(TEXT("No Y-axis setting found to toggle."));
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			FColor::Green,
			FString::Printf(TEXT("Y Inversion Updated: %s"), *Details)
		);
	}
}

void UElectricDreamsHotkeySubsystem::ApplyVerticalReposition(APlayerController* PlayerController, float DeltaTime)
{
	if (PlayerController == nullptr || DeltaTime <= 0.0f)
	{
		return;
	}

	float VerticalInput = 0.0f;
	if (PlayerController->IsInputKeyDown(EKeys::Home))
	{
		VerticalInput += 1.0f;
	}
	if (PlayerController->IsInputKeyDown(EKeys::End))
	{
		VerticalInput -= 1.0f;
	}

	VerticalInput += PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_RightTriggerAxis);
	VerticalInput -= PlayerController->GetInputAnalogKeyState(EKeys::Gamepad_LeftTriggerAxis);

	if (FMath::IsNearlyZero(VerticalInput))
	{
		return;
	}

	AActor* TargetActor = PlayerController->GetPawn();
	if (TargetActor == nullptr)
	{
		TargetActor = PlayerController->GetViewTarget();
	}
	if (TargetActor == nullptr)
	{
		return;
	}

	constexpr float VerticalUnitsPerSecond = 1200.0f;
	const FVector VerticalOffset(0.0f, 0.0f, VerticalInput * VerticalUnitsPerSecond * DeltaTime);
	TargetActor->AddActorWorldOffset(VerticalOffset, false, nullptr, ETeleportType::TeleportPhysics);
}

void UElectricDreamsHotkeySubsystem::CycleLightingPreset(bool bForward)
{
	constexpr int32 PresetCount = UE_ARRAY_COUNT(ElectricDreamsLighting::Presets);
	const int32 Direction = bForward ? 1 : -1;
	LightingPresetIndex = (LightingPresetIndex + Direction + PresetCount) % PresetCount;
	ApplyLightingPreset(true);
}

void UElectricDreamsHotkeySubsystem::ApplyLightingPreset(bool bShowMessage)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	constexpr int32 PresetCount = UE_ARRAY_COUNT(ElectricDreamsLighting::Presets);
	LightingPresetIndex = FMath::Clamp(LightingPresetIndex, 0, PresetCount - 1);
	const ElectricDreamsLighting::FLightingPreset& Preset = ElectricDreamsLighting::Presets[LightingPresetIndex];

	int32 DirectionalLightCount = 0;
	int32 SkyLightCount = 0;
	int32 FogCount = 0;

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;

		TInlineComponentArray<UDirectionalLightComponent*> DirectionalLights(Actor);
		for (UDirectionalLightComponent* DirectionalLight : DirectionalLights)
		{
			if (DirectionalLight == nullptr)
			{
				continue;
			}

			FRotator NewRotation = DirectionalLight->GetComponentRotation();
			NewRotation.Pitch = Preset.SunPitch;
			DirectionalLight->SetWorldRotation(NewRotation);
			DirectionalLight->SetIntensity(Preset.SunIntensity);
			DirectionalLight->SetLightColor(Preset.SunColor, false);
			DirectionalLight->MarkRenderStateDirty();
			++DirectionalLightCount;
		}

		TInlineComponentArray<USkyLightComponent*> SkyLights(Actor);
		for (USkyLightComponent* SkyLight : SkyLights)
		{
			if (SkyLight == nullptr)
			{
				continue;
			}

			SkyLight->SetIntensity(Preset.SkyIntensity);
			SkyLight->RecaptureSky();
			++SkyLightCount;
		}

		TInlineComponentArray<UExponentialHeightFogComponent*> FogComponents(Actor);
		for (UExponentialHeightFogComponent* FogComponent : FogComponents)
		{
			if (FogComponent == nullptr)
			{
				continue;
			}

			FogComponent->SetFogDensity(Preset.FogDensity);
			FogComponent->SetFogInscatteringColor(Preset.FogColor);
			FogComponent->MarkRenderStateDirty();
			++FogCount;
		}
	}

	bLightingPresetApplied = true;

	if (bShowMessage && GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			4.0f,
			FColor::Green,
			FString::Printf(
				TEXT("Lighting: %s (Directional=%d, Sky=%d, Fog=%d)"),
				Preset.Name,
				DirectionalLightCount,
				SkyLightCount,
				FogCount
			)
		);
	}
}

void UElectricDreamsHotkeySubsystem::CycleLevel(bool bForward)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	static const TArray<FString> LevelRotation = {
		TEXT("/Game/TropicalIslandPack/Maps/MainLevel/TropicalIsland_Boat_Cinematic_Map"),
		TEXT("/Game/TropicalIslandPack/Maps/MainLevel/TropicalIsland_Map"),
		TEXT("/Game/TropicalIslandPack/Maps/Sublevel/TropicalIslandMap_Environment"),
		TEXT("/Game/Levels/ElectricDreams_Env"),
		TEXT("/Game/TropicalIslandPack/Maps/MainLevel/TropicalIsland_Map_Overcast")
	};

	FString CurrentMapName = World->GetMapName();
	if (!World->StreamingLevelsPrefix.IsEmpty())
	{
		CurrentMapName.RemoveFromStart(World->StreamingLevelsPrefix);
	}

	int32 CurrentIndex = INDEX_NONE;
	for (int32 Index = 0; Index < LevelRotation.Num(); ++Index)
	{
		const FString ShortName = FPackageName::GetShortName(LevelRotation[Index]);
		if (ShortName.Equals(CurrentMapName, ESearchCase::IgnoreCase))
		{
			CurrentIndex = Index;
			break;
		}
	}

	int32 NextIndex = 0;
	if (CurrentIndex != INDEX_NONE)
	{
		const int32 Direction = bForward ? 1 : -1;
		NextIndex = (CurrentIndex + Direction + LevelRotation.Num()) % LevelRotation.Num();
	}

	const FString& NextLevel = LevelRotation[NextIndex];
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			2.0f,
			FColor::Green,
			FString::Printf(TEXT("Loading: %s"), *FPackageName::GetShortName(NextLevel))
		);
	}

	UGameplayStatics::OpenLevel(World, FName(*NextLevel));
}

void UElectricDreamsHotkeySubsystem::ToggleVrMode()
{
	if (GEngine == nullptr)
	{
		return;
	}

	const FVrRuntimeState InitialState = QueryVrRuntimeState();
	const bool bVrIsActive = IsVrFullyActive(InitialState);
	if (bVrIsActive)
	{
		StopVrEnableRetry();

		FVrRuntimeState NewState;
		const bool bDisableApplied = TryApplyVrState(false, NewState);
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			bDisableApplied ? FColor::Green : FColor::Yellow,
			FString::Printf(
				TEXT("VR Disable Requested | XR=%s | HMDConnected=%d HMDEnabled=%d StereoEnabled=%d"),
				*NewState.XrSystemName,
				NewState.bHmdConnected ? 1 : 0,
				NewState.bHmdEnabled ? 1 : 0,
				NewState.bStereoEnabled ? 1 : 0
			)
		);
		return;
	}

	if (!InitialState.bHasXrSystem || !InitialState.bHasHmdDevice || !InitialState.bHasStereoDevice)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			6.0f,
			FColor::Red,
			FString::Printf(
				TEXT("VR unavailable. XR=%s HMD=%d Stereo=%d. Ensure OpenXR is enabled and SteamVR is running."),
				*InitialState.XrSystemName,
				InitialState.bHasHmdDevice ? 1 : 0,
				InitialState.bHasStereoDevice ? 1 : 0
			)
		);
		return;
	}

	static IConsoleVariable* DlssgCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssgCVarName);
	static IConsoleVariable* DlssSrCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssSrCVarName);
	static IConsoleVariable* DeepDvcCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DeepDvcCVarName);
	static IConsoleVariable* HiddenAreaMaskCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::HiddenAreaMaskCVarName);
	static IConsoleVariable* OpenXRDepthLayerCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::OpenXRDepthLayerCVarName);
	if (!bHasSyncedVrState || !bLastSyncedVrActive)
	{
		if (DlssgCVar != nullptr)
		{
			CachedNonVrDlssgValue = DlssgCVar->GetInt();
		}
		if (DlssSrCVar != nullptr)
		{
			CachedNonVrDlssSrValue = DlssSrCVar->GetInt();
		}
		if (DeepDvcCVar != nullptr)
		{
			CachedNonVrDeepDvcValue = DeepDvcCVar->GetInt();
		}
		if (HiddenAreaMaskCVar != nullptr)
		{
			CachedNonVrHiddenAreaMaskValue = HiddenAreaMaskCVar->GetInt();
		}
		if (OpenXRDepthLayerCVar != nullptr)
		{
			CachedNonVrOpenXRDepthLayerValue = OpenXRDepthLayerCVar->GetInt();
		}
	}

	if (DlssgCVar != nullptr)
	{
		DlssgCVar->Set(ElectricDreamsHotkeys::DlssgDisabledValue, ECVF_SetByGameSetting);
	}
	if (DlssSrCVar != nullptr)
	{
		DlssSrCVar->Set(ElectricDreamsHotkeys::DlssSrDisabledValue, ECVF_SetByGameSetting);
	}
	if (DeepDvcCVar != nullptr)
	{
		DeepDvcCVar->Set(ElectricDreamsHotkeys::DeepDvcDisabledValue, ECVF_SetByGameSetting);
	}
	if (HiddenAreaMaskCVar != nullptr)
	{
		HiddenAreaMaskCVar->Set(ElectricDreamsHotkeys::HiddenAreaMaskDisabledValue, ECVF_SetByGameSetting);
	}
	if (OpenXRDepthLayerCVar != nullptr)
	{
		OpenXRDepthLayerCVar->Set(ElectricDreamsHotkeys::OpenXRDepthLayerDisabledValue, ECVF_SetByGameSetting);
	}

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Yellow,
			TEXT("VR preflight: DLSS SR/FG, DeepDVC, hidden area mask and depth layer forced OFF.")
		);
	}

	StartVrEnableRetry();
}

void UElectricDreamsHotkeySubsystem::TickVrEnableRetry()
{
	if (!bVrEnableRetryActive || GEngine == nullptr)
	{
		return;
	}

	const double CurrentSeconds = FPlatformTime::Seconds();
	if (CurrentSeconds < NextVrEnableAttemptTimeSeconds)
	{
		return;
	}

	FVrRuntimeState StateAfterAttempt;
	const bool bEnableApplied = TryApplyVrState(true, StateAfterAttempt);
	const bool bVrActive = bEnableApplied && IsVrFullyActive(StateAfterAttempt);

	if (bVrActive)
	{
		StopVrEnableRetry();
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Green,
			FString::Printf(
				TEXT("VR Enabled | XR=%s | HMDConnected=%d HMDEnabled=%d StereoEnabled=%d"),
				*StateAfterAttempt.XrSystemName,
				StateAfterAttempt.bHmdConnected ? 1 : 0,
				StateAfterAttempt.bHmdEnabled ? 1 : 0,
				StateAfterAttempt.bStereoEnabled ? 1 : 0
			)
		);
		return;
	}

	--RemainingVrEnableAttempts;
	if (RemainingVrEnableAttempts <= 0)
	{
		StopVrEnableRetry();
		GEngine->AddOnScreenDebugMessage(
			-1,
			7.0f,
			FColor::Yellow,
			FString::Printf(
				TEXT("VR enable timed out | XR=%s | HMDConnected=%d HMDEnabled=%d StereoEnabled=%d. Keep SteamVR+PSVR2 app running and retry."),
				*StateAfterAttempt.XrSystemName,
				StateAfterAttempt.bHmdConnected ? 1 : 0,
				StateAfterAttempt.bHmdEnabled ? 1 : 0,
				StateAfterAttempt.bStereoEnabled ? 1 : 0
			)
		);
		return;
	}

	NextVrEnableAttemptTimeSeconds = CurrentSeconds + ElectricDreamsHotkeys::VrEnableRetryIntervalSeconds;
}

void UElectricDreamsHotkeySubsystem::StartVrEnableRetry()
{
	bVrEnableRetryActive = true;
	RemainingVrEnableAttempts = ElectricDreamsHotkeys::VrEnableRetryAttempts;
	NextVrEnableAttemptTimeSeconds = FPlatformTime::Seconds();

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Yellow,
			TEXT("VR enable requested. Attempting to activate OpenXR/SteamVR session...")
		);
	}
}

void UElectricDreamsHotkeySubsystem::StopVrEnableRetry()
{
	bVrEnableRetryActive = false;
	RemainingVrEnableAttempts = 0;
	NextVrEnableAttemptTimeSeconds = 0.0;
}

void UElectricDreamsHotkeySubsystem::SyncVrRuntimeCvars()
{
	static IConsoleVariable* DlssgCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssgCVarName);
	static IConsoleVariable* DlssSrCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DlssSrCVarName);
	static IConsoleVariable* DeepDvcCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::DeepDvcCVarName);
	static IConsoleVariable* HiddenAreaMaskCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::HiddenAreaMaskCVarName);
	static IConsoleVariable* OpenXRDepthLayerCVar = IConsoleManager::Get().FindConsoleVariable(ElectricDreamsHotkeys::OpenXRDepthLayerCVarName);
	if (DlssgCVar == nullptr && DlssSrCVar == nullptr && DeepDvcCVar == nullptr && HiddenAreaMaskCVar == nullptr && OpenXRDepthLayerCVar == nullptr)
	{
		return;
	}

	const FVrRuntimeState VrState = QueryVrRuntimeState();
	const bool bVrActive = IsVrFullyActive(VrState);
	const int32 CurrentDlssg = DlssgCVar != nullptr ? DlssgCVar->GetInt() : ElectricDreamsHotkeys::DlssgDisabledValue;
	const int32 CurrentDlssSr = DlssSrCVar != nullptr ? DlssSrCVar->GetInt() : ElectricDreamsHotkeys::DlssSrDisabledValue;
	const int32 CurrentDeepDvc = DeepDvcCVar != nullptr ? DeepDvcCVar->GetInt() : ElectricDreamsHotkeys::DeepDvcDisabledValue;
	const int32 CurrentHiddenAreaMask = HiddenAreaMaskCVar != nullptr ? HiddenAreaMaskCVar->GetInt() : ElectricDreamsHotkeys::HiddenAreaMaskDisabledValue;
	const int32 CurrentOpenXRDepthLayer = OpenXRDepthLayerCVar != nullptr ? OpenXRDepthLayerCVar->GetInt() : ElectricDreamsHotkeys::OpenXRDepthLayerDisabledValue;

	if (bVrActive)
	{
		if (bHasSyncedVrState && !bLastSyncedVrActive)
		{
			CachedNonVrDlssgValue = CurrentDlssg;
			CachedNonVrDlssSrValue = CurrentDlssSr;
			CachedNonVrDeepDvcValue = CurrentDeepDvc;
			CachedNonVrHiddenAreaMaskValue = CurrentHiddenAreaMask;
			CachedNonVrOpenXRDepthLayerValue = CurrentOpenXRDepthLayer;
		}

		if (DlssgCVar != nullptr && CurrentDlssg != ElectricDreamsHotkeys::DlssgDisabledValue)
		{
			DlssgCVar->Set(ElectricDreamsHotkeys::DlssgDisabledValue, ECVF_SetByGameSetting);
		}
		if (DlssSrCVar != nullptr && CurrentDlssSr != ElectricDreamsHotkeys::DlssSrDisabledValue)
		{
			DlssSrCVar->Set(ElectricDreamsHotkeys::DlssSrDisabledValue, ECVF_SetByGameSetting);
		}
		if (DeepDvcCVar != nullptr && CurrentDeepDvc != ElectricDreamsHotkeys::DeepDvcDisabledValue)
		{
			DeepDvcCVar->Set(ElectricDreamsHotkeys::DeepDvcDisabledValue, ECVF_SetByGameSetting);
		}
		if (HiddenAreaMaskCVar != nullptr && CurrentHiddenAreaMask != ElectricDreamsHotkeys::HiddenAreaMaskDisabledValue)
		{
			HiddenAreaMaskCVar->Set(ElectricDreamsHotkeys::HiddenAreaMaskDisabledValue, ECVF_SetByGameSetting);
		}
		if (OpenXRDepthLayerCVar != nullptr && CurrentOpenXRDepthLayer != ElectricDreamsHotkeys::OpenXRDepthLayerDisabledValue)
		{
			OpenXRDepthLayerCVar->Set(ElectricDreamsHotkeys::OpenXRDepthLayerDisabledValue, ECVF_SetByGameSetting);
		}
	}
	else
	{
		const int32 DesiredNonVrValue = FMath::Clamp(CachedNonVrDlssgValue, 0, 1);
		if (DlssgCVar != nullptr && CurrentDlssg != DesiredNonVrValue)
		{
			DlssgCVar->Set(DesiredNonVrValue, ECVF_SetByGameSetting);
		}

		const int32 DesiredNonVrDlssSrValue = FMath::Clamp(CachedNonVrDlssSrValue, 0, 1);
		if (DlssSrCVar != nullptr && CurrentDlssSr != DesiredNonVrDlssSrValue)
		{
			DlssSrCVar->Set(DesiredNonVrDlssSrValue, ECVF_SetByGameSetting);
		}

		const int32 DesiredNonVrDeepDvcValue = FMath::Clamp(CachedNonVrDeepDvcValue, 0, 1);
		if (DeepDvcCVar != nullptr && CurrentDeepDvc != DesiredNonVrDeepDvcValue)
		{
			DeepDvcCVar->Set(DesiredNonVrDeepDvcValue, ECVF_SetByGameSetting);
		}

		const int32 DesiredNonVrHiddenAreaMaskValue = FMath::Clamp(CachedNonVrHiddenAreaMaskValue, 0, 1);
		if (HiddenAreaMaskCVar != nullptr && CurrentHiddenAreaMask != DesiredNonVrHiddenAreaMaskValue)
		{
			HiddenAreaMaskCVar->Set(DesiredNonVrHiddenAreaMaskValue, ECVF_SetByGameSetting);
		}

		const int32 DesiredNonVrOpenXRDepthLayerValue = FMath::Clamp(CachedNonVrOpenXRDepthLayerValue, 0, 1);
		if (OpenXRDepthLayerCVar != nullptr && CurrentOpenXRDepthLayer != DesiredNonVrOpenXRDepthLayerValue)
		{
			OpenXRDepthLayerCVar->Set(DesiredNonVrOpenXRDepthLayerValue, ECVF_SetByGameSetting);
		}
	}

	const bool bStateChanged = !bHasSyncedVrState || bLastSyncedVrActive != bVrActive;
	bHasSyncedVrState = true;
	bLastSyncedVrActive = bVrActive;

	if (bStateChanged && GEngine != nullptr)
	{
		const int32 FinalDlssgValue = DlssgCVar != nullptr ? DlssgCVar->GetInt() : -1;
		const int32 FinalDlssSrValue = DlssSrCVar != nullptr ? DlssSrCVar->GetInt() : -1;
		const int32 FinalDeepDvcValue = DeepDvcCVar != nullptr ? DeepDvcCVar->GetInt() : -1;
		const int32 FinalHiddenAreaMaskValue = HiddenAreaMaskCVar != nullptr ? HiddenAreaMaskCVar->GetInt() : -1;
		const int32 FinalOpenXRDepthLayerValue = OpenXRDepthLayerCVar != nullptr ? OpenXRDepthLayerCVar->GetInt() : -1;
		const FString VrDlssgState = FString::Printf(
			TEXT("VR %s | %s=%d | %s=%d | %s=%d | %s=%d | %s=%d"),
			bVrActive ? TEXT("Active") : TEXT("Inactive"),
			ElectricDreamsHotkeys::DlssgCVarName,
			FinalDlssgValue,
			ElectricDreamsHotkeys::DlssSrCVarName,
			FinalDlssSrValue,
			ElectricDreamsHotkeys::DeepDvcCVarName,
			FinalDeepDvcValue,
			ElectricDreamsHotkeys::HiddenAreaMaskCVarName,
			FinalHiddenAreaMaskValue,
			ElectricDreamsHotkeys::OpenXRDepthLayerCVarName,
			FinalOpenXRDepthLayerValue
		);
		UE_LOG(LogTemp, Display, TEXT("%s"), *VrDlssgState);
		GEngine->AddOnScreenDebugMessage(
			-1,
			5.0f,
			FColor::Green,
			VrDlssgState
		);
	}
}

bool UElectricDreamsHotkeySubsystem::TryApplyVrState(bool bEnableVr, FVrRuntimeState& OutState)
{
	OutState = QueryVrRuntimeState();

	if (GEngine == nullptr)
	{
		return false;
	}

	IHeadMountedDisplay* HmdDevice = (GEngine->XRSystem.IsValid()) ? GEngine->XRSystem->GetHMDDevice() : nullptr;
	UWorld* World = GetWorld();

	if (HmdDevice != nullptr)
	{
		HmdDevice->EnableHMD(bEnableVr);
	}

	if (GEngine->StereoRenderingDevice.IsValid())
	{
		GEngine->StereoRenderingDevice->EnableStereo(bEnableVr);
	}

	if (World != nullptr)
	{
		GEngine->Exec(World, *FString::Printf(TEXT("vr.bEnableHMD %s"), bEnableVr ? TEXT("1") : TEXT("0")));
		GEngine->Exec(World, *FString::Printf(TEXT("vr.bEnableStereo %s"), bEnableVr ? TEXT("1") : TEXT("0")));
	}

	OutState = QueryVrRuntimeState();
	if (bEnableVr)
	{
		return IsVrFullyActive(OutState);
	}

	return !OutState.bHmdEnabled && !OutState.bStereoEnabled;
}

UElectricDreamsHotkeySubsystem::FVrRuntimeState UElectricDreamsHotkeySubsystem::QueryVrRuntimeState() const
{
	FVrRuntimeState State;
	if (GEngine == nullptr)
	{
		return State;
	}

	State.bHasXrSystem = GEngine->XRSystem.IsValid();
	if (State.bHasXrSystem)
	{
		State.XrSystemName = GEngine->XRSystem->GetSystemName().ToString();
		if (IHeadMountedDisplay* HmdDevice = GEngine->XRSystem->GetHMDDevice())
		{
			State.bHasHmdDevice = true;
			State.bHmdConnected = HmdDevice->IsHMDConnected();
			State.bHmdEnabled = HmdDevice->IsHMDEnabled();
		}
	}

	State.bHasStereoDevice = GEngine->StereoRenderingDevice.IsValid();
	if (State.bHasStereoDevice)
	{
		State.bStereoEnabled = GEngine->StereoRenderingDevice->IsStereoEnabled();
	}

	return State;
}

bool UElectricDreamsHotkeySubsystem::IsVrFullyActive(const FVrRuntimeState& State) const
{
	return State.bHasXrSystem &&
		State.bHasHmdDevice &&
		State.bHasStereoDevice &&
		State.bHmdConnected &&
		State.bHmdEnabled &&
		State.bStereoEnabled;
}
