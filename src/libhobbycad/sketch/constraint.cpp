// =====================================================================
//  src/libhobbycad/sketch/constraint.cpp — Sketch constraint implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/constraint.h>
#include "hobbycad/parameters.h"
#include <hobbycad/sketch/entity.h>
#include <hobbycad/geometry/utils.h>

#include <cmath>
#include <string>
#include <algorithm>
#include <unordered_set>

namespace hobbycad {
namespace sketch {

using namespace geometry;

// =====================================================================
//  Constraint Query Functions
// =====================================================================

bool isDimensionalConstraint(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle:
    case ConstraintType::FixedAngle:
    case ConstraintType::CurvatureDimension:
    case ConstraintType::TangentAngle:
        return true;
    default:
        return false;
    }
}

bool isGeometricConstraint(ConstraintType type)
{
    return !isDimensionalConstraint(type);
}

int requiredEntityCount(ConstraintType type)
{
    switch (type) {
    // Single entity constraints
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::FixedPoint:
    case ConstraintType::FixedAngle:
    case ConstraintType::CurvatureDimension:
    case ConstraintType::TangentAngle:
        return 1;

    // Two entity constraints
    case ConstraintType::Distance:
    case ConstraintType::Angle:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Tangent:
    case ConstraintType::Equal:
    case ConstraintType::Concentric:
    case ConstraintType::Collinear:
    case ConstraintType::PointOnLine:
    case ConstraintType::PointOnCircle:
    // Midpoint pins a point to the midpoint of a line: point + line, two
    // entities. The solver realizes it with SLVS_C_AT_MIDPOINT, which takes
    // exactly those two. (It was listed as three, so the CLI demanded a
    // third id the solver ignored, and the redundancy check then fired on a
    // malformed constraint: the "already implied" misreport, B7.)
    case ConstraintType::Midpoint:
        return 2;

    // Three entity constraints
    case ConstraintType::Symmetric:
        return 3;

    default:
        return 2;
    }
}

const char* constraintTypeName(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:      return "Distance";
    case ConstraintType::Radius:        return "Radius";
    case ConstraintType::Diameter:      return "Diameter";
    case ConstraintType::Angle:         return "Angle";
    case ConstraintType::Horizontal:    return "Horizontal";
    case ConstraintType::Vertical:      return "Vertical";
    case ConstraintType::Parallel:      return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Coincident:    return "Coincident";
    case ConstraintType::Tangent:       return "Tangent";
    case ConstraintType::Curvature:     return "Curvature";
    case ConstraintType::Equal:         return "Equal";
    case ConstraintType::Midpoint:      return "Midpoint";
    case ConstraintType::Symmetric:     return "Symmetric";
    case ConstraintType::Concentric:    return "Concentric";
    case ConstraintType::Collinear:     return "Collinear";
    case ConstraintType::PointOnLine:   return "Point On Line";
    case ConstraintType::PointOnCircle: return "Point On Circle";
    case ConstraintType::PointOnSpline: return "Point On Spline";
    case ConstraintType::CurvatureDimension: return "Radius Of Curvature";
    case ConstraintType::TangentAngle: return "Tangent Angle";
    case ConstraintType::FixedPoint:    return "Fixed Point";
    case ConstraintType::FixedAngle:    return "Fixed Angle";
    default:                            return "Unknown";
    }
}

std::vector<ConstraintType> allConstraintTypes()
{
    return {
        ConstraintType::Distance,      ConstraintType::Radius,
        ConstraintType::Diameter,      ConstraintType::Angle,
        ConstraintType::Horizontal,    ConstraintType::Vertical,
        ConstraintType::Parallel,      ConstraintType::Perpendicular,
        ConstraintType::Coincident,    ConstraintType::Tangent,
        ConstraintType::Curvature,     ConstraintType::Equal,
        ConstraintType::Midpoint,
        ConstraintType::Symmetric,     ConstraintType::Concentric,
        ConstraintType::Collinear,     ConstraintType::PointOnLine,
        ConstraintType::PointOnCircle, ConstraintType::PointOnSpline,
        ConstraintType::CurvatureDimension, ConstraintType::FixedPoint,
        ConstraintType::FixedAngle,   ConstraintType::TangentAngle,
    };
}

