#pragma once

#include "CoreMinimal.h"

/**
 * Conversion between the OpenSCENARIO / OpenDRIVE world frame and Unreal Engine.
 *
 * ASAM frame: right-handed, Z up, X forward, Y left, metres, heading counter-clockwise in radians.
 * Unreal frame: left-handed, Z up, X forward, Y right, centimetres, yaw clockwise in degrees.
 */
namespace OpenScenarioCoords
{
	inline FVector ToUnrealLocation(double X, double Y, double Z)
	{
		return FVector(X * 100.0, -Y * 100.0, Z * 100.0);
	}

	inline double HeadingToYawDegrees(double HeadingRad)
	{
		return -FMath::RadiansToDegrees(HeadingRad);
	}

	inline double YawDegreesToHeading(double YawDeg)
	{
		return -FMath::DegreesToRadians(YawDeg);
	}

	inline double NormalizeAngle(double A)
	{
		while (A > UE_DOUBLE_PI) { A -= 2.0 * UE_DOUBLE_PI; }
		while (A < -UE_DOUBLE_PI) { A += 2.0 * UE_DOUBLE_PI; }
		return A;
	}
}
