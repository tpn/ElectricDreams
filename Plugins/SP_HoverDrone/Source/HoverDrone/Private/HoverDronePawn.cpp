// Copyright Epic Games, Inc. All Rights Reserved.

#include "HoverDronePawn.h"
#include "GameFramework/PlayerInput.h"
#include "Curves/CurveVector.h"
#include "CollisionQueryParams.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Components/SphereComponent.h"

#include "HoverDroneMovementComponent.h"

#include "HAL/IConsoleManager.h"

#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "EnhancedInputComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(HoverDronePawn)

float DroneSpeedScalar = 1.f;
FAutoConsoleVariableRef CVarDroneSpeedScalar(
	TEXT("HoverDrone.DroneSpeedScalar"),
	DroneSpeedScalar,
	TEXT("Simple scalar on linear acceleration for the drone.\n"),
	ECVF_Default);

bool GHoverDroneInvertLookY = true;
FAutoConsoleVariableRef CVarHoverDroneInvertLookY(
	TEXT("HoverDrone.InvertLookY"),
	GHoverDroneInvertLookY,
	TEXT("When true, hover drone pitch look input is inverted for mouse/controller.\n"),
	ECVF_Default);

AHoverDronePawn::AHoverDronePawn(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer
		.SetDefaultSubobjectClass<UHoverDroneMovementComponent>(ADefaultPawn::MovementComponentName)
		.SetDefaultSubobjectClass<UCameraComponent>(AHoverDronePawnBase::CameraComponentName)
	)
{
	// Avoid hard constructor errors when optional plugin input assets are missing.
	InputMappingContext = LoadObject<UInputMappingContext>(nullptr, TEXT("/SP_HoverDrone/Input/IM_HoverDrone.IM_HoverDrone"));
	MoveAction = LoadObject<UInputAction>(nullptr, TEXT("/SP_HoverDrone/Input/IA_HoverDrone_Move.IA_HoverDrone_Move"));
	LookAction = LoadObject<UInputAction>(nullptr, TEXT("/SP_HoverDrone/Input/IA_HoverDrone_Look.IA_HoverDrone_Look"));
	ChangeAltitudeAction = LoadObject<UInputAction>(nullptr, TEXT("/SP_HoverDrone/Input/IA_HoverDrone_ChangeAltitude.IA_HoverDrone_ChangeAltitude"));
	ChangeSpeedAction = LoadObject<UInputAction>(nullptr, TEXT("/SP_HoverDrone/Input/IA_HoverDrone_ChangeSpeed.IA_HoverDrone_ChangeSpeed"));

	if (InputMappingContext == nullptr || MoveAction == nullptr || LookAction == nullptr || ChangeAltitudeAction == nullptr || ChangeSpeedAction == nullptr)
	{
		UE_LOG(LogHoverDrone, Warning, TEXT("SP_HoverDrone input assets are missing. Drone Enhanced Input bindings will be partially or fully disabled."));
	}

	SetCanBeDamaged(false);
	bAddDefaultMovementBindings = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	bAllowSpeedChange = true;

	TiltUpVector = FVector(0, 0, 10000.f);
}

void AHoverDronePawn::SetupPlayerInputComponent(UInputComponent* InInputComponent)
{
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InInputComponent))
	{
		if (MoveAction != nullptr)
		{
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHoverDronePawn::MoveActionBinding);
		}
		if (LookAction != nullptr)
		{
			EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHoverDronePawn::LookActionBinding);
		}
		if (ChangeAltitudeAction != nullptr)
		{
			EnhancedInputComponent->BindAction(ChangeAltitudeAction, ETriggerEvent::Triggered, this, &AHoverDronePawn::ChangeAltitudeActionBinding);
		}
		if (ChangeSpeedAction != nullptr)
		{
			EnhancedInputComponent->BindAction(ChangeSpeedAction, ETriggerEvent::Triggered, this, &AHoverDronePawn::ChangeSpeedActionBinding);
		}
	}
	else
	{
		UE_LOG(LogHoverDrone, Warning, TEXT("Failed to setup player input for %s, InputComponent type is not UEnhancedInputComponent."), *GetName());
	}
}

void AHoverDronePawn::PawnClientRestart()
{
	Super::PawnClientRestart();

	if (InputMappingContext)
	{
		if (APlayerController* PC = GetController<APlayerController>())
		{
			if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					InputSubsystem->AddMappingContext(InputMappingContext, InputMappingPriority);
				}
			}
		}
	}
}

