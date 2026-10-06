// Test-only vendored copy of the OpenDRIVE data model from the separate OpenDRIVE_UE plugin
// (https://github.com/p-schulz/OpenDRIVE_UE, Plugins/OpenDrive/Source/OpenDrive). The standalone harness cannot
// pull in a sibling repository, so the runtime sources are copied here; re-sync them when the shared model changes.
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

/** Which side of the centre line a <crossfall> entry applies to. */
enum class EOpenDriveCrossfallSide : uint8
{
	Left,
	Right,
	Both
};

/** <road><type type="..."/> — road category, independent of the speed limit it may carry. */
enum class EOpenDriveRoadType : uint8
{
	Unknown,
	Rural,
	Motorway,
	Town,
	LowSpeed,
	Pedestrian,
	Bicycle,
	TownExpressway,
	TownCollector,
	TownArterial,
	TownPrivate,
	TownLocal,
	TownPlayStreet
};

OPENDRIVE_API EOpenDriveRoadType ParseOpenDriveRoadType(const FString& S);
OPENDRIVE_API FString OpenDriveRoadTypeToString(EOpenDriveRoadType Type);

/** Cubic polynomial a + b*ds + c*ds^2 + d*ds^3 with ds = s - S (S is an absolute road s-coordinate). */
struct OPENDRIVE_API FOpenDriveCubic
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

	/** Derivative dValue/dAbsS at AbsS. */
	double EvalDerivative(double AbsS) const
	{
		const double Ds = AbsS - S;
		return B + Ds * (2.0 * C + Ds * 3.0 * D);
	}

	/** Evaluate the piecewise function: last entry with S <= AbsS (first entry if before all). */
	static double EvalPiecewise(const TArray<FOpenDriveCubic>& Entries, double AbsS);
};

struct OPENDRIVE_API FOpenDriveGeometry
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

/** Speed limit valid from S (absolute road s) on, in m/s. */
struct FOpenDriveSpeedLimit
{
	double S = 0.0;
	double MaxSpeed = 0.0;
};

/** <road><type> entry: road category (and optional country code) valid from S on. Speed limits are
 *  tracked separately in FOpenDriveRoad::SpeedLimits, parsed from the same <type><speed> child. */
struct OPENDRIVE_API FOpenDriveRoadTypeEntry
{
	double S = 0.0;
	EOpenDriveRoadType Type = EOpenDriveRoadType::Town;
	FString Country;
};

/** <lateralProfile><crossfall>: banking angle in radians, like superelevation, but one-sided or applied
 *  to both sides independently. Used (instead of superelevation) mainly for drainage on straight roads. */
struct OPENDRIVE_API FOpenDriveCrossfallEntry
{
	EOpenDriveCrossfallSide Side = EOpenDriveCrossfallSide::Both;
	FOpenDriveCubic Cubic;
};

/**
 * <lateralProfile><shape>: additional cross-section height as a cubic in local dt = t - T, active from
 * (S, T) on. Entries sharing the same S are the record's rows across t; the group with the greatest
 * S <= query-S is active, and within it the row with the greatest T <= query-T applies. This is what
 * most tools call "road carving" -- crowns, gutters, anything the road surface does across its width
 * that elevation/superelevation/crossfall (which are t-independent, aside from side) can't express.
 */
struct OPENDRIVE_API FOpenDriveShapeEntry
{
	double S = 0.0;
	double T = 0.0;
	double A = 0.0, B = 0.0, C = 0.0, D = 0.0;

	double Eval(double AbsT) const
	{
		const double Dt = AbsT - T;
		return A + Dt * (B + Dt * (C + Dt * D));
	}
};

/** <lane><roadMark> line type. Botts dots/grass/curb/edge are rendered as a plain coloured line in the
 *  debug visualizer (Phase 7's mesh generation is what actually distinguishes their geometry). */