namespace {

/// Lower-case, and drop spaces and underscores.
std::string normalizeTypeName(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == ' ' || c == '_' || c == '-') continue;
        out += static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

bool parseConstraintTypeName(const std::string& name, ConstraintType* out)
{
    if (!out) return false;
    const std::string want = normalizeTypeName(name);
    if (want.empty()) return false;

    // The canonical names first, so an alias can never shadow one.
    for (ConstraintType t : allConstraintTypes()) {
        if (normalizeTypeName(constraintTypeName(t)) == want) {
            *out = t;
            return true;
        }
    }

    // Short forms, for the names that are a nuisance to type in full.
    struct Alias { const char* text; ConstraintType type; };
    static const Alias kAliases[] = {
        {"horiz",     ConstraintType::Horizontal},
        {"vert",      ConstraintType::Vertical},
        {"perp",      ConstraintType::Perpendicular},
        {"dia",       ConstraintType::Diameter},
        {"rad",       ConstraintType::Radius},
        {"coincide",  ConstraintType::Coincident},
        {"tan",       ConstraintType::Tangent},
        {"dist",      ConstraintType::Distance},
        {"pointonline",   ConstraintType::PointOnLine},
        {"pointoncircle", ConstraintType::PointOnCircle},
    };
    for (const Alias& a : kAliases) {
        if (want == a.text) {
            *out = a.type;
            return true;
        }
    }
    return false;
}

bool isAngularConstraint(ConstraintType type)
{
    // Every constraint whose value is in degrees (constraintUnit returns the
    // degree sign for the same three).
    return type == ConstraintType::Angle ||
           type == ConstraintType::FixedAngle ||
           type == ConstraintType::TangentAngle;
}

const char* constraintUnit(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::CurvatureDimension:
        return "mm";
    case ConstraintType::Angle:
    case ConstraintType::FixedAngle:
    case ConstraintType::TangentAngle:
        return "\xC2\xB0";
    default:
        return "";
    }
}

// =====================================================================
//  Constraint Detection
// =====================================================================

std::vector<ConstraintType> suggestConstraints(const Entity& e1, const Entity& e2)
{
    std::vector<ConstraintType> suggestions;

    // Line-Line constraints
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        suggestions.push_back(ConstraintType::Parallel);
        suggestions.push_back(ConstraintType::Perpendicular);
        suggestions.push_back(ConstraintType::Equal);
        suggestions.push_back(ConstraintType::Collinear);
        suggestions.push_back(ConstraintType::Angle);

        // Check if they share an endpoint
        if (entitiesConnected(e1, e2)) {
            suggestions.push_back(ConstraintType::Coincident);
        }
    }
    // Point-Line constraints
    else if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
             (e1.type == EntityType::Line && e2.type == EntityType::Point)) {
        suggestions.push_back(ConstraintType::PointOnLine);
        suggestions.push_back(ConstraintType::Distance);
        suggestions.push_back(ConstraintType::Midpoint);
    }
    // Point-Point constraints
    else if (e1.type == EntityType::Point && e2.type == EntityType::Point) {
        suggestions.push_back(ConstraintType::Coincident);
        suggestions.push_back(ConstraintType::Distance);
    }
    // Circle/Arc constraints
    else if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
             (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        suggestions.push_back(ConstraintType::Concentric);
        suggestions.push_back(ConstraintType::Equal);
        suggestions.push_back(ConstraintType::Tangent);
    }
    // Line-Circle constraints
    else if ((e1.type == EntityType::Line &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Line)) {
        suggestions.push_back(ConstraintType::Tangent);
        suggestions.push_back(ConstraintType::Distance);
    }
    // Point-Circle constraints
    else if ((e1.type == EntityType::Point &&
              (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
             ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
              e2.type == EntityType::Point)) {
        suggestions.push_back(ConstraintType::PointOnCircle);
        // Coincident on a point + a whole circle/arc means ON THE PERIMETER
        // (handled as Point-On-Circle in the GUI). Coinciding with the CENTER is
        // done by selecting the center POINT and using a point-to-point
        // coincidence, so Coincident is not offered for the point+curve pair.
    }

    // Bezier spline G2 and point-on-spline (solver enforces the Bezier form)
    else if (e1.type == EntityType::Spline && e2.type == EntityType::Spline) {
        suggestions.push_back(ConstraintType::Curvature);
    }
    else if ((e1.type == EntityType::Spline && e2.type == EntityType::Arc) ||
             (e1.type == EntityType::Arc && e2.type == EntityType::Spline)) {
        suggestions.push_back(ConstraintType::Curvature);
    }
    else if ((e1.type == EntityType::Point && e2.type == EntityType::Spline) ||
             (e1.type == EntityType::Spline && e2.type == EntityType::Point)) {
        suggestions.push_back(ConstraintType::PointOnSpline);
    }

    // Always suggest distance as fallback
    if (!hobbycad::contains(suggestions, ConstraintType::Distance)) {
        suggestions.push_back(ConstraintType::Distance);
    }

    return suggestions;
}

