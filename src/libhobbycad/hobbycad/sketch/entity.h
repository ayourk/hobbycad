// =====================================================================
//  src/libhobbycad/hobbycad/sketch/entity.h — Sketch entity types
// =====================================================================
//
//  Unified sketch entity representation used by both the library and GUI.
//  The GUI can extend these with view-specific state.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_ENTITY_H
#define HOBBYCAD_SKETCH_ENTITY_H

#include "../core.h"
#include "../geometry/types.h"
#include "../types.h"

#include <string>
#include <vector>
#include <limits>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Entity Types
// =====================================================================

/// Types of sketch entities
enum class EntityType {
    Point,         ///< Single point
    Line,          ///< Line segment (2 endpoints)
    Rectangle,     ///< Axis-aligned rectangle (2 corner points) or angled (4 corner points)
    Parallelogram, ///< Parallelogram (4 corner points: p1, p2, p3, p4 where p4 = p1 + (p3 - p2))
    Circle,        ///< Circle (center + radius)
    Arc,           ///< Arc (center + radius + angles)
    Spline,        ///< Catmull-Rom spline (control points)
    Polygon,       ///< Regular polygon (center + radius + sides)
    Slot,          ///< Slot: Linear (2 arc centers + radius) or Arc (arc center + start + end + radius)
    Ellipse,       ///< Ellipse (center + major/minor radii)
    Text,          ///< Text annotation
    Dimension      ///< Dimension annotation (GUI-only, not stored as geometry)
};

// =====================================================================
//  Sketch Entity
// =====================================================================

/// A single sketch entity (point, line, circle, etc.)
///
/// This is the core data structure for sketch geometry. The GUI extends
/// this with selection state and other view-specific properties.
struct HOBBYCAD_EXPORT Entity {
    int id = 0;                           ///< Unique ID within the sketch
    EntityType type = EntityType::Line;   ///< Entity type

    // Geometry data (interpretation depends on type)
    std::vector<Point3> points;           ///< Control/definition points (3D; z==0 = on-plane / 2D)
    double radius = 0.0;                  ///< For circles, arcs, slots, polygons
    double startAngle = 0.0;              ///< For arcs (degrees)
    double sweepAngle = 360.0;            ///< For arcs (degrees)
    int sides = 6;                        ///< For polygons
    double majorRadius = 0.0;             ///< For ellipses
    double minorRadius = 0.0;             ///< For ellipses
    double ellipseRotation = 0.0;         ///< Ellipse major-axis angle (degrees, CCW from +X)
    double ellipseStart = 0.0;            ///< Elliptical-arc start parameter (degrees; 0 = major +axis)
    double ellipseSweep = 360.0;          ///< Elliptical-arc sweep (degrees; 360 = full ellipse)
    std::string text;                     ///< For text entities
    std::string fontFamily;               ///< Font family (empty = default)
    double fontSize = 12.0;               ///< Font size in mm
    bool fontBold = false;                ///< Bold text
    bool fontItalic = false;              ///< Italic text
    double textRotation = 0.0;            ///< Text rotation in degrees

    // Arc slot specific
    bool arcFlipped = false;              ///< For arc slots: true = >180 degree arc
    // Spline flavor
    bool splineBezier = false;
    bool splineClosed = false;            ///< Bezier spline is a closed loop (last segment wraps to control point 0)            ///< Spline is piecewise cubic Bezier (control points are handles, a solver curve) vs Catmull-Rom (interpolating, points-only)
    bool splineRational = false;          ///< Bezier spline is rational (weighted): weights[] per control point, registered as SLVS_E_RATIONAL_CUBIC
    std::vector<double> weights;          ///< Rational spline control-point weights (one per control point; empty = non-rational, all 1)
    /// Conic arc by rho (conicFromRho): the stored rho, a property of the
    /// curve as authored; 0 = not a conic. Cleared by any hand edit of the
    /// control polygon.
    double conicRho = 0.0;

    /// For a Slot: the id(s) of the centerline segment(s) this slot follows,
    /// empty when it is not path-following.
    ///
    /// Slots are CENTERLINE-DRIVEN references, not baked geometry. Every slot-
    /// creation path builds construction centerline geometry and links the slot
    /// to it here (GUI tool -> createCenterlineSlot; GUI sweep + CLI ->
    /// addSlotCenterline; tree -> createTreeSlotFromSelection). ONE unified
    /// model: exactly one id = a simple slot along a single Line or Arc, re-
    /// derived by updateSlotFromPath into `points` (smooth capsule caps); more
    /// than one id = a chain/loop/branching tree, re-derived by
    /// updateSlotOutlineFromPaths into `outlineCache` (the swept outline
    /// polygon). Re-derived after every solve (updateSlotsFromPaths / cmdSolve);
    /// the solver skips a path-following slot's own points and the user
    /// constrains/drags the centerline. Serialized as "path_entity_ids". A
    /// SPLINE centerline is not yet supported.
    std::vector<int> pathEntityIds;