enum class EOpenDriveRoadMarkType : uint8
{
	None,
	Solid,
	Broken,
	SolidSolid,
	SolidBroken,
	BrokenSolid,
	BrokenBroken,
	BottsDots,
	Grass,
	Curb,
	Edge,
	Custom
};

enum class EOpenDriveRoadMarkWeight : uint8 { Standard, Bold };
enum class EOpenDriveRoadMarkColor : uint8 { Standard, Yellow, Red, Blue, Green, Orange, Violet };
enum class EOpenDriveLaneChange : uint8 { Increase, Decrease, Both, None };

OPENDRIVE_API EOpenDriveRoadMarkType ParseOpenDriveRoadMarkType(const FString& S);
OPENDRIVE_API FString OpenDriveRoadMarkTypeToString(EOpenDriveRoadMarkType Type);
OPENDRIVE_API EOpenDriveRoadMarkColor ParseOpenDriveRoadMarkColor(const FString& S);
OPENDRIVE_API FString OpenDriveRoadMarkColorToString(EOpenDriveRoadMarkColor Color);
OPENDRIVE_API EOpenDriveLaneChange ParseOpenDriveLaneChange(const FString& S);
OPENDRIVE_API FString OpenDriveLaneChangeToString(EOpenDriveLaneChange Value);

/** <lane><roadMark>: paints the line at this lane's outer border (the one further from the centre line),
 *  or the centre line itself for lane 0. Valid from S (absolute road s) on. */
struct OPENDRIVE_API FOpenDriveRoadMarkEntry
{
	double S = 0.0;
	EOpenDriveRoadMarkType Type = EOpenDriveRoadMarkType::Solid;
	EOpenDriveRoadMarkWeight Weight = EOpenDriveRoadMarkWeight::Standard;
	EOpenDriveRoadMarkColor Color = EOpenDriveRoadMarkColor::Standard;
	/** Metres; negative means "unspecified" (reader should assume a sensible default, e.g. 0.12 m). */
	double Width = -1.0;
	EOpenDriveLaneChange LaneChange = EOpenDriveLaneChange::None;
	double Height = 0.0;
};

/** <lane><material>: surface friction/roughness/texture, valid from S (absolute road s) on. */
struct OPENDRIVE_API FOpenDriveLaneMaterialEntry
{
	double S = 0.0;
	double Friction = 1.0;
	double Roughness = 0.0;
	FString Surface;
};

/** <lane><access>: allows or denies a vehicle category, valid from S (absolute road s) on. Restriction is
 *  the ASAM vehicle category string (e.g. "bicycle", "pedestrian", "bus"); empty means all categories. */
struct OPENDRIVE_API FOpenDriveLaneAccessEntry
{
	double S = 0.0;
	bool bAllow = true;
	FString Restriction;
};

/** <lane><rule>: a free-text traffic rule annotation (e.g. "no overtaking"), valid from S on. */
struct OPENDRIVE_API FOpenDriveLaneRuleEntry
{
	double S = 0.0;
	FString Value;
};

/** <lane><height>: raises the lane's rendered surface above the road's elevation profile (e.g. for a
 *  curb or sidewalk), valid from S (absolute road s) on. */
struct OPENDRIVE_API FOpenDriveLaneHeightEntry
{
	double S = 0.0;
	double InnerHeight = 0.0;
	double OuterHeight = 0.0;
};

struct OPENDRIVE_API FOpenDriveLane
{
	int32 Id = 0;
	FString Type;
	/** 0 means "no link" (lane id 0 is the centre lane and can never be a link target). */
	int32 Predecessor = 0;
	int32 Successor = 0;
	/** Absolute-s width entries (sOffset already added to the lane section start). */
	TArray<FOpenDriveCubic> Widths;
	/** Lane speed limits (absolute s). */
	TArray<FOpenDriveSpeedLimit> SpeedLimits;
	/** Absolute-s road mark entries (sOffset already added to the lane section start). */
	TArray<FOpenDriveRoadMarkEntry> RoadMarks;
	TArray<FOpenDriveLaneMaterialEntry> Materials;
	TArray<FOpenDriveLaneAccessEntry> Access;
	TArray<FOpenDriveLaneRuleEntry> Rules;
	TArray<FOpenDriveLaneHeightEntry> Heights;