void AHoverDronePawn::MoveForward(float Val)
{
	if (Val != 0.f)
	{
		if (Controller)
		{
			FRotator const ControlSpaceRot = CameraComponent->GetComponentToWorld().GetRotation().Rotator();

			FVector WorldDir = FRotationMatrix(ControlSpaceRot).GetScaledAxis(EAxis::X);

			// constrain right/forward movement to XY plane
			if (bConstrainMovementToXYPlane)
			{
				WorldDir.Z = 0.f;
			}

			if (WorldDir.IsZero() == false)
			{
				// normalize so sliding speed isn't pitch-dependent
				WorldDir.Normalize();

				// transform to world space and add it
				AddMovementInput(WorldDir, Val);
			}
		}
	}
}

void AHoverDronePawn::MoveRight(float Val)
{
	if (Val != 0.f)
	{
		if (Controller)
		{
			FRotator const ControlSpaceRot = CameraComponent->GetComponentToWorld().GetRotation().Rotator();

			// transform to world space and add it
			FVector WorldDir = FRotationMatrix(ControlSpaceRot).GetScaledAxis(EAxis::Y); 

			// constrain right/forward movement to XY plane
			if (bConstrainMovementToXYPlane)
			{
				WorldDir.Z = 0.f;
			}

			if (WorldDir.IsZero() == false)
			{
				// normalize so sliding speed isn't pitch-dependent
				WorldDir.Normalize();

				// transform to world space and add it
				AddMovementInput(WorldDir, Val);
			}
		}
	}
}

void AHoverDronePawn::MoveUp(float Val)
{
	if (Val != 0.f)
	{
		AddMovementInput(FVector::UpVector, Val);
	}
}

void AHoverDronePawn::MoveActionBinding(const FInputActionValue& ActionValue)
{
	const FInputActionValue::Axis2D AxisValue = ActionValue.Get<FInputActionValue::Axis2D>();
	MoveForward(AxisValue.X);
	MoveRight(AxisValue.Y);
}

void AHoverDronePawn::LookActionBinding(const FInputActionValue& ActionValue)
{
	const FInputActionValue::Axis2D AxisValue = ActionValue.Get<FInputActionValue::Axis2D>();
	TurnAccel(AxisValue.X);
	LookUpAccel(AxisValue.Y);
}

void AHoverDronePawn::ChangeAltitudeActionBinding(const FInputActionValue& ActionValue)
{
	MoveUp(ActionValue.Get<FInputActionValue::Axis1D>());
}

void AHoverDronePawn::ChangeSpeedActionBinding(const FInputActionValue& ActionValue)
{
	if (ActionValue.Get<FInputActionValue::Axis1D>() > 0)
	{
		IncreaseHoverDroneSpeed();
	}
	else
	{
		DecreaseHoverDroneSpeed();
	}
}

void AHoverDronePawn::TurnAccel(float Val)
{
	APlayerController* const PC = Cast<APlayerController>(GetController());
	bool const bLookInputIgnored = PC && PC->IsLookInputIgnored();

	if (bLookInputIgnored == false)
	{
		UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
		if (HoverMoveComponent != nullptr)
		{
			FRotator const YawInput = FRotator(0, Val, 0);// FRotator(0, (PC && PC->ShouldInvertYaw()) ? -Val : Val, 0);
			HoverMoveComponent->AddRotationInput(YawInput);
		}
	}
}

void AHoverDronePawn::LookUpAccel(float Val)
{
	APlayerController* const PC = Cast<APlayerController>(GetController());
	bool const bLookInputIgnored = PC && PC->IsLookInputIgnored();

	if (bLookInputIgnored == false)
	{
		UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
		if (HoverMoveComponent != nullptr)
		{
			const float PitchInputValue = GHoverDroneInvertLookY ? Val : -Val;
			FRotator const PitchInput = FRotator(PitchInputValue, 0, 0);
			HoverMoveComponent->AddRotationInput(PitchInput);
		}
	}
}

FRotator AHoverDronePawn::GetViewRotation() const
{
	// pawn rotation dictates camera rotation
	float const Pitch = GetActorRotation().Pitch;
	float const Yaw = GetActorRotation().Yaw;
	return FRotator(Pitch, Yaw, 0.f);
}

void AHoverDronePawn::BeginLookat()
{
	// do a trace to see what we're looking at and save it
	APlayerController* const PC = Cast<APlayerController>(Controller);
	if (PC && PC->PlayerCameraManager)
	{
		FVector CamLoc;
		FRotator CamRot;
		GetActorEyesViewPoint(CamLoc, CamRot);

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(Reverb_HoverDrone_BeginLookat), true, this);
		FHitResult Hit;

		FVector TraceStart = CamLoc;
		FVector TraceEnd = TraceStart + CamRot.Vector() * 100000.f;
		bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams);
		if (bHit)
		{
			UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
			if (HoverMoveComponent)
			{
				HoverMoveComponent->ForceFacing(Hit.ImpactPoint);
			}
		}
	}
}

void AHoverDronePawn::EndLookat()
{
	UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		HoverMoveComponent->StopForceFacing();
	}
}