    /// Cached swept outline for a multi-segment slot: a closed polygon (CCW)
    /// from sketch::slotOutline(). Empty for a simple slot, which renders
    /// from `points`. Derived, so it is recomputed on solve, not authored.
    std::vector<Point2D> outlineCache;

    /// Associative offset link. When >= 0, this entity is an offset copy of
    /// entity offsetParentId, kept at offsetDistance on offsetSide; a solve
    /// re-derives it from the parent the way a slot follows its path.
    /// offsetSide is +1/-1 (line: which perpendicular side; circle/arc: +1
    /// outward, -1 inward).
    int    offsetParentId = -1;
    double offsetDistance = 0.0;
    int    offsetSide     = 0;

    /// Associative projection link. When >= 0, this entity is a projection of
    /// entity projectionSourceId onto this sketch's plane; a solve re-derives
    /// its points from the source through the two planes' bases (see
    /// updateProjectionFromSource). The source may live on a DIFFERENT plane;
    /// projection is the cross-plane analogue of the offset link. The projected
    /// entity is reference geometry: driven by its source, not the solver.
    int    projectionSourceId = -1;
    /// Which sketch owns the projection source (-1 = same sketch / unset).
    /// Entity ids are unique only within a sketch, so cross-sketch projection
    /// needs both ids to resolve the source at solve time.
    int    projectionSourceSketchId = -1;

    // State
    bool isConstruction = false;          ///< Construction geometry flag
    bool isCenterline = false;            ///< Centerline linetype (dash-dot reference axis)
    int  color = -1;                      ///< Per-entity RGB (0xRRGGBB); -1 = default / by-layer
    bool constrained = false;             ///< Has constraints applied
    int groupId = -1;                     ///< Owning decomposition group (-1 if none)

    // ---- Convenience Methods ----

    /// Get the bounding box of the entity
    geometry::BoundingBox boundingBox() const;

    /// Get endpoints for entities that have them (lines, arcs)
    /// Returns empty vector for other types
    std::vector<Point2D> endpoints() const;

    /// The points at which another entity may join this one: endpoints(),
    /// plus a point's position and a rectangle's four corners. This is what
    /// chain selection follows; endpoints() stays the open-curve notion that
    /// profile detection needs.
    std::vector<Point2D> connectionPoints() const;

    /// Check if a point is on this entity within tolerance
    bool containsPoint(const Point2D& point, double tolerance = 0.5) const;

    /// Get the closest point on this entity to a given point
    /// Build the geometric Arc this entity describes.
    ///
    /// Only meaningful when `type == EntityType::Arc`; the fields it reads
    /// (points[0], radius, startAngle, sweepAngle) are shared with other
    /// types, so callers must check the type first. Exists because this exact
    /// four-field reconstruction was written out by hand at a dozen call
    /// sites, in both the library and the GUI.
    geometry::Arc toArc() const;

    Point2D closestPoint(const Point2D& point) const;

    /// Get distance from a point to this entity
    double distanceTo(const Point2D& point) const;

    /// Transform the entity by a 2D transformation
    void transform(const geometry::Transform2D& t);

    /// Create a transformed copy
    Entity transformed(const geometry::Transform2D& t) const;

    /// Clone the entity with a new ID
    Entity clone(int newId) const;
};

// =====================================================================
//  Entity Factory Functions
// =====================================================================

/// Create a point entity
HOBBYCAD_EXPORT Entity createPoint(int id, const Point2D& position);

/// Create a line entity
HOBBYCAD_EXPORT Entity createLine(int id, const Point2D& start, const Point2D& end);

/// Create a rectangle entity
HOBBYCAD_EXPORT Entity createRectangle(int id, const Point2D& corner1, const Point2D& corner2);

/// Create a circle entity
HOBBYCAD_EXPORT Entity createCircle(int id, const Point2D& center, double radius);

/// Create an arc entity (from center, radius, angles)
HOBBYCAD_EXPORT Entity createArc(int id, const Point2D& center, double radius,
                                  double startAngle, double sweepAngle);