	const FOpenDriveRoadMarkEntry* FindRoadMarkAt(double AbsS) const
	{
		const FOpenDriveRoadMarkEntry* Best = nullptr;
		for (const FOpenDriveRoadMarkEntry& Entry : RoadMarks)
		{
			if (Entry.S <= AbsS + 1e-9)
			{
				Best = &Entry;
			}
			else
			{
				break;
			}
		}
		return Best;
	}

	double GetWidth(double AbsS) const
	{
		return Widths.Num() > 0 ? FMath::Max(0.0, FOpenDriveCubic::EvalPiecewise(Widths, AbsS)) : 0.0;
	}
	bool IsDriving() const { return Type.Equals(TEXT("driving"), ESearchCase::IgnoreCase); }
};

struct OPENDRIVE_API FOpenDriveLaneSection
{
	double S = 0.0;
	double EndS = 0.0;
	/** Left lanes (id > 0), the centre lane (id 0) and right lanes (id < 0), sorted by ascending id. */
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

	FOpenDriveLane* FindLaneMutable(int32 Id)
	{
		for (FOpenDriveLane& L : Lanes)
		{
			if (L.Id == Id)
			{
				return &L;
			}
		}
		return nullptr;
	}
};

/** <signal orientation="+|-|none">: "+"/"-" mean the signal faces traffic travelling in the direction of
 *  increasing/decreasing s; "none" means it applies to both directions (e.g. a speed limit gantry). */
enum class EOpenDriveSignalOrientation : uint8 { Plus, Minus, None };

OPENDRIVE_API EOpenDriveSignalOrientation ParseOpenDriveSignalOrientation(const FString& S);
OPENDRIVE_API FString OpenDriveSignalOrientationToString(EOpenDriveSignalOrientation Value);

/**
 * <road><signals><signal>: a traffic sign or light. Type/Subtype/Country follow the ASAM/Vienna Convention
 * codes the spec's own examples use (e.g. Country "DE", Type "206" = stop sign) -- the editor's presets
 * (see FOpenDriveModelEdit::AddSignal) fill these in so authors don't need to know the codes themselves.
 * Dynamic (traffic-light) state control belongs in a <controller> (FOpenDriveController), not here.
 */
struct OPENDRIVE_API FOpenDriveSignal
{
	FString Id;
	FString Name;
	double S = 0.0;
	double T = 0.0;
	double ZOffset = 0.0;
	bool bDynamic = false;
	EOpenDriveSignalOrientation Orientation = EOpenDriveSignalOrientation::None;
	FString Country;
	FString Type;
	FString Subtype;
	double Value = 0.0;
	FString Unit;
	double Height = 0.0;
	double Width = 0.0;
	FString Text;
	/** Additional heading (radians) on top of the road heading + orientation flip. */
	double HOffset = 0.0;
	double Pitch = 0.0;
	double Roll = 0.0;
};

/** <controller><control signalId="..."/>: one signal this controller drives (e.g. one phase of a light). */
struct OPENDRIVE_API FOpenDriveControllerEntry
{
	FString SignalId;
	FString Type;
};

/** Top-level <controller>: groups signals under shared control logic (e.g. a traffic light's phases).
 *  Actual phase timing/state is a simulation concern (OpenSCENARIO_UE's TrafficSignalController), not
 *  modelled here -- this only records which signals belong together. */
struct OPENDRIVE_API FOpenDriveController
{
	FString Id;
	FString Name;
	TArray<FOpenDriveControllerEntry> Controls;
};

/**
 * <road><objects><object>: a static object along the road (pole, tree, barrier, ...). This is a
 * deliberately reduced sketch of the spec's <object> element -- a single pose/footprint per object, no
 * <repeat> (guardrail/fence-style repetition along s), no <outline> (custom polygon footprint) and no
 * parking-space subtype. Those are left for a future pass if the project needs them; a single explicit
 * object per instance already covers most static-prop placement (signs are FOpenDriveSignal instead).
 */