void AHoverDronePawn::ToggleFixedHeight()
{
	UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		HoverMoveComponent->SetMaintainHoverHeight( !HoverMoveComponent->GetMaintainHoverHeight() );
	}
}

float AHoverDronePawn::GetAltitude() const
{
	UHoverDroneMovementComponent const* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		return HoverMoveComponent->GetAltitude();
	}

	return 0.f;
}

bool AHoverDronePawn::IsMaintainingConstantAltitude() const
{
	UHoverDroneMovementComponent const* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		return HoverMoveComponent->GetMaintainHoverHeight();
	}

	return false;
}


void AHoverDronePawn::IncreaseHoverDroneSpeed()
{
	if (bAllowSpeedChange)
	{
		SetDroneSpeedIndex(GetDroneSpeedIndex() + 1);
	}
}

void AHoverDronePawn::DecreaseHoverDroneSpeed()
{
	if (bAllowSpeedChange)
	{
		SetDroneSpeedIndex(GetDroneSpeedIndex() - 1);
	}
}

int32 AHoverDronePawn::GetDroneSpeedIndex() const
{
	UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		return HoverMoveComponent->GetDroneSpeedIndex();
	}

	return INDEX_NONE;
}

void AHoverDronePawn::SetDroneSpeedIndex(int32 SpeedIndex)
{
	UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		HoverMoveComponent->SetDroneSpeedIndex(SpeedIndex);
	}
}

void AHoverDronePawn::SetToDefaultDroneSpeedIndex()
{
	AHoverDronePawn const* const ThisCDO = CastChecked<AHoverDronePawn>(GetClass()->GetDefaultObject());
	UHoverDroneMovementComponent const* const CDOMoveComp = ThisCDO ? Cast<UHoverDroneMovementComponent>(ThisCDO->GetMovementComponent()) : nullptr;

	if (CDOMoveComp)
	{
		UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
		if (HoverMoveComponent)
		{
			HoverMoveComponent->SetDroneSpeedIndex(CDOMoveComp->GetDroneSpeedIndex());
		}
	}
}

void AHoverDronePawn::ResetInterpolation()
{
	UHoverDroneMovementComponent* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent());
	if (HoverMoveComponent)
	{
		HoverMoveComponent->ResetInterpolation();
	}
}


void AHoverDronePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
}




FRotator AHoverDronePawn::GetTiltedDroneRotation(float DeltaTime)
{
	if (bIsTiltingEnabled == false)
	{
		return GetActorRotation();
	}
	

	FVector MeasuredVelocity = FVector::ZeroVector;
	if (UHoverDroneMovementComponent const* const HoverMoveComponent = Cast<UHoverDroneMovementComponent>(GetMovementComponent()))
	{
		MeasuredVelocity = HoverMoveComponent->MeasuredVelocity;
	}

	// now apply a tilt to simulate motion
	{
		FMatrix const OldCamToWorld = FRotationMatrix(GetActorRotation());
		FMatrix const UntiltedDroneToWorld = FRotationMatrix::MakeFromZX(FVector::UpVector, OldCamToWorld.GetUnitAxis(EAxis::X));

		FMatrix const OldCamToDrone = OldCamToWorld * UntiltedDroneToWorld.Inverse();

		// more velocity => more tilt
		// greater up vector magnitude => less tilt per unit velocity
		FVector const TiltedUpVector = (MeasuredVelocity + TiltUpVector);
		FMatrix TiltedDroneToWorld = FRotationMatrix::MakeFromZX(TiltedUpVector, UntiltedDroneToWorld.GetUnitAxis(EAxis::X));

		// interpolate drone tilt to smooth it out
		// only interpolating pitch and roll though!
		FRotator GoalTiltedDroneRot = TiltedDroneToWorld.Rotator();
		if (bEnableTiltLimits)
		{
			GoalTiltedDroneRot.Pitch = FMath::Clamp<>(GoalTiltedDroneRot.Pitch, -TiltLimits.Pitch, TiltLimits.Pitch);
			GoalTiltedDroneRot.Roll = FMath::Clamp<>(GoalTiltedDroneRot.Roll, -TiltLimits.Roll, TiltLimits.Roll);
		}

		GoalTiltedDroneRot.Yaw = 0.f;
		FRotator InterpedTiltedDroneRot = DroneTiltInterpolator.Eval(GoalTiltedDroneRot, DeltaTime);

		LastTiltedDroneRot = InterpedTiltedDroneRot;
		InterpedTiltedDroneRot.Yaw = GetActorRotation().Yaw; // keep original Yaw
		//return InterpedTiltedDroneRot;

		FMatrix const NewCamToWorld = OldCamToDrone * TiltedDroneToWorld;

		return NewCamToWorld.Rotator();
	}
}