/// Create an arc entity from three points
HOBBYCAD_EXPORT Entity createArcFromThreePoints(int id, const Point2D& start,
                                                  const Point2D& mid, const Point2D& end);

/// Create a spline entity
HOBBYCAD_EXPORT Entity createSpline(int id, const std::vector<Point2D>& controlPoints);
/// Create a piecewise cubic Bezier spline (control points are Bezier handles; a
/// solver curve, so tangent/curvature constraints can act on it). 3N+1 points = N segments.
HOBBYCAD_EXPORT Entity createBezierSpline(int id, const std::vector<Point2D>& controlPoints);
/// Create a RATIONAL (weighted) cubic Bezier spline: control points + one weight
/// each. Registered as SLVS_E_RATIONAL_CUBIC segments; represents exact conics.
HOBBYCAD_EXPORT Entity createRationalBezierSpline(int id, const std::vector<Point2D>& controlPoints, const std::vector<double>& weights);

/// One anchor of a Bezier authoring path: a point with optional in/out handle
/// control points. A missing handle defaults to the anchor itself (a corner).
/// inHandle is the control point BEFORE the anchor (the previous segment's
/// C_in); outHandle is the control point AFTER it (the next segment's C_out).
struct BezierAnchor {
    Point2D pos;
    bool hasIn = false;
    bool hasOut = false;
    Point2D inHandle;
    Point2D outHandle;
    double weight = 1.0;   ///< rational weight for this anchor's control points (1 = non-rational)
};

/// Assemble the cubic Bezier control polygon [P0, out0, in1, P1, out1, in2, ...,
/// inN, PN] (3N+1 points, N = anchors-1) from an authoring path, ready for
/// createBezierSpline(). A missing handle defaults to its anchor position.
/// Returns empty if fewer than two anchors are given.
HOBBYCAD_EXPORT std::vector<Point2D> bezierControlPolygon(const std::vector<BezierAnchor>& anchors);
/// Per-control-point weights parallel to bezierControlPolygon(): each control
/// point takes the weight of the anchor it belongs to (3N+1 values).
HOBBYCAD_EXPORT std::vector<double> bezierControlPolygonWeights(const std::vector<BezierAnchor>& anchors);

/// Inverse of bezierControlPolygon(): recover the authoring anchors (with their
/// in/out handle control points) from a stored Bezier control polygon. Returns
/// empty if the polygon is not a valid Bezier (must be 3N+1 points, N >= 1).
/// A handle coincident with its anchor is reported present but zero-length.
HOBBYCAD_EXPORT std::vector<BezierAnchor> bezierAnchorsFromControlPolygon(const std::vector<Point2D>& poly);

/// Which handle of an anchor a polar edit addresses.
enum class BezierHandleSide { In, Out, Tangent };

/// Place a handle by angle (degrees) and length from its anchor: Out sets
/// the out handle, In the in handle, Tangent sets the out handle and mirrors
/// the in handle through the anchor (a smooth node). `canIn` / `canOut`
/// gate the ends of an open path: the first anchor has no in handle, the
/// last no out handle.
HOBBYCAD_EXPORT void setAnchorHandle(BezierAnchor& a, BezierHandleSide side,
                                     double angleDeg, double length,
                                     bool canIn = true, bool canOut = true);

/// Polar form of a handle relative to its anchor. False when the handle is
/// absent or coincides with the anchor (a corner). The angle is in degrees
/// in atan2's range; normalize it if a display wants 0..360.
HOBBYCAD_EXPORT bool anchorHandlePolar(const BezierAnchor& a, BezierHandleSide side,
                                       double& angleDeg, double& length);

/// How an interior anchor's two handles relate: Corner when either is
/// missing or zero-length or they are not collinear through the anchor,
/// Smooth when collinear and equal in length, Asymmetric when collinear only.
enum class AnchorContinuity { Corner, Smooth, Asymmetric };
HOBBYCAD_EXPORT AnchorContinuity anchorContinuity(const BezierAnchor& a);

/// Create a polygon entity
HOBBYCAD_EXPORT Entity createPolygon(int id, const Point2D& center, double radius, int sides);