struct OPENDRIVE_API FOpenDriveObject
{
	FString Id;
	FString Name;
	/** Free-form category from the spec's suggested list (e.g. "pole", "tree", "barrier") or a project's own. */
	FString Type;
	double S = 0.0;
	double T = 0.0;
	double ZOffset = 0.0;
	/** Heading relative to the road's reference-line heading at S, radians. */
	double HOffset = 0.0;
	double Pitch = 0.0;
	double Roll = 0.0;
	/** true = always faces +s regardless of which side of the road it's on ("orientation" attr absent/"none"
	 *  in the spec is direction-independent; here we only distinguish "has an explicit direction" or not). */
	EOpenDriveSignalOrientation Orientation = EOpenDriveSignalOrientation::None;
	/** Bounding-box footprint (metres). Radius is used instead of Length/Width for round objects when > 0. */
	double Length = 0.0;
	double Width = 0.0;
	double Height = 0.0;
	double Radius = 0.0;
};

/** Top-level <junctionGroup>: groups junctions that form a single logical intersection (typically a
 *  roundabout split into several <junction> elements). Sketch-scoped: Type is stored verbatim (usually
 *  "roundabout") rather than as an enum, since it has no other behavioural meaning in this model. */
struct OPENDRIVE_API FOpenDriveJunctionGroup
{
	FString Id;
	FString Name;
	FString Type;
	TArray<FString> JunctionRefs;
};

struct OPENDRIVE_API FOpenDriveRoad
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
	/** Reference-line elevation profile ("elevationProfile/elevation"). */
	TArray<FOpenDriveCubic> Elevation;
	/** Banking angle in radians about the s-axis ("lateralProfile/superelevation"). Takes precedence over Crossfall where both are present. */
	TArray<FOpenDriveCubic> Superelevation;
	/** "lateralProfile/crossfall": one-sided (or symmetric) banking, used where Superelevation is empty. */
	TArray<FOpenDriveCrossfallEntry> Crossfall;
	/** "lateralProfile/shape": additive cross-section height as a function of (s, t) -- see FOpenDriveShapeEntry. Sorted by (S, T). */
	TArray<FOpenDriveShapeEntry> Shape;
	TArray<FOpenDriveCubic> LaneOffset;
	TArray<FOpenDriveLaneSection> LaneSections;
	/** Road-type speed limits (absolute s), used where a lane has none. */
	TArray<FOpenDriveSpeedLimit> SpeedLimits;
	/** Road category entries (absolute s); see FOpenDriveRoadTypeEntry. */
	TArray<FOpenDriveRoadTypeEntry> Types;
	/** Signs and traffic lights ("signals/signal"), sorted by S. */
	TArray<FOpenDriveSignal> Signals;
	/** Static objects ("objects/object") -- see FOpenDriveObject. */
	TArray<FOpenDriveObject> Objects;

	// Derived at load time, used to prune spatial queries.
	double MinX = 0.0, MinY = 0.0, MaxX = 0.0, MaxY = 0.0;
	double MaxExtent = 0.0;

	bool IsJunctionRoad() const { return !JunctionId.IsEmpty(); }
};

struct OPENDRIVE_API FOpenDriveJunctionConnection
{
	FString Id;
	FString IncomingRoad;
	FString ConnectingRoad;
	EOpenDriveContactPoint ContactPoint = EOpenDriveContactPoint::Start;
	/** (from lane on incoming road, to lane on connecting road) */
	TArray<TPair<int32, int32>> LaneLinks;
};

struct OPENDRIVE_API FOpenDriveJunction
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
 * Supports reference-line geometry (line, arc, spiral, poly3, paramPoly3), elevation, superelevation,
 * lane offsets, lane sections/widths, road links, junction connections, nearest-road lookup and
 * road-level routing. This is the shared model used by both the OpenDrive plugin and OpenScenario_UE.
 */