std::vector<ConstraintType> suggestConstraints(const Entity& entity)
{
    std::vector<ConstraintType> suggestions;

    switch (entity.type) {
    case EntityType::Line:
        suggestions.push_back(ConstraintType::Horizontal);
        suggestions.push_back(ConstraintType::Vertical);
        suggestions.push_back(ConstraintType::FixedAngle);
        suggestions.push_back(ConstraintType::Distance);  // Length
        break;

    case EntityType::Circle:
    case EntityType::Arc:
        suggestions.push_back(ConstraintType::Radius);
        suggestions.push_back(ConstraintType::Diameter);
        break;

    case EntityType::Point:
        suggestions.push_back(ConstraintType::FixedPoint);
        break;

    default:
        break;
    }

    return suggestions;
}

double calculateConstraintValue(
    ConstraintType type,
    const std::vector<const Entity*>& entities,
    const std::vector<int>& pointIndices)
{
    if (entities.empty()) return 0.0;

    switch (type) {
    case ConstraintType::Distance:
        if (entities.size() >= 2) {
            // Distance between two entities
            const Entity* e1 = entities[0];
            const Entity* e2 = entities[1];

            // Point to point
            if (e1->type == EntityType::Point && e2->type == EntityType::Point) {
                if (!e1->points.empty() && !e2->points.empty()) {
                    return lineLength(e1->points[0], e2->points[0]);
                }
            }
            // Line length (when both points are on same line)
            if (e1->type == EntityType::Line && e1->points.size() >= 2) {
                return lineLength(e1->points[0], e1->points[1]);
            }
        } else if (entities.size() == 1) {
            // Single entity - measure its length
            const Entity* e = entities[0];
            if (e->type == EntityType::Line && e->points.size() >= 2) {
                return lineLength(e->points[0], e->points[1]);
            }
        }
        break;

    case ConstraintType::Radius:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
                return e->radius;
            }
        }
        break;

    case ConstraintType::Diameter:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
                return e->radius * 2.0;
            }
        }
        break;

    case ConstraintType::Angle:
        if (entities.size() >= 2) {
            const Entity* e1 = entities[0];
            const Entity* e2 = entities[1];

            if (e1->type == EntityType::Line && e2->type == EntityType::Line) {
                if (e1->points.size() >= 2 && e2->points.size() >= 2) {
                    Point2D d1 = e1->points[1] - e1->points[0];
                    Point2D d2 = e2->points[1] - e2->points[0];
                    return angleBetween(d1, d2);
                }
            }
        }
        break;

    case ConstraintType::FixedAngle:
        if (!entities.empty()) {
            const Entity* e = entities[0];
            if (e->type == EntityType::Line && e->points.size() >= 2) {
                return getEntityAngle(*e);  // returns [0, 360)
            }
        }
        break;

    default:
        break;
    }

    return 0.0;
}

ConstraintType suggestConstraintType(const Entity& e1, const Entity& e2)
{
    // Point-Point: Distance
    if (e1.type == EntityType::Point && e2.type == EntityType::Point) {
        return ConstraintType::Distance;
    }

    // Point-Line: Distance (or PointOnLine)
    if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
        (e1.type == EntityType::Line && e2.type == EntityType::Point)) {
        return ConstraintType::Distance;
    }

    // Line-Line: Angle (most common use case)
    if (e1.type == EntityType::Line && e2.type == EntityType::Line) {
        return ConstraintType::Angle;
    }

    // Circle/Arc: Radius for single, Concentric for pair
    if ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) &&
        (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) {
        return ConstraintType::Concentric;
    }

    // Line-Circle: Tangent
    if ((e1.type == EntityType::Line && (e2.type == EntityType::Circle || e2.type == EntityType::Arc)) ||
        ((e1.type == EntityType::Circle || e1.type == EntityType::Arc) && e2.type == EntityType::Line)) {
        return ConstraintType::Tangent;
    }

    // Default: Distance
    return ConstraintType::Distance;
}