/// True when a Polygon is the PARAMETRIC kind (inscribed or circumscribed)
/// rather than a freeform one.
///
/// The two are stored differently and the difference is easy to get wrong:
///   * regular:    points[0] is the CENTER, points[1..sides] are the vertices,
///                 and `radius` is set;
///   * freeform:   points[0..sides-1] are the vertices, there is no center
///                 point, and `radius` is left at 0.
///
/// A near-zero radius is therefore the discriminator. This rule was being
/// re-derived at each call site; call this instead.
HOBBYCAD_EXPORT bool isRegularPolygon(const Entity& entity);

/// Create a linear slot entity (obround/stadium shape)
/// @param id Entity ID
/// @param center1 Center of first semicircular end
/// @param center2 Center of second semicircular end
/// @param radius Half-width of the slot (radius of end semicircles)
HOBBYCAD_EXPORT Entity createSlot(int id, const Point2D& center1, const Point2D& center2, double radius);

/// Create an arc slot entity (curved slot following an arc path)
/// Storage format: points[0] = arc center, points[1] = start endpoint, points[2] = end endpoint
/// @param id Entity ID
/// @param arcCenter Center of the arc that the slot follows
/// @param start Start endpoint (on the arc)
/// @param end End endpoint (on the arc)
/// @param radius Half-width of the slot
/// @param flipped True for >180 degree arcs (inverts the sweep direction)
/// The sweep at which an arc slot closes on itself.
///
/// Aaron's rule, 2026-02-20: "Full circle minus 2x radius of arc end."
/// The point of it, 2026-08-28: "an arc slot that could serve as a dial
/// indicator where the 2 ends of the arc slot meet."
///
/// So this is the useful MAXIMUM, not a safety margin.
///
/// Aaron was precise about what "meet" means, 2026-08-28: "the arc ends
/// would never touch, but 1 arc end could touch the perimeter of the slot
/// and vice versa." The two centerline ENDPOINTS stay exactly two cap
/// radii apart and never coincide; what comes into contact is the
/// PERIMETER: one end's cap arc against the other's. Two circles of
/// radius halfWidth whose centers are 2 x halfWidth apart are tangent, so
/// those two statements describe the same configuration.
///
/// That separation is a CHORD of the centerline circle, so it subtends
/// 2 * asin(halfWidth / pathRadius) at the center. The endpoints therefore
/// end up exactly the slot's WIDTH apart: the gap left in the ring is as
/// wide as the slot itself.
///
/// WHY THE EXACT VALUE MATTERS, and why the obvious formula is not good
/// enough. Aaron, 2026-08-28: "The point of this max distance is to
/// produce an almost triangle tip around the perimeter allowing the center
/// piece to be seperated from the outer shell."
///
/// At this sweep the two cap circles are tangent, and the material left
/// between them and the outer wall is a curvilinear triangle tapering to a
/// CUSP at the point of tangency. That cusp is the last thing joining the
/// center piece to the outer shell.
///
/// Reading "minus 2x radius" as an ARC length instead (the obvious
/// 360 - 2*halfWidth/pathRadius) makes the caps OVERLAP, because an arc
/// is longer than the chord it spans. At r=30, width=8 that is 0.024mm of
/// overlap: enough to cut the tip off and detach the center piece early.
/// The two formulas differ by 0.046 degrees, which is exactly the sort of
/// difference that looks like rounding and is not.
///
/// tests/cli/slots.cpp measures the gap between the endpoints rather than
/// the angle, so the arc-length form fails it.
///
/// Sweeping FURTHER makes the caps overlap and the outline
/// self-intersect, so this is also the limit.
///
/// The rule was agreed in 2026 and then never implemented; the shape was
/// constructible past this point in every front end.
///
/// @param pathRadius  Radius out to the slot's centerline. Must be > 0.
/// @param halfWidth   Half the slot's width, i.e. the cap radius.
/// @return the limit in DEGREES, always below 360. Zero if the inputs
///         cannot describe a slot at all.
HOBBYCAD_EXPORT double maxArcSlotSweepDegrees(double pathRadius,
                                              double halfWidth);

/// The angle at which an arc slot's two ends are TANGENT.
///
/// The same quantity as maxArcSlotSweepDegrees() seen from the other side:
/// that is a full turn less this. Both exist because the two front ends ask
/// the question in different directions: placement wants a MINIMUM
/// separation as the ends are brought together, and a typed sweep wants a
/// MAXIMUM before they close.
///
/// Aaron, 2026-08-28, deriving it from the requirement rather than the
/// arithmetic: "Point 1 of the arc should be able to be tangent to the
/// perimeter of the slot near point 2 and vice versa. This should be able
/// to produce a cusp." Two caps of radius halfWidth are tangent when their
/// centers are 2 x halfWidth apart, one full slot width.
///
/// @return the separation in DEGREES, or 0 if no slot is possible.
HOBBYCAD_EXPORT double arcSlotCuspSeparationDegrees(double pathRadius,
                                                    double halfWidth);

