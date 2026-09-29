#pragma once

#include "CoreMinimal.h"

enum class EOpenDriveGeometryType : uint8
{
	Line,
	Arc,
	Spiral,
	Poly3,
	ParamPoly3
};

enum class EOpenDriveElementType : uint8
{
	None,
	Road,
	Junction
};

enum class EOpenDriveContactPoint : uint8
{
	None,
	Start,
	End
};

/** Cubic polynomial a + b*ds + c*ds^2 + d*ds^3 with ds = s - S (S is an absolute road s-coordinate). */
struct OPENSCENARIO_API FOpenDriveCubic
{
	double S = 0.0;
	double A = 0.0;
	double B = 0.0;
	double C = 0.0;
	double D = 0.0;

	double Eval(double AbsS) const
	{
		const double Ds = AbsS - S;
		return A + Ds * (B + Ds * (C + Ds * D));
	}

	/** Evaluate the piecewise function: last entry with S <= AbsS (first entry if before all). */
	static double EvalPiecewise(const TArray<FOpenDriveCubic>& Entries, double AbsS);
};

struct OPENSCENARIO_API FOpenDriveGeometry
{
	EOpenDriveGeometryType Type = EOpenDriveGeometryType::Line;
	double S = 0.0;
	double X = 0.0;
	double Y = 0.0;
	double Hdg = 0.0;
	double Length = 0.0;

	// Arc
	double Curvature = 0.0;
	// Spiral
	double CurvStart = 0.0;
	double CurvEnd = 0.0;
	// Poly3 uses (AV..DV) as a,b,c,d. ParamPoly3 uses both sets.
	double AU = 0.0, BU = 0.0, CU = 0.0, DU = 0.0;
	double AV = 0.0, BV = 0.0, CV = 0.0, DV = 0.0;
	bool bNormalizedRange = false;
};

struct OPENSCENARIO_API FOpenDriveLane
{
	int32 Id = 0;
	FString Type;
	/** 0 means "no link" (lane id 0 is the centre lane and can never be a link target). */
	int32 Predecessor = 0;
	int32 Successor = 0;
	/** Absolute-s width entries (sOffset already added to the lane section start). */
	TArray<FOpenDriveCubic> Widths;

	double GetWidth(double AbsS) const
	{
		return Widths.Num() > 0 ? FMath::Max(0.0, FOpenDriveCubic::EvalPiecewise(Widths, AbsS)) : 0.0;
	}
	bool IsDriving() const { return Type.Equals(TEXT("driving"), ESearchCase::IgnoreCase); }
};

struct OPENSCENARIO_API FOpenDriveLaneSection
{
	double S = 0.0;
	double EndS = 0.0;
	/** Left lanes (id > 0) and right lanes (id < 0), sorted by ascending id. */
	TArray<FOpenDriveLane> Lanes;

	const FOpenDriveLane* FindLane(int32 Id) const
	{
		for (const FOpenDriveLane& L : Lanes)
		{
			if (L.Id == Id)
			{
				return &L;
			}
		}
		return nullptr;
	}
};

struct OPENSCENARIO_API FOpenDriveRoad
{
	FString Id;
	FString Name;
	/** Empty if the road is not part of a junction. */
	FString JunctionId;
	double Length = 0.0;

	EOpenDriveElementType PredecessorType = EOpenDriveElementType::None;
	FString PredecessorId;
	EOpenDriveContactPoint PredecessorContact = EOpenDriveContactPoint::None;
	EOpenDriveElementType SuccessorType = EOpenDriveElementType::None;
	FString SuccessorId;
	EOpenDriveContactPoint SuccessorContact = EOpenDriveContactPoint::None;

	TArray<FOpenDriveGeometry> Geometry;
	TArray<FOpenDriveCubic> Elevation;
	TArray<FOpenDriveCubic> LaneOffset;
	TArray<FOpenDriveLaneSection> LaneSections;

	// Derived at load time, used to prune spatial queries.
	double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
	double MaxExtent = 0.0;

	bool IsJunctionRoad() const { return !JunctionId.IsEmpty(); }
};

struct OPENSCENARIO_API FOpenDriveJunctionConnection
{
	FString Id;
	FString IncomingRoad;
	FString ConnectingRoad;
	EOpenDriveContactPoint ContactPoint = EOpenDriveContactPoint::Start;
	/** (from lane on incoming road, to lane on connecting road) */
	TArray<TPair<int32, int32>> LaneLinks;
};