bool isFixedPointOn(const Constraint& c, int entityId, int pointIndex)
{
    return c.type == ConstraintType::FixedPoint && !c.entityIds.empty()
        && c.entityIds[0] == entityId
        && (c.pointIndices.empty() ? 0 : c.pointIndices[0]) == pointIndex;
}

bool dimensionDrivesPoint(const Constraint& c, int entityId, int pointIndex)
{
    if (c.type != ConstraintType::Distance) return false;
    for (std::size_t k = 0; k < c.entityIds.size(); ++k) {
        const int pi = k < c.pointIndices.size() ? c.pointIndices[k] : -1;
        if (c.entityIds[k] == entityId && pi == pointIndex) return true;
    }
    return false;
}

Constraint makeFixedPoint(int id, int entityId, int pointIndex)
{
    Constraint fp;
    fp.id = id;
    fp.type = ConstraintType::FixedPoint;
    fp.entityIds = {entityId};
    fp.pointIndices = {pointIndex};
    fp.enabled = true; fp.isDriving = true; fp.satisfied = true;
    fp.labelVisible = false;
    return fp;
}

Constraint makeDimensionConstraint(int id, ConstraintType type,
                                   const std::vector<int>& entityIds,
                                   const std::vector<int>& pointIndices,
                                   double value, const Point2D& labelPos,
                                   bool driving, bool supplementary,
                                   const Entity* firstEntity)
{
    Constraint c;
    c.id = id;
    c.type = type;
    c.entityIds = entityIds;
    c.pointIndices = pointIndices;
    c.value = value;
    c.isDriving = driving;
    c.labelPosition = labelPos;
    c.supplementary = supplementary;
    c.enabled = true;
    c.satisfied = true;
    if ((type == ConstraintType::Radius || type == ConstraintType::Diameter)
        && firstEntity && firstEntity->type == EntityType::Circle && !firstEntity->points.empty()) {
        const Point2D dir = labelPos - Point2D(firstEntity->points[0]);
        c.labelAngle = std::atan2(dir.y, dir.x);
    }
    return c;
}

bool isCoincidentBetween(const Constraint& c, int e1, int i1, int e2, int i2)
{
    if (c.type != ConstraintType::Coincident || c.entityIds.size() < 2 || c.pointIndices.size() < 2)
        return false;
    const bool a = (c.entityIds[0] == e1 && c.pointIndices[0] == i1 &&
                    c.entityIds[1] == e2 && c.pointIndices[1] == i2);
    const bool b = (c.entityIds[0] == e2 && c.pointIndices[0] == i2 &&
                    c.entityIds[1] == e1 && c.pointIndices[1] == i1);
    return a || b;
}