/// The smallest angle the two ends may be apart at all: one cap radius.
///
/// Named apart from the cusp separation because they are different
/// questions and were briefly the same answer. Closer than the cusp
/// separation the caps OVERLAP, which is allowed and is how the middle is
/// freed; closer than THIS the ends have merged and the outline stops
/// describing the shape.
HOBBYCAD_EXPORT double arcSlotFloorSeparationDegrees(double pathRadius,
                                                     double halfWidth);

/// An arc-slot centerline arc center after the angular-separation floor.
struct SlotArcCenter {
    Point2D center;
    double  radius = 0.0;
};

/// Enforce the minimum angular separation of an arc slot's two ends: given the
/// chord (start,end), the current centerline-arc center and the slot half-width,
/// push the center out along the chord's bisector until the ends are at least
/// arcSlotFloorSeparationDegrees() apart. Returns the (possibly unchanged)
/// center and its radius. A half-width below 0.1 is treated as the UI default
/// (5.0), matching the sketch tool.
HOBBYCAD_EXPORT SlotArcCenter enforceSlotArcSeparation(
    const Point2D& start, const Point2D& end,
    const Point2D& center, double slotHalfWidth);

/// Recompute an arc's stored endpoints from its parameters: points[1] (start)
/// and points[2] (end) from the center (points[0]), radius, startAngle and
/// sweepAngle (degrees). No-op unless the entity is an Arc with >= 3 points.
HOBBYCAD_EXPORT void resyncArcEndpoints(Entity& arc);

/// Write an arc as [center, start, end] from its center, radius and angles
/// (what the solver and the canvas both expect), setting the angle fields.
HOBBYCAD_EXPORT void setArcFromAngles(Entity& arc, const Point2D& center, double radius,
                                      double startAngleDeg, double sweepAngleDeg);

/// A text entity's second point is its rotation handle: at least two font
/// sizes (or 0.6 per character) from the anchor along textRotation. Call
/// after text, fontSize or textRotation change.
HOBBYCAD_EXPORT void resyncTextHandle(Entity& text);

/// Which of an arc's two ends (1 or 2) is nearer `p`; 1 for a non-arc.
HOBBYCAD_EXPORT int nearestArcEndIndex(const Entity& arc, const Point2D& p);

/// Rescale a circle's stored perimeter points to `radius` about its center
/// (points[0]), and set the radius field. No-op for a non-circle.
HOBBYCAD_EXPORT void rescaleCircleToRadius(Entity& circle, double radius);

/// Re-derive a slot's geometry from the path it follows.
///
/// A Line path gives a straight slot whose end centers are the line's
/// endpoints; an Arc path gives an arc slot sharing the arc's center,
/// radius and sweep. The slot's WIDTH is left alone; the path says where
/// the slot goes, not how thick it is.
///
/// @param slot  Modified in place. Must be EntityType::Slot.
/// @param path  The line or arc to follow.
/// @return false, leaving `slot` untouched, if the path is not a line or
///         an arc, or does not carry enough points to describe one.
HOBBYCAD_EXPORT bool updateSlotFromPath(Entity& slot, const Entity& path);

/// The construction centerline a slot follows: a Line through the two cap
/// centers of a linear slot, or an Arc through center, start and end of an arc
/// slot. The arc's radius and angles are derived from the slot's points and
/// arcFlipped (arc slots leave sweepAngle unset) unless the caller knows them
/// exactly and passes them. The path carries no id; the caller assigns one.
HOBBYCAD_EXPORT Entity makeSlotCenterline(const Entity& slot, double pathRadius = -1.0,
                                          double startAngleDeg = std::numeric_limits<double>::quiet_NaN(),
                                          double sweepDeg = std::numeric_limits<double>::quiet_NaN());