struct OPENSCENARIO_API FOpenDriveJunction
{
	FString Id;
	FString Name;
	TArray<FOpenDriveJunctionConnection> Connections;
};

struct FOpenDrivePose
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	double Heading = 0.0;
};

struct FOpenDriveRoadPosition
{
	FString RoadId;
	double S = 0.0;
	double T = 0.0;
	int32 LaneId = 0;
	/** Distance from the point to the drivable surface of the road (0 if inside). */
	double DistanceToRoad = 0.0;
};

/** A way to continue from the end (bForward) or the start (!bForward) of a road. */
struct FOpenDriveSuccessor
{
	FString RoadId;
	/** True if the successor is entered at its start and traversed in +s direction. */
	bool bForward = true;
	/** Lane id on the current road -> lane id on the successor road. */
	TMap<int32, int32> LaneMap;
};

struct FOpenDriveRouteStep
{
	FString RoadId;
	bool bForward = true;
};

/**
 * Parsed ASAM OpenDRIVE road network (plain C++ data, no UObject dependency).
 * Supports reference-line geometry (line, arc, spiral, poly3, paramPoly3), elevation, lane offsets,
 * lane sections/widths, road links, junction connections, nearest-road lookup and road-level routing.
 */
class OPENSCENARIO_API FOpenDriveMap
{
public:
	bool LoadFromString(const FString& Xml, FString& OutError);

	const FString& GetName() const { return Name; }
	const TArray<FOpenDriveRoad>& GetRoads() const { return Roads; }
	const TArray<FOpenDriveJunction>& GetJunctions() const { return Junctions; }
	double GetTotalLength() const;

	const FOpenDriveRoad* FindRoad(const FString& RoadId) const;
	const FOpenDriveJunction* FindJunction(const FString& JunctionId) const;

	// --- Geometry -----------------------------------------------------------------------------
	/** Reference line pose (t = 0), without elevation. */
	void EvaluateReferenceLine(const FOpenDriveRoad& Road, double S, double& X, double& Y, double& Heading) const;
	/** Pose at (s, t). Heading is the reference line heading (direction of increasing s). */
	FOpenDrivePose EvaluatePose(const FOpenDriveRoad& Road, double S, double T) const;

	// --- Lanes --------------------------------------------------------------------------------
	const FOpenDriveLaneSection* FindLaneSection(const FOpenDriveRoad& Road, double S) const;
	double GetLaneOffset(const FOpenDriveRoad& Road, double S) const;
	/** t-coordinate of the centre of the given lane. Returns the lane offset for LaneId 0. */
	double GetLaneCenterT(const FOpenDriveRoad& Road, double S, int32 LaneId) const;
	double GetLaneWidth(const FOpenDriveRoad& Road, double S, int32 LaneId) const;
	/** t of the outer border of the outermost left (bLeft) or right lane. */
	double GetOuterBorderT(const FOpenDriveRoad& Road, double S, bool bLeft) const;
	/** Lane containing t (outermost lane if t is outside the road, 0 if the road has no lanes). */
	int32 FindLaneAt(const FOpenDriveRoad& Road, double S, double T) const;
	/** Returns LaneId if it exists at S, otherwise the closest existing lane of the same side. */
	int32 ClampLaneId(const FOpenDriveRoad& Road, double S, int32 LaneId) const;

	// --- Queries ------------------------------------------------------------------------------
	/** Closest road position to a world point. Only roads within MaxDistance of their surface qualify. */
	bool FindClosestRoadPosition(double X, double Y, double MaxDistance, FOpenDriveRoadPosition& Out) const;

	// --- Routing ------------------------------------------------------------------------------
	void GetSuccessors(const FOpenDriveRoad& Road, bool bForward, TArray<FOpenDriveSuccessor>& Out) const;
	/**
	 * Shortest (by road length) sequence of roads from StartRoad, initially travelling in the given
	 * direction, to any traversal of TargetRoad. The first step is always the start road.
	 */
	bool FindRoute(const FString& StartRoadId, bool bStartForward, const FString& TargetRoadId, TArray<FOpenDriveRouteStep>& OutRoute) const;

private:
	void ComputeRoadBounds(FOpenDriveRoad& Road) const;
	int32 FindGeometryIndex(const FOpenDriveRoad& Road, double S) const;

	FString Name;
	TArray<FOpenDriveRoad> Roads;
	TMap<FString, int32> RoadIndex;
	TArray<FOpenDriveJunction> Junctions;
	TMap<FString, int32> JunctionIndex;
};
