#pragma once

#include "CoreMinimal.h"
#include "OpenScenarioSignals.generated.h"

UENUM(BlueprintType)
enum class EOpenScenarioSignalState : uint8
{
	/** No active signal (dark, or not a signal that has states). */
	Off,
	Red,
	Yellow,
	Green
};

/**
 * Behaviour of OpenDRIVE signals during a simulation (used by vehicles under simple dynamics).
 *
 * Traffic lights (dynamic signals of the listed types) cycle automatically unless a scenario controls them:
 * lights on the same OpenDRIVE controller switch together, crossing approaches of one junction alternate.
 * Static signs are read from their OpenDRIVE type code: 206 stop, 205 give way, 274 speed limit (value in
 * the signal's unit, km/h by default), 278/280/282 end of speed limit; US codes R1-1, R1-2 and R2-1 as well.
 */
USTRUCT(BlueprintType)
struct FOpenScenarioSignalSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals")
	bool bObeyTrafficLights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals")
	bool bObeyStopSigns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals")
	bool bObeyYieldSigns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals")
	bool bObeySpeedSigns = true;

	/** Let traffic lights cycle on their own (otherwise they stay as the scenario sets them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Traffic Lights")
	bool bAutoCycleTrafficLights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Traffic Lights", meta = (ClampMin = "1.0"))
	double GreenTime = 20.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Traffic Lights", meta = (ClampMin = "0.5"))
	double YellowTime = 3.0;

	/** Time with all lights of a junction red between two phases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Traffic Lights", meta = (ClampMin = "0.0"))
	double AllRedTime = 2.0;

	/**
	 * OpenDRIVE type codes of vehicle traffic lights. Dynamic signals of other types that are not recognised
	 * as signs are treated as traffic lights too; pedestrian lights (1000002) are ignored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Traffic Lights")
	TArray<FString> TrafficLightTypes = { TEXT("1000001") };

	/** Time a vehicle stands still at a stop sign before it may go (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Signs", meta = (ClampMin = "0.0"))
	double StopDwellTime = 2.0;

	/** Distance the front bumper keeps from the stop line (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Signs", meta = (ClampMin = "0.0"))
	double StopLineMargin = 0.5;

	/** Speed limit when approaching a give-way sign with the junction occupied (m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Signals|Signs", meta = (ClampMin = "0.0"))
	double YieldApproachSpeed = 5.0;
};