/// The angle subtended when the two ends are `capRadiiApart` cap radii
/// apart, the general form of the two rules above.
///
///   2.0  the caps are TANGENT. The material between them pinches to a
///        cusp, the tip that still holds the middle of the ring to the
///        outside. maxArcSlotSweepDegrees() is a full turn less this.
///   1.0  the caps OVERLAP, each center sitting on the other's rim. No
///        cusp: the leftover material breaks into two separate slivers
///        and the middle comes free. Aaron asked for this case
///        deliberately, 2026-08-28: freeing the center piece is the
///        point of the exercise, and tangency only holds it by a point.
///
/// Below 1.0 the ends have effectively merged and the outline is no
/// longer telling the truth about the shape, so that is the floor.
/// A slot width has to be greater than zero, and how much greater is set
/// by the length precision (Aaron): a width at or below kDegenerateLen is
/// zero for every purpose here.
inline bool slotWidthIsPositive(double width) { return geometry::isPositiveLength(width); }

HOBBYCAD_EXPORT double arcSlotGapDegrees(double pathRadius, double halfWidth,
                                         double capRadiiApart);

/// The furthest an arc slot may sweep at all, as opposed to the furthest
/// it can sweep and still form a cusp. A full turn less the 1.0 gap.
HOBBYCAD_EXPORT double absoluteMaxArcSlotSweepDegrees(double pathRadius,
                                                      double halfWidth);

HOBBYCAD_EXPORT Entity createArcSlot(int id, const Point2D& arcCenter, const Point2D& start,
                                      const Point2D& end, double radius, bool flipped = false);

/// Create an ellipse entity
HOBBYCAD_EXPORT Entity createEllipse(int id, const Point2D& center, double majorRadius, double minorRadius,
                                     double rotationDeg = 0.0);

// ---------------------------------------------------------------------
//  An ellipse's axes, as geometry
// ---------------------------------------------------------------------
//  An ellipse stores its axes as scalars (majorRadius, minorRadius,
//  ellipseRotation), which the solver cannot move: a scalar has no
//  handle to constrain. It ALSO carries two points beside its center,
//  points[1] on the +major axis and points[2] on the +minor axis, and
//  those are ordinary solver points that can be dimensioned and dragged.
//  Fusion exposes the same thing as majorAxisLine / minorAxisLine, and
//  FreeCAD as internal-alignment geometry.
//
//  The two representations must agree, so everything that changes one
//  goes through these. The scalars stay the serialized form: a file is
//  unchanged by this, and an ellipse read from an older file (center
//  only) is brought up to date by ensureEllipseAxisPoints().

/// Give `e` its two axis points, computed from the scalars, replacing
/// whatever was there. No effect on a non-ellipse.
HOBBYCAD_EXPORT void syncEllipseAxisPoints(Entity& e);

/// Add the axis points only if they are missing (an ellipse from an older
/// file, from DXF, or from a projection). Returns true if it added them.
HOBBYCAD_EXPORT bool ensureEllipseAxisPoints(Entity& e);

/// Re-derive majorRadius, minorRadius and ellipseRotation from the axis
/// points, after the solver or a drag has moved them. Keeps major >= minor
/// by swapping the roles and turning the frame a quarter turn, so the
/// SHAPE is preserved rather than silently redrawn. Returns false if `e`
/// is not an ellipse or has no axis points to read.
HOBBYCAD_EXPORT bool syncEllipseFields(Entity& e);

/// Turn the CLICK ORDER of an ellipse placement into the stored layout.
///
/// Both GUI modes take three clicks and mean different things by them:
///   - Center + Axes: [center, major-axis end, a point giving the minor]
///   - 3-Point:       [one major end, the other major end, minor point]
/// so the center is either given outright or is the midpoint of the first
/// two clicks. In both, the THIRD click is a point the ellipse passes
/// through (Fusion, Onshape): the minor radius is solved so the curve goes
/// under that click, and only when the click lies beyond the major extent
/// does its perpendicular distance stand in.
///
/// `out` receives majorRadius, minorRadius, ellipseRotation and the
/// canonical points (center, +major end, +minor end). Two clicks are
/// accepted so an interrupted placement still commits, with the minor
/// axis defaulting to half the major. Returns false if `clicks` has fewer
/// than two entries or the major axis is degenerate.
///
/// This lives here, not in the tool handler, because a wrong layout
/// renders identically on screen and is invisible until something
/// downstream reads the points.
/// With FIVE clicks the last two are points on the ellipse choosing an arc,
/// the SHORTER way around between them by default and the long way when
/// `longWay` is set (the circular arc tools' Shift). The stored sweep is
/// always positive and counter-clockwise, in (0, 360]: when the short arc
/// runs clockwise from click four it is stored from click five instead.
/// Clicking the same point twice is a full turn. Four clicks (the end still
/// being placed) set the start and leave the sweep full.
HOBBYCAD_EXPORT bool ellipseFromClicks(bool threePoint,
                                       const std::vector<Point2D>& clicks,
                                       Entity& out,
                                       bool longWay = false);