AngleDimensionProblem angleDimensionBetweenLines(const Entity& la, const Entity& lb, AngleDimension& out)
{
    if (la.type != EntityType::Line || lb.type != EntityType::Line
        || la.points.size() < 2 || lb.points.size() < 2)
        return AngleDimensionProblem::NotTwoLines;
    const Point2D a0(la.points[0]), a1(la.points[1]);
    const Point2D b0(lb.points[0]), b1(lb.points[1]);
    const Point2D da = a1 - a0, db = b1 - b0;
    const double na = geometry::length(da), nb = geometry::length(db);
    if (na < geometry::kZeroEps || nb < geometry::kZeroEps) return AngleDimensionProblem::Degenerate;
    constexpr double kParallelSin = 1.7e-3;   // about a tenth of a degree
    if (std::abs(geometry::cross(da, db)) / (na * nb) < kParallelSin)
        return AngleDimensionProblem::Parallel;

    const double raw = geometry::angleBetween(da, db);
    auto nr = [](const Point2D& p, const Point2D& q) {
        return geometry::lineLength(p, q) < geometry::kDegenerateLen;
    };
    out.vertex = (a0 + b0) / 2.0;
    Point2D aOther, bOther;
    out.sharedVertex = true;
    if      (nr(a0, b0)) { out.vertex = a0; aOther = a1; bOther = b1; }
    else if (nr(a0, b1)) { out.vertex = a0; aOther = a1; bOther = b0; }
    else if (nr(a1, b0)) { out.vertex = a1; aOther = a0; bOther = b1; }
    else if (nr(a1, b1)) { out.vertex = a1; aOther = a0; bOther = b0; }
    else                 { out.sharedVertex = false; }

    out.value = raw;
    out.supplementary = false;
    out.hasBisector = false;
    if (out.sharedVertex) {
        const Point2D wa = aOther - out.vertex, wb = bOther - out.vertex;
        const double lwa = geometry::length(wa), lwb = geometry::length(wb);
        if (lwa > geometry::kZeroEps && lwb > geometry::kZeroEps) {
            const double interior = geometry::angleBetween(wa, wb);
            out.value = interior;
            out.supplementary = std::abs(interior - raw) > 0.5;
            const Point2D bis = wa / lwa + wb / lwb;
            const double bl = geometry::length(bis);
            if (bl > geometry::kZeroEps) { out.bisector = bis / bl; out.hasBisector = true; }
        }
    }
    return AngleDimensionProblem::None;
}

bool angleFromLabelSide(const Entity& e1, const Entity& e2, const Point2D& vertex,
                        const Point2D& labelPos, double& value, bool& supplementary)
{
    if (e1.type != EntityType::Line || e2.type != EntityType::Line
        || e1.points.size() < 2 || e2.points.size() < 2) return false;
    auto away = [&](const Entity& e) {
        const Point2D p0(e.points[0]), p1(e.points[1]);
        return (geometry::lineLength(vertex, p0) > geometry::lineLength(vertex, p1)) ? (p0 - vertex)
                                                                                     : (p1 - vertex);
    };
    const Point2D w1 = away(e1), w2 = away(e2);
    const double l1n = geometry::length(w1), l2n = geometry::length(w2);
    if (l1n < geometry::kZeroEps || l2n < geometry::kZeroEps) return false;
    const double theta = geometry::angleBetween(w1, w2);
    const Point2D d1 = Point2D(e1.points[1]) - Point2D(e1.points[0]);
    const Point2D d2 = Point2D(e2.points[1]) - Point2D(e2.points[0]);
    const double raw = geometry::angleBetween(d1, d2);
    const double p1 = std::atan2(w1.y, w1.x);
    const double p2 = std::atan2(w2.y, w2.x);
    const double rays[4] = { p1, p1 + M_PI, p2, p2 + M_PI };
    const Point2D lv = labelPos - vertex;
    const double rl = std::atan2(lv.y, lv.x);
    double cw = -2.0 * M_PI, ccw = 2.0 * M_PI;
    for (double r : rays) {
        const double d = geometry::wrapSweepRad(r - rl);
        if (d >= -geometry::kZeroEps && d < ccw) ccw = d;
        if (d <=  geometry::kZeroEps && d > cw)  cw  = d;
    }
    double v = radiansToDegrees(ccw - cw);
    if (v < geometry::kAngleEpsDeg) v = theta;
    value = v;
    supplementary = std::abs(v - raw) > 0.5;
    return true;
}

ConstraintType suggestDimensionType(const Entity& e1, const Entity& e2)
{
    if (e1.type == EntityType::Point && e2.type == EntityType::Point)
        return ConstraintType::Distance;
    if ((e1.type == EntityType::Point && e2.type == EntityType::Line) ||
        (e1.type == EntityType::Line && e2.type == EntityType::Point))
        return ConstraintType::Distance;
    if (e1.type == EntityType::Line && e2.type == EntityType::Line)
        return ConstraintType::Angle;
    if (e1.type == EntityType::Circle || e1.type == EntityType::Arc ||
        e2.type == EntityType::Circle || e2.type == EntityType::Arc)
        return ConstraintType::Radius;
    return ConstraintType::Distance;
}

