#pragma once

#include "CoreMinimal.h"
#include "OpenScenarioDynamics.generated.h"

UENUM(BlueprintType)
enum class EOpenScenarioDynamicsMode : uint8
{
	/** Speeds are set exactly as the scenario commands them (no limits, no reactions). */
	Kinematic,
	/** Vehicles follow commanded speeds within acceleration/braking limits and react to speed limits, curves and traffic. */
	Simple
};

/**
 * Parameters of the simple vehicle/driver model.
 *
 * A SpeedAction sets the *desired* speed. Each vehicle then accelerates or brakes towards
 * min(desired speed, entity max speed, lookahead envelope) within its acceleration limits, where the
 * envelope is the fastest speed from which the vehicle can still brake (at ComfortDeceleration) to the
 * speed limit / curve speed / end of road ahead of it. If another actor is on the vehicle's path, an
 * IDM-style car-following term additionally limits the acceleration.
 */
USTRUCT(BlueprintType)
struct FOpenScenarioDynamicsSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics")
	EOpenScenarioDynamicsMode Mode = EOpenScenarioDynamicsMode::Simple;

	/** Cap for entity performance (m/s^2); scenario files often state unrealistically high values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.1", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	double MaxAcceleration = 3.5;

	/** Cap for the emergency braking deceleration (m/s^2, positive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.1", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	double MaxDeceleration = 8.0;

	/** Deceleration used to plan braking for limits, curves and traffic (m/s^2, positive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.1", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	double ComfortDeceleration = 3.0;

	/** Limits how fast the acceleration command may change (m/s^3) when following speed targets, limits and curves. 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "0.0", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	double MaxJerk = 25.0;

	/** Obey OpenDRIVE road-type and lane speed limits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Road", meta = (EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	bool bRespectSpeedLimits = true;

	/** Fraction of the limit that is driven (1 = exactly the limit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Road", meta = (ClampMin = "0.1", ClampMax = "2.0", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple && bRespectSpeedLimits"))
	double SpeedLimitFactor = 1.0;

	/** Slow down for curves so that the lateral acceleration stays below MaxLateralAcceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Road", meta = (EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	bool bSlowInCurves = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Road", meta = (ClampMin = "0.1", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple && bSlowInCurves"))
	double MaxLateralAcceleration = 3.0;

	/** Brake for and follow other actors on the vehicle's path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Traffic", meta = (EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	bool bBrakeForObstacles = true;

	/** Desired time gap to the vehicle in front (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Traffic", meta = (ClampMin = "0.0", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple && bBrakeForObstacles"))
	double TimeHeadway = 1.5;

	/** Bumper-to-bumper distance kept when standing (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics|Traffic", meta = (ClampMin = "0.0", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple && bBrakeForObstacles"))
	double MinGap = 2.0;

	/** Upper bound of the distance looked ahead (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamics", meta = (ClampMin = "10.0", EditCondition = "Mode == EOpenScenarioDynamicsMode::Simple"))
	double MaxLookAhead = 150.0;
};