/// How an ellipse (or elliptical arc) is placed by clicks. One entry point,
/// ellipseFromPlacement(), turns any prefix of the clicks (the last one
/// usually being the live cursor) into the entity the placement describes,
/// so the preview, the cursor constraint and the commit all read one rule.
enum class EllipsePlacement {
    CenterAxes,   ///< center, +major end, a point the ellipse passes through
    ThreePoint,   ///< the two major ends, a point the ellipse passes through
    Arc,          ///< CenterAxes, then start and end on the perimeter (5)
    SpanRise,     ///< arc: the two ends of the span, then the apex (half ellipse)
    Corner,       ///< arc: the corner (center), a point on each leg (quarter ellipse)
    Endpoints     ///< arc: two perimeter points, the center, then the axis direction
};

/// How many clicks a placement takes to commit.
HOBBYCAD_EXPORT int ellipsePlacementClicks(EllipsePlacement mode);

/// The entity a placement's clicks describe so far; false while there are
/// too few clicks or the clicks describe no ellipse (a degenerate span, an
/// Endpoints center/axis pair that no ellipse through both points fits).
///
/// SpanRise: clicks are the two ends of the span (the chord is one axis, its
/// midpoint the center) and the apex, a point the curve passes through; the
/// arc is the half on the apex's side (180). Corner: the corner is the
/// center, the second click the end of one axis, the third a point the
/// curve passes through (it sets the other radius); the arc is the quarter
/// from the first leg point to the other axis end, on the click's side.
/// Endpoints: two points ON the curve, the center, then a point giving the
/// axis direction; both radii are solved from the two points, the arc runs
/// from the first to the second, the shorter way unless `longWay`.
HOBBYCAD_EXPORT bool ellipseFromPlacement(EllipsePlacement mode,
                                          const std::vector<Point2D>& clicks,
                                          bool longWay, Entity& out);

/// A conic arc by rho, the transition curve Fusion, Onshape and SolidWorks
/// share: `start` and `end` are its endpoints, `apex` is where the two end
/// tangents meet, and `rho` in (0, 1) is how far from the chord's midpoint
/// toward the apex the curve's shoulder sits (below 0.5 an elliptical arc,
/// 0.5 a parabola, above 0.5 a hyperbola). A conic is a rational quadratic
/// Bezier (weights 1, rho/(1-rho), 1); it is stored as ONE rational CUBIC
/// Bezier segment by exact degree elevation, so the spline machinery
/// (drawing, export, the fork's SLVS_E_RATIONAL_CUBIC) already handles it.
/// False when the chord is degenerate or the apex lies on the chord's line.
HOBBYCAD_EXPORT bool conicFromRho(int id, const Point2D& start, const Point2D& end,
                                  const Point2D& apex, double rho, Entity& out);

/// The rho a cursor position implies: its projection onto the segment from
/// the chord's midpoint to the apex, clamped to [0.02, 0.98].
HOBBYCAD_EXPORT double conicRhoFromPoint(const Point2D& start, const Point2D& end,
                                         const Point2D& apex, const Point2D& p);

/// The shoulder point, midpoint + rho * (apex - midpoint): it is ON the curve.
HOBBYCAD_EXPORT Point2D conicShoulder(const Point2D& start, const Point2D& end,
                                      const Point2D& apex, double rho);

/// The apex of a conic stored by conicFromRho(): where the end tangents
/// (P0 toward P1, P3 toward P2) of its one rational cubic segment meet. It is
/// not a stored point, so this is how the panel and export show it. False
/// unless `e` is a four-point Bezier whose end tangents are not parallel.
HOBBYCAD_EXPORT bool conicApex(const Entity& e, Point2D& apex);

/// Re-author a conic (conicRho > 0) with a new rho, the Fusion property
/// route: ends, end tangent directions and the apex stay, the inner control
/// points and weights are rebuilt, and the new rho is stored. Only the
/// points, weights and rho change; every other field of `e` is untouched.
/// False when `e` is not a conic or its apex cannot be recovered.
HOBBYCAD_EXPORT bool setConicRho(Entity& e, double rho);

/// The kind of conic a rho makes: "elliptical" below 0.5, "parabolic" at
/// 0.5, "hyperbolic" above. English, for the command layer and as a
/// translation key; "" for a rho outside (0, 1).
HOBBYCAD_EXPORT const char* conicKindName(double rho);