// ---- Helper: resolve a point index on an entity ----
// For Rectangle entities, indices 2 and 3 are virtual corners:
//   0 = points[0] = (x1,y1)   1 = points[1] = (x2,y2)
//   2 = (x1,y2)               3 = (x2,y1)
static Point2D resolveEntityPoint(const Entity* e, int idx)
{
    if (e->type == EntityType::Rectangle && e->points.size() >= 2) {
        switch (idx) {
        case 0: return e->points[0];
        case 1: return e->points[1];
        case 2: return Point2D(e->points[0].x, e->points[1].y);
        case 3: return Point2D(e->points[1].x, e->points[0].y);
        default: break;
        }
    }
    if (idx >= 0 && idx < static_cast<int>(e->points.size()))
        return e->points[idx];
    return e->points.empty() ? Point3() : e->points[std::min(idx, static_cast<int>(e->points.size()) - 1)];
}

// ---- Helper: resolve a Distance endpoint for one entity ----
static Point2D resolveDistancePoint(const Entity* e, int pointIndex)
{
    if (e->type == EntityType::Point) {
        return e->points.empty() ? Point3() : e->points[0];
    }
    if (e->type == EntityType::Circle || e->type == EntityType::Arc) {
        return e->points.empty() ? Point3() : e->points[0];  // center
    }
    return resolveEntityPoint(e, pointIndex);
}

bool getConstraintEndpoints(
    const Constraint& constraint,
    EntityFinder findEntity,
    Point2D& p1, Point2D& p2)
{
    if (constraint.entityIds.empty()) {
        return false;
    }

    const Entity* e1 = findEntity(constraint.entityIds[0]);
    if (!e1) {
        return false;
    }

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            int idx1 = (constraint.pointIndices.size() > 0) ? constraint.pointIndices[0] : 0;
            int idx2 = (constraint.pointIndices.size() > 1) ? constraint.pointIndices[1] : 0;
            p1 = resolveDistancePoint(e1, idx1);
            p2 = resolveDistancePoint(e2, idx2);
            return true;
        } else {
            // Single line: endpoints
            if (e1->type == EntityType::Line && e1->points.size() >= 2) {
                p1 = e1->points[0];
                p2 = e1->points[1];
                return true;
            }
        }
        break;

    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (e1->type == EntityType::Circle || e1->type == EntityType::Arc) {
            if (e1->points.empty()) return false;
            p1 = e1->points[0];  // Center
            // Point on circle at 0 degrees
            p2 = p1 + Point2D(e1->radius, 0);
            return true;
        }
        break;

    case ConstraintType::Angle:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            if (e1->type == EntityType::Line && e2->type == EntityType::Line) {
                if (e1->points.size() < 2 || e2->points.size() < 2) return false;
                if (constraint.hasAnchorPoint()) {
                    // Use the explicit anchor vertex as p1, midpoint of far
                    // edges as p2 (for hit-testing / bounding box purposes)
                    p1 = constraint.anchorPoint;
                    p2 = ((e1->points[0] + e1->points[1]) / 2.0 +
                          (e2->points[0] + e2->points[1]) / 2.0) / 2.0;
                } else {
                    // Legacy: use midpoints of lines
                    p1 = (e1->points[0] + e1->points[1]) / 2.0;
                    p2 = (e2->points[0] + e2->points[1]) / 2.0;
                }
                return true;
            }
        }
        break;

    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::FixedAngle:
        if (e1->type == EntityType::Line && e1->points.size() >= 2) {
            p1 = e1->points[0];
            p2 = e1->points[1];
            return true;
        }
        break;

    case ConstraintType::Coincident:
    case ConstraintType::Concentric:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            if (!e1->points.empty() && !e2->points.empty()) {
                p1 = e1->points[0];
                p2 = e2->points[0];
                return true;
            }
        }
        break;

    case ConstraintType::Tangent:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Equal:
    case ConstraintType::Collinear:
        if (constraint.entityIds.size() >= 2) {
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (!e2) return false;

            // Use centroids of entities
            if (!e1->points.empty() && !e2->points.empty()) {
                Point2D c1(0, 0), c2(0, 0);
                for (const Point2D& p : e1->points) c1 = c1 + p;
                for (const Point2D& p : e2->points) c2 = c2 + p;
                p1 = c1 / static_cast<double>(e1->points.size());
                p2 = c2 / static_cast<double>(e2->points.size());
                return true;
            }
        }
        break;

    default:
        break;
    }

    return false;
}