class OPENDRIVE_API FOpenDriveMap
{
public:
	bool LoadFromString(const FString& Xml, FString& OutError);

	const FString& GetName() const { return Name; }
	void SetName(const FString& InName) { Name = InName; }
	TArray<FOpenDriveRoad>& GetRoadsMutable() { return Roads; }
	const TArray<FOpenDriveRoad>& GetRoads() const { return Roads; }
	TArray<FOpenDriveJunction>& GetJunctionsMutable() { return Junctions; }
	const TArray<FOpenDriveJunction>& GetJunctions() const { return Junctions; }
	TArray<FOpenDriveController>& GetControllersMutable() { return Controllers; }
	const TArray<FOpenDriveController>& GetControllers() const { return Controllers; }
	TArray<FOpenDriveJunctionGroup>& GetJunctionGroupsMutable() { return JunctionGroups; }
	const TArray<FOpenDriveJunctionGroup>& GetJunctionGroups() const { return JunctionGroups; }
	double GetTotalLength() const;

	FOpenDriveRoad* FindRoadMutable(const FString& RoadId);
	const FOpenDriveRoad* FindRoad(const FString& RoadId) const;
	const FOpenDriveJunction* FindJunction(const FString& JunctionId) const;
	const FOpenDriveController* FindController(const FString& ControllerId) const;
	FOpenDriveJunctionGroup* FindJunctionGroupMutable(const FString& JunctionGroupId);
	const FOpenDriveJunctionGroup* FindJunctionGroup(const FString& JunctionGroupId) const;

	/** Rebuilds the Id -> index lookup tables and per-road spatial bounds. Call after structural edits. */
	void RebuildIndex();

	// --- Geometry -----------------------------------------------------------------------------
	/** Reference line pose (t = 0), without elevation. */
	void EvaluateReferenceLine(const FOpenDriveRoad& Road, double S, double& X, double& Y, double& Heading) const;
	/** Pose at (s, t). Heading is the reference line heading (direction of increasing s). */
	FOpenDrivePose EvaluatePose(const FOpenDriveRoad& Road, double S, double T) const;
	/** Banking angle in radians at S (0 if the road has no superelevation profile). */
	double GetSuperelevation(const FOpenDriveRoad& Road, double S) const;
	/** Crossfall banking angle in radians at S for the given side (0 if the road has no crossfall profile for that side). */
	double GetCrossfallAngle(const FOpenDriveRoad& Road, double S, bool bLeftSide) const;
	/** Additive "road carving" height from the lateral profile shape at (s, t) (0 if the road has no shape profile). */
	double GetShapeZ(const FOpenDriveRoad& Road, double S, double T) const;

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

	/** Speed limit in m/s valid for the lane at S (lane limit first, then road type), or a negative value if none. */
	double GetSpeedLimit(const FOpenDriveRoad& Road, double S, int32 LaneId) const;
	/** Signed curvature (1/m, positive = turning left) of the reference line at S. */
	double GetReferenceCurvature(const FOpenDriveRoad& Road, double S) const;
	/** Signed curvature of the path along the centre of the given lane. */
	double GetLaneCurvature(const FOpenDriveRoad& Road, double S, int32 LaneId) const;

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

	/** Recomputes the world-space bounds and cross-section extent of a single road. Call after editing its geometry or lanes. */
	void ComputeRoadBounds(FOpenDriveRoad& Road) const;

private:
	int32 FindGeometryIndex(const FOpenDriveRoad& Road, double S) const;

	FString Name;
	TArray<FOpenDriveRoad> Roads;
	TMap<FString, int32> RoadIndex;
	TArray<FOpenDriveJunction> Junctions;
	TMap<FString, int32> JunctionIndex;
	TArray<FOpenDriveController> Controllers;
	TArray<FOpenDriveJunctionGroup> JunctionGroups;
};