/// The parameter angle (degrees, the ellipse's own frame, 0 = the +major
/// axis) of the direction from the ellipse's center to `p`. This is what a
/// click on the curve means as an arc endpoint, and it is NOT the polar
/// angle unless the two radii are equal. Returns 0 for a degenerate ellipse.
HOBBYCAD_EXPORT double ellipseParamDeg(const Entity& e, const Point2D& p);

/// The point of ellipse `e` at parameter angle `paramDeg` (degrees, the
/// ellipse's own frame, 0 = the +major axis), rotation included: the
/// inverse of ellipseParamDeg(). The arc range is not applied.
HOBBYCAD_EXPORT Point2D ellipsePointAtParamDeg(const Entity& e, double paramDeg);

/// True when an ellipse's arc range is a whole turn, so it is a closed
/// ellipse rather than an elliptical arc.
HOBBYCAD_EXPORT bool isFullEllipse(const Entity& e);

/// Create a text entity
HOBBYCAD_EXPORT Entity createText(int id, const Point2D& position, const std::string& text,
                                   const std::string& fontFamily = {},
                                   double fontSize = 12.0, bool bold = false,
                                   bool italic = false, double rotation = 0.0);

// =====================================================================
//  Entity Query Functions
// =====================================================================

/// Check if two entities are connected (share a common endpoint)
HOBBYCAD_EXPORT bool entitiesConnected(const Entity& e1, const Entity& e2,
                                        double tolerance = geometry::POINT_TOLERANCE);

/// Get the connection point between two entities (if connected)
HOBBYCAD_EXPORT std::optional<Point2D> connectionPoint(const Entity& e1, const Entity& e2,
                                                        double tolerance = geometry::POINT_TOLERANCE);

/// Check if entity intersects a rectangle
HOBBYCAD_EXPORT bool entityIntersectsRect(const Entity& entity, const Rect2D& rect);

/// Check if entity is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool entityEnclosedByRect(const Entity& entity, const Rect2D& rect);

/// Find the index of the nearest control point in the entity's points vector
/// @param entity The entity to search
/// @param point The reference point
/// @return Index of nearest point, or -1 if entity has no points
HOBBYCAD_EXPORT int nearestPointIndex(const Entity& entity, const Point2D& point);

/// Get the angle of a line entity in degrees (0-360)
/// @param entity The entity (must be a line)
/// @return Angle in degrees, or 0.0 if not a line
HOBBYCAD_EXPORT double getEntityAngle(const Entity& entity);

/// Find the entity with a given ID in a vector.
/// @return Pointer to the entity, or nullptr if not found.
HOBBYCAD_EXPORT const Entity* findEntityById(const std::vector<Entity>& entities, int id);
HOBBYCAD_EXPORT Entity* findEntityById(std::vector<Entity>& entities, int id);

/// Highest entity id in the container plus one (1 when empty). Derived from
/// the container rather than a running counter so a discarded and rebuilt
/// sketch never hands out ids with gaps.
HOBBYCAD_EXPORT int nextFreeEntityId(const std::vector<Entity>& entities);

/// The four corners of a Rectangle, in edge order: the stored corners of a
/// 4-point (rotated) rectangle, or the two diagonal corners of an axis-aligned
/// one expanded. Returns false for any other entity.
HOBBYCAD_EXPORT bool rectangleCorners(const Entity& rect, Point2D out[4]);

/// The four corners of a four-sided entity, in edge order: a Rectangle (as
/// rectangleCorners) or a Parallelogram (its four stored corners). The query,
/// export and B-rep code share it so the two shapes are handled alike.
/// Returns false for any other entity or too few points.
HOBBYCAD_EXPORT bool quadCorners(const Entity& entity, Point2D out[4]);

/// The four corners of the rotated rectangle whose first edge is p1 -> p2
/// and whose width is p3's perpendicular distance from that edge (the
/// three-point rectangle tool): p1, p2, p2 + w, p1 + w. False when p1 and
/// p2 coincide.
HOBBYCAD_EXPORT bool rectangleFromThreePoints(const Point2D& p1, const Point2D& p2,
                                              const Point2D& p3, Point2D out[4]);

/// The fourth corner of the parallelogram p1, p2, p3, p4: p1 + (p3 - p2).
inline Point2D parallelogramFourthCorner(const Point2D& p1, const Point2D& p2, const Point2D& p3)
{
    return p1 + (p3 - p2);
}

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_ENTITY_H