bool getConstraintEndpoints(
    const Constraint& constraint,
    const std::vector<Entity>& entities,
    Point2D& p1, Point2D& p2)
{
    return getConstraintEndpoints(
        constraint,
        [&entities](int id) -> const Entity* { return findEntityById(entities, id); },
        p1, p2);
}

// =====================================================================
//  Constraint Utility Functions
// =====================================================================

std::unordered_set<int> getConstrainedEntityIds(const std::vector<Constraint>& constraints)
{
    std::unordered_set<int> ids;
    for (const Constraint& c : constraints) {
        if (c.enabled && c.isDriving) {
            for (int eid : c.entityIds) {
                ids.insert(eid);
            }
        }
    }
    return ids;
}

double computeDrivenValue(const Constraint& constraint,
                          EntityFinder findEntity)
{
    switch (constraint.type) {
    case ConstraintType::Distance: {
        Point2D p1, p2;
        if (getConstraintEndpoints(constraint, findEntity, p1, p2)) {
            return std::hypot(p2.x - p1.x, p2.y - p1.y);
        }
        break;
    }
    case ConstraintType::Radius: {
        if (!constraint.entityIds.empty()) {
            const Entity* entity = findEntity(constraint.entityIds[0]);
            if (entity && (entity->type == EntityType::Circle ||
                           entity->type == EntityType::Arc)) {
                return entity->radius;
            }
        }
        break;
    }
    case ConstraintType::Diameter: {
        if (!constraint.entityIds.empty()) {
            const Entity* entity = findEntity(constraint.entityIds[0]);
            if (entity && (entity->type == EntityType::Circle ||
                           entity->type == EntityType::Arc)) {
                return entity->radius * 2.0;
            }
        }
        break;
    }
    case ConstraintType::Angle: {
        if (constraint.entityIds.size() >= 2) {
            const Entity* e1 = findEntity(constraint.entityIds[0]);
            const Entity* e2 = findEntity(constraint.entityIds[1]);
            if (e1 && e2 && e1->type == EntityType::Line &&
                e2->type == EntityType::Line &&
                e1->points.size() >= 2 && e2->points.size() >= 2) {
                Point2D d1 = e1->points[1] - e1->points[0];
                Point2D d2 = e2->points[1] - e2->points[0];
                return angleBetween(d1, d2);
            }
        }
        break;
    }
    case ConstraintType::FixedAngle: {
        if (!constraint.entityIds.empty()) {
            const Entity* e = findEntity(constraint.entityIds[0]);
            if (e && e->type == EntityType::Line && e->points.size() >= 2) {
                return getEntityAngle(*e);  // returns [0, 360)
            }
        }
        break;
    }
    default:
        break;
    }
    return constraint.value;  // Return existing value if no computation possible
}

double computeDrivenValue(const Constraint& constraint,
                          const std::vector<Entity>& entities)
{
    return computeDrivenValue(
        constraint,
        [&entities](int id) -> const Entity* { return findEntityById(entities, id); });
}

bool isValidConstraintValue(ConstraintType type, double value)
{
    if (!std::isfinite(value)) return false;
    if (!isDimensionalConstraint(type)) return true;
    switch (type) {
    case ConstraintType::Angle:
        return true;                 // zero degrees is parallel, and legal
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        return geometry::isPositiveLength(value);   // zero at the length precision
    default:
        return true;
    }
}

