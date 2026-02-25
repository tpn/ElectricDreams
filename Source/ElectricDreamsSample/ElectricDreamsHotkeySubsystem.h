// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ElectricDreamsHotkeySubsystem.generated.h"

UCLASS()
class ELECTRICDREAMSSAMPLE_API UElectricDreamsHotkeySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return true; }

private:
	struct FVrRuntimeState
	{
		bool bHasXrSystem = false;
		bool bHasHmdDevice = false;
		bool bHasStereoDevice = false;
		bool bHmdConnected = false;
		bool bHmdEnabled = false;
		bool bStereoEnabled = false;
		FString XrSystemName = TEXT("None");
	};

	void ApplyVerticalReposition(APlayerController* PlayerController, float DeltaTime);
	void ToggleHelpOverlay();
	void DrawHelpOverlay() const;
	void AdjustMovementRateMultiplier(float LogDelta);
	float GetMovementRateMultiplier() const;
	void ToggleYAxisInversion(APlayerController* PlayerController);
	void CycleLevel(bool bForward);
	void CycleLightingPreset(bool bForward);
	void ApplyLightingPreset(bool bShowMessage);
	void ToggleVrMode();
	void TickVrEnableRetry();
	void StartVrEnableRetry();
	void StopVrEnableRetry();
	void SyncVrRuntimeCvars();
	bool TryApplyVrState(bool bEnableVr, FVrRuntimeState& OutState);
	FVrRuntimeState QueryVrRuntimeState() const;
	bool IsVrFullyActive(const FVrRuntimeState& State) const;

	int32 LightingPresetIndex = 1;
	bool bLightingPresetApplied = false;
	bool bShowHelpOverlay = false;
	bool bAutoVrStartupAttempted = false;
	bool bVrEnableRetryActive = false;
	bool bHasSyncedVrState = false;
	bool bLastSyncedVrActive = false;
	int32 CachedNonVrDlssgValue = 1;
	int32 CachedNonVrDlssSrValue = 1;
	int32 CachedNonVrDeepDvcValue = 0;
	int32 CachedNonVrHiddenAreaMaskValue = 0;
	int32 CachedNonVrOpenXRDepthLayerValue = 1;
	double NextVrEnableAttemptTimeSeconds = 0.0;
	int32 RemainingVrEnableAttempts = 0;
};