std::string constraintOperandError(ConstraintType type,
                                   const std::vector<EntityType>& t)
{
    auto isLine  = [](EntityType e) { return e == EntityType::Line; };
    auto isCurve = [](EntityType e) { return e == EntityType::Circle || e == EntityType::Arc; };
    auto isEllipse = [](EntityType e) { return e == EntityType::Ellipse; };
    auto isSpline = [](EntityType e) { return e == EntityType::Spline; };
    auto need = [&](bool ok, const char* msg) -> std::string {
        return ok ? std::string() : std::string(msg);
    };
    switch (type) {
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (t.empty()) return {};
        return need(isCurve(t[0]), "Radius/Diameter applies to a circle or arc.");
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::FixedAngle:
        if (t.empty()) return {};
        return need(isLine(t[0]), "That applies to a line.");
    case ConstraintType::Angle:
        if (t.size() < 2) return {};
        return need(isLine(t[0]) && isLine(t[1]), "Angle is between two lines.");
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Collinear:
        if (t.size() < 2) return {};
        return need(isLine(t[0]) && isLine(t[1]), "That applies to two lines.");
    case ConstraintType::Concentric:
        if (t.size() < 2) return {};
        return need(isCurve(t[0]) && isCurve(t[1]), "Concentric applies to two circles or arcs.");
    case ConstraintType::Tangent:
        if (t.size() < 2) return {};
        // A line and an ellipse is allowed too (solved approximately, via
        // the osculating circle). Ellipse against a circle, arc or another
        // ellipse is not: libslvs has no ellipse entity to build that on.
        if ((isEllipse(t[0]) && isLine(t[1])) || (isLine(t[0]) && isEllipse(t[1]))) return {};
        return need((isCurve(t[0]) || isCurve(t[1])) && (isLine(t[0]) || isCurve(t[0]))
                    && (isLine(t[1]) || isCurve(t[1])),
                    "Tangent needs a circle or arc and a line, two curves, "
                    "or an ellipse and a line.");
    case ConstraintType::Equal:
        if (t.size() < 2) return {};
        return need((isLine(t[0]) && isLine(t[1])) || (isCurve(t[0]) && isCurve(t[1])),
                    "Equal applies to two lines, or two circles/arcs.");
    case ConstraintType::Curvature: {
        if (t.size() < 2) return {};
        auto g2ok = [&](EntityType e){ return isSpline(e) || e == EntityType::Arc; };
        return need(g2ok(t[0]) && g2ok(t[1]) && (isSpline(t[0]) || isSpline(t[1])),
                    "Curvature (G2) applies to two Bezier splines, or a Bezier spline and an arc.");
    }
    case ConstraintType::Midpoint:
        if (t.size() < 2) return {};
        return need(isLine(t[1]), "Midpoint: the second entity must be a line.");
    case ConstraintType::PointOnLine:
        if (t.size() < 2) return {};
        return need(isLine(t[1]), "Point on line: the second entity must be a line.");
    case ConstraintType::PointOnCircle:
        if (t.size() < 2) return {};
        return need(isCurve(t[1]) || isEllipse(t[1]),
                    "Point on circle: the second entity must be a circle, arc, or ellipse.");
    case ConstraintType::PointOnSpline:
        if (t.size() < 2) return {};
        return need(isSpline(t[0]) || isSpline(t[1]),
                    "Point on spline: one operand must be a Bezier spline.");
    case ConstraintType::CurvatureDimension:
        if (t.empty()) return {};
        return need(isSpline(t[0]), "Radius of curvature applies to a Bezier spline.");
    case ConstraintType::TangentAngle:
        if (t.empty()) return {};
        return need(isSpline(t[0]), "Tangent angle applies to a Bezier spline anchor.");
    // Distance, Coincident, Midpoint, Symmetric, FixedPoint: point/index based,
    // no entity-kind restriction here.
    default:
        return {};
    }
}

bool reevaluateConstraint(Constraint& c, const std::map<std::string, double>& parameters)
{
    if (c.expression.empty()) return false;
    double result = 0.0;
    if (!hobbycad::evaluateExpression(c.expression, result, parameters)) return false;
    if (result == c.value) return false;
    c.value = result;
    return true;
}

int reevaluateConstraints(std::vector<Constraint>& constraints,
                          const std::map<std::string, double>& parameters)
{
    int changed = 0;
    for (auto& c : constraints)
        if (reevaluateConstraint(c, parameters)) ++changed;
    return changed;
}

const Constraint* findConstraintById(const std::vector<Constraint>& constraints, int id)
{
    for (const Constraint& c : constraints) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

Constraint* findConstraintById(std::vector<Constraint>& constraints, int id)
{
    for (Constraint& c : constraints) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

int nextFreeConstraintId(const std::vector<Constraint>& constraints)
{
    int next = 1;
    for (const Constraint& c : constraints) {
        if (c.id >= next) next = c.id + 1;
    }
    return next;
}

}  // namespace sketch
}  // namespace hobbycad
