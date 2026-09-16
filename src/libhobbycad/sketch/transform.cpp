// =====================================================================
//  src/libhobbycad/sketch/transform.cpp — see transform.h for the model
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
#include <hobbycad/sketch/transform.h>
#include <hobbycad/units.h>
#include <hobbycad/types.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hobbycad {
namespace sketch {

namespace {

bool nearAngle(double a, double target, double tol = geometry::kZeroEps)
{
    return std::fabs(hobbycad::normalizeAngle360(a) - target) < tol
        || std::fabs(hobbycad::normalizeAngle360(a) - target - 360.0) < tol;
}

Point2D rotateAbout(const Point2D& p, const Point2D& c, double cosA, double sinA)
{
    const Point2D r = p - c;
    return {c.x + r.x * cosA - r.y * sinA, c.y + r.x * sinA + r.y * cosA};
}


std::string typeName(ConstraintType t)
{
    switch (t) {
    case ConstraintType::Horizontal: return "Horizontal";
    case ConstraintType::Vertical: return "Vertical";
    case ConstraintType::Parallel: return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Symmetric: return "Symmetric";
    case ConstraintType::Midpoint: return "Midpoint";
    case ConstraintType::FixedAngle: return "FixedAngle";
    case ConstraintType::Distance: return "Distance";
    case ConstraintType::Radius: return "Radius";
    case ConstraintType::Diameter: return "Diameter";
    default: return "constraint";
    }
}

} // namespace

bool constraintInsideSet(const Constraint& c, const std::vector<int>& memberIds)
{
    if (c.entityIds.empty()) return false;
    for (int eid : c.entityIds)
        if (!hobbycad::contains(memberIds, eid)) return false;
    return true;
}

Point2D memberCenter(const std::vector<Entity>& entities, const std::vector<int>& memberIds)
{
    // Length-weighted centroid of the curves. Aaron, 2026-09-03: the pivot
    // "should start at the geometric center of a group".
    double sx = 0.0, sy = 0.0, sw = 0.0;
    Point2D pointSum; int pointCount = 0;
    auto add = [&](const Point2D& c, double w) { sx += c.x * w; sy += c.y * w; sw += w; };
    for (const auto& e : entities) {
        if (!hobbycad::contains(memberIds, e.id)) continue;
        switch (e.type) {
        case EntityType::Circle:
            if (!e.points.empty() && e.radius > 0.0) add(e.points[0], 2.0 * hobbycad::Pi * e.radius);
            break;
        case EntityType::Arc: {
            if (e.points.empty() || e.radius <= 0.0) break;
            const double sweep = degreesToRadians(std::fabs(e.sweepAngle));
            const double midA = degreesToRadians(e.startAngle + e.sweepAngle / 2.0);
            // centroid of a circular arc lies on the bisector at r*sin(s/2)/(s/2)
            const double d = sweep > geometry::kExactEps ? e.radius * std::sin(sweep / 2.0) / (sweep / 2.0) : e.radius;
            add({e.points[0].x + d * std::cos(midA), e.points[0].y + d * std::sin(midA)}, e.radius * sweep);
            break;
        }
        case EntityType::Point:
            if (!e.points.empty()) { pointSum += e.points[0]; ++pointCount; }
            break;
        default:
            // polylines of any kind: each segment by its length
            for (size_t i = 0; i + 1 < e.points.size(); ++i) {
                const Point2D a = e.points[i], b = e.points[i + 1];
                const double len = std::hypot(b.x - a.x, b.y - a.y);
                if (len > 0.0) add({(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}, len);
            }
            break;
        }
    }
    if (sw > 0.0) return {sx / sw, sy / sw};
    if (pointCount > 0) return pointSum / static_cast<double>(pointCount);
    return {};
}

GroupTransformResult transformEntities(std::vector<Entity>& entities,
                                       std::vector<Constraint>& constraints,
                                       const std::vector<int>& memberIds,
                                       const GroupTransformParams& p,
                                       int nextEntityId,
                                       int nextConstraintId)
{
    GroupTransformResult r;
    if (memberIds.empty()) { r.refusal = "nothing to transform"; return r; }

    // ---- refusals come first, before anything is touched -----------------
    if (p.kind == GroupTransformKind::Scale && !(p.factor > 0.0)) {
        r.refusal = "scale factor must be greater than zero";
        return r;
    }
    if (p.kind == GroupTransformKind::Mirror) {
        // Symmetric (two points about an axis line) and Midpoint (point on a
        // line) only survive a mirror when the axis moves with the set.
        std::string bad;
        for (const auto& c : constraints) {
            if (c.type != ConstraintType::Symmetric && c.type != ConstraintType::Midpoint) continue;
            bool touches = false, inside = true;
            for (int eid : c.entityIds) {
                if (hobbycad::contains(memberIds, eid)) touches = true; else inside = false;
            }
            if (touches && !inside) {
                if (!bad.empty()) bad += ", ";
                bad += typeName(c.type) + " " + std::to_string(c.id);
            }
        }
        if (!bad.empty()) {
            r.refusal = "mirror would break " + bad + " (axis outside the set); mirror the axis with it or remove the constraint";
            return r;
        }
    }

    const Point2D center = p.centerGiven ? p.center : memberCenter(entities, memberIds);
    r.centerUsed = center;

    auto touched = [&](int id) {
        if (!hobbycad::contains(r.changedEntityIds, id)) r.changedEntityIds.push_back(id);
    };
    auto touchedC = [&](int id) {
        if (!hobbycad::contains(r.changedConstraintIds, id)) r.changedConstraintIds.push_back(id);
    };

    // One construction line, pinned at both ends, standing in for the frame
    // a Horizontal/Vertical constraint used to refer to. Shared by rotation
    // (any angle that is not a multiple of 90) and mirroring across a line
    // that is not axis-aligned. Pinning keeps the set's degrees of freedom
    // exactly what they were: the line is as immovable as the axis it replaces.
    int nextE = nextEntityId, nextC = nextConstraintId;
    auto addPinnedReference = [&](double angleRad, const char* what) -> int {
        Entity ref;
        ref.id = nextE++;
        ref.type = EntityType::Line;
        ref.isConstruction = true;
        const double len = 10.0;
        ref.points = {center, {center.x + len * std::cos(angleRad), center.y + len * std::sin(angleRad)}};
        entities.push_back(ref);
        r.addedEntities.push_back(ref);
        for (int pi = 0; pi < 2; ++pi) {
            Constraint fp;
            fp.id = nextC++;
            fp.type = ConstraintType::FixedPoint;
            fp.entityIds = {ref.id};
            fp.pointIndices = {pi};
            fp.labelVisible = false;
            constraints.push_back(fp);
            r.addedConstraints.push_back(fp);
        }
        r.notes.push_back("construction line " + std::to_string(ref.id) + " added as the "
                          + what + ", pinned at both ends");
        return ref.id;
    };

    // ---- geometry -----------------------------------------------------------
    switch (p.kind) {
    case GroupTransformKind::Translate:
        for (auto& e : entities) {
            if (!hobbycad::contains(memberIds, e.id)) continue;
            for (auto& pt : e.points) pt += p.delta;
            touched(e.id);
        }
        for (auto& c : constraints) {
            if (!constraintInsideSet(c, memberIds)) continue;
            c.labelPosition += p.delta;
            c.anchorPoint += p.delta;
            touchedC(c.id);
        }
        break;

    case GroupTransformKind::Rotate: {
        const double rad = degreesToRadians(p.angleDeg);
        const double cosA = std::cos(rad), sinA = std::sin(rad);
        for (auto& e : entities) {
            if (!hobbycad::contains(memberIds, e.id)) continue;
            for (auto& pt : e.points) pt = rotateAbout(pt, center, cosA, sinA);
            if (e.type == EntityType::Arc) e.startAngle = hobbycad::normalizeAngle360(e.startAngle + p.angleDeg);
            touched(e.id);
        }
        const bool quarter = nearAngle(p.angleDeg, 90.0) || nearAngle(p.angleDeg, 270.0);
        const bool half = nearAngle(p.angleDeg, 0.0) || nearAngle(p.angleDeg, 180.0);
        int refLineId = -1;
        const size_t nBefore = constraints.size();   // pins appended below are not revisited
        for (size_t ci = 0; ci < nBefore; ++ci) {
            Constraint& c = constraints[ci];
            if (!constraintInsideSet(c, memberIds)) continue;
            c.labelPosition = rotateAbout(c.labelPosition, center, cosA, sinA);
            c.anchorPoint = rotateAbout(c.anchorPoint, center, cosA, sinA);
            touchedC(c.id);
            if (c.type == ConstraintType::FixedAngle) {
                c.value = hobbycad::normalizeAngle360(c.value + p.angleDeg);
                r.notes.push_back("FixedAngle " + std::to_string(c.id) + " turned with the set");
                continue;
            }
            if (c.type != ConstraintType::Horizontal && c.type != ConstraintType::Vertical) continue;
            if (half) continue;
            if (quarter) {
                c.type = (c.type == ConstraintType::Horizontal) ? ConstraintType::Vertical
                                                                 : ConstraintType::Horizontal;
                r.notes.push_back(typeName(c.type == ConstraintType::Vertical ? ConstraintType::Horizontal
                                                                               : ConstraintType::Vertical)
                                  + " " + std::to_string(c.id) + " is now " + typeName(c.type));
                continue;
            }
            // Any other angle: the frame-absolute constraint becomes relative
            // to one construction line that rotated with the set.
            if (refLineId < 0) refLineId = addPinnedReference(rad, "rotated horizontal");
            const std::string was = typeName(c.type);
            c.type = (c.type == ConstraintType::Horizontal) ? ConstraintType::Parallel
                                                             : ConstraintType::Perpendicular;
            c.entityIds.push_back(refLineId);
            r.notes.push_back(was + " " + std::to_string(c.id) + " is now " + typeName(c.type)
                              + " to line " + std::to_string(refLineId));
        }
        break;
    }

    case GroupTransformKind::Scale:
        for (auto& e : entities) {
            if (!hobbycad::contains(memberIds, e.id)) continue;
            for (auto& pt : e.points) pt = center + (pt - center) * p.factor;
            e.radius *= p.factor;
            e.majorRadius *= p.factor;
            e.minorRadius *= p.factor;
            touched(e.id);
        }
        for (auto& c : constraints) {
            if (!constraintInsideSet(c, memberIds)) continue;
            c.labelPosition = center + (c.labelPosition - center) * p.factor;
            c.anchorPoint = center + (c.anchorPoint - center) * p.factor;
            touchedC(c.id);
            if (c.type == ConstraintType::Distance || c.type == ConstraintType::Radius
                || c.type == ConstraintType::Diameter) {
                c.value *= p.factor;
                r.notes.push_back(typeName(c.type) + " " + std::to_string(c.id) + " scaled with the set");
            }
        }
        break;

    case GroupTransformKind::Mirror: {
        // Axis: a line through the center (horizontal or vertical) or the
        // line through mirrorA-mirrorB. Everything below works from the
        // axis angle theta and one point on it.
        Point2D axisPt = center;
        double theta = p.mirrorAcrossHorizontal ? 0.0 : hobbycad::Pi / 2.0;
        if (p.mirrorLineGiven) {
            const Point2D d = p.mirrorB - p.mirrorA;
            const double len = std::hypot(d.x, d.y);
            if (len < geometry::kZeroEps) { r.refusal = "mirror line has no length"; r.addedEntities.clear(); return r; }
            axisPt = p.mirrorA;
            theta = std::atan2(d.y, d.x);
        }
        const double ux = std::cos(theta), uy = std::sin(theta);
        auto reflect = [&](const Point2D& q) -> Point2D {
            const Point2D v = q - axisPt;
            const double along = v.x * ux + v.y * uy;
            return {axisPt.x + 2.0 * along * ux - v.x, axisPt.y + 2.0 * along * uy - v.y};
        };
        const double thetaDeg = radiansToDegrees(theta);
        for (auto& e : entities) {
            if (!hobbycad::contains(memberIds, e.id)) continue;
            for (auto& pt : e.points) pt = reflect(pt);
            if (e.type == EntityType::Arc)
                e.startAngle = hobbycad::normalizeAngle360(2.0 * thetaDeg - e.startAngle - e.sweepAngle);
            touched(e.id);
        }
        // What happens to Horizontal/Vertical depends on the axis angle:
        // axis-aligned keeps them, a 45-degree axis swaps them, anything
        // else needs the pinned reference (the mirrored horizontal lies at 2*theta).
        const double t2 = hobbycad::normalizeAngle360(2.0 * thetaDeg);
        const bool keep = nearAngle(t2, 0.0) || nearAngle(t2, 180.0);
        const bool swap = nearAngle(t2, 90.0) || nearAngle(t2, 270.0);
        int refLineId = -1;
        const size_t nBefore = constraints.size();
        for (size_t ci = 0; ci < nBefore; ++ci) {
            Constraint& c = constraints[ci];
            if (!constraintInsideSet(c, memberIds)) continue;
            c.labelPosition = reflect(c.labelPosition);
            c.anchorPoint = reflect(c.anchorPoint);
            touchedC(c.id);
            if (c.type == ConstraintType::FixedAngle) {
                c.value = hobbycad::normalizeAngle360(2.0 * thetaDeg - c.value);
                r.notes.push_back("FixedAngle " + std::to_string(c.id) + " reflected with the set");
                continue;
            }
            if (c.type != ConstraintType::Horizontal && c.type != ConstraintType::Vertical) continue;
            if (keep) continue;
            if (swap) {
                c.type = (c.type == ConstraintType::Horizontal) ? ConstraintType::Vertical
                                                                 : ConstraintType::Horizontal;
                r.notes.push_back(typeName(c.type == ConstraintType::Vertical ? ConstraintType::Horizontal
                                                                               : ConstraintType::Vertical)
                                  + " " + std::to_string(c.id) + " is now " + typeName(c.type));
                continue;
            }
            if (refLineId < 0) refLineId = addPinnedReference(2.0 * theta, "mirrored horizontal");
            const std::string was = typeName(c.type);
            c.type = (c.type == ConstraintType::Horizontal) ? ConstraintType::Parallel
                                                             : ConstraintType::Perpendicular;
            c.entityIds.push_back(refLineId);
            r.notes.push_back(was + " " + std::to_string(c.id) + " is now " + typeName(c.type)
                              + " to line " + std::to_string(refLineId));
        }
        break;
    }
    }

    // ---- post-translation: a free-move gesture is a turn plus a drag -------
    if (p.kind != GroupTransformKind::Translate && (p.delta.x != 0.0 || p.delta.y != 0.0)) {
        for (auto& e : entities) {
            if (!hobbycad::contains(memberIds, e.id) && !hobbycad::contains(r.changedEntityIds, e.id)) continue;
            for (auto& pt : e.points) pt += p.delta;
            touched(e.id);
        }
        for (auto& c : constraints) {
            if (!constraintInsideSet(c, memberIds)) continue;
            c.labelPosition += p.delta;
            c.anchorPoint += p.delta;
            touchedC(c.id);
        }
        for (auto& e : r.addedEntities) for (auto& pt : e.points) pt += p.delta;
        for (auto& e : entities)   // the reference line moved too: keep the returned copy in step
            for (auto& a : r.addedEntities) if (a.id == e.id) e = a;
    }

    r.applied = true;
    return r;
}

Point2D transformPoint(const Point2D& p, const GroupTransformParams& params, const Point2D& centerUsed)
{
    Point2D out = p;
    switch (params.kind) {
    case GroupTransformKind::Translate:
        return p + params.delta;
    case GroupTransformKind::Rotate: {
        const double rad = degreesToRadians(params.angleDeg);
        out = rotateAbout(p, centerUsed, std::cos(rad), std::sin(rad));
        break;
    }
    case GroupTransformKind::Scale:
        out = centerUsed + (p - centerUsed) * params.factor;
        break;
    case GroupTransformKind::Mirror: {
        Point2D axisPt = centerUsed;
        double theta = params.mirrorAcrossHorizontal ? 0.0 : hobbycad::Pi / 2.0;
        if (params.mirrorLineGiven) { const Point2D d = params.mirrorB - params.mirrorA; axisPt = params.mirrorA; theta = std::atan2(d.y, d.x); }
        const double ux = std::cos(theta), uy = std::sin(theta);
        const Point2D v = p - axisPt;
        const double along = v.x * ux + v.y * uy;
        out = {axisPt.x + 2.0 * along * ux - v.x, axisPt.y + 2.0 * along * uy - v.y};
        break;
    }
    }
    return out + params.delta;
}

Point2D effectivePivot(const Group& g, const std::vector<Entity>& entities)
{
    return g.hasPivot ? g.pivot : memberCenter(entities, g.entityIds);
}

CloneSetResult cloneSet(const std::vector<Entity>& entities, const std::vector<Constraint>& constraints,
                        const std::vector<int>& memberIds, int nextEntityId, int nextConstraintId)
{
    CloneSetResult out;
    auto mapped = [&](int oldId) -> int {
        for (const auto& m : out.entityIdMap) if (m.first == oldId) return m.second;
        return -1;
    };
    for (const auto& e : entities) {
        if (!hobbycad::contains(memberIds, e.id)) continue;
        Entity c = e.clone(nextEntityId++);
        c.groupId = -1;
        out.entityIdMap.push_back({e.id, c.id});
        out.entities.push_back(c);
    }
    for (auto& c : out.entities) {
        for (auto& pid : c.pathEntityIds) pid = mapped(pid);
        c.pathEntityIds.erase(std::remove(c.pathEntityIds.begin(), c.pathEntityIds.end(), -1),
                              c.pathEntityIds.end());   // drop segments outside the copied set
    }
    for (const auto& k : constraints) {
        if (!constraintInsideSet(k, memberIds)) continue;
        Constraint c = k;
        c.id = nextConstraintId++;
        for (int& eid : c.entityIds) eid = mapped(eid);
        out.constraintIdMap.push_back({k.id, c.id});
        out.constraints.push_back(c);
    }
    out.nextEntityId = nextEntityId;
    out.nextConstraintId = nextConstraintId;
    return out;
}

TransformScratch transformSet(const std::vector<Entity>& entities,
                              const std::vector<Constraint>& constraints,
                              const std::vector<int>& memberIds,
                              const GroupTransformParams& params,
                              bool createCopy, int nextEntityId, int nextConstraintId)
{
    TransformScratch sc;
    sc.entities = entities;
    sc.constraints = constraints;
    sc.targetIds = memberIds;
    if (createCopy) {
        sc.clones = cloneSet(sc.entities, sc.constraints, memberIds, nextEntityId, nextConstraintId);
        sc.entities.insert(sc.entities.end(), sc.clones.entities.begin(), sc.clones.entities.end());
        sc.constraints.insert(sc.constraints.end(), sc.clones.constraints.begin(), sc.clones.constraints.end());
        nextEntityId = sc.clones.nextEntityId;
        nextConstraintId = sc.clones.nextConstraintId;
        sc.targetIds.clear();
        for (const Entity& e : sc.clones.entities) sc.targetIds.push_back(e.id);
    }
    sc.result = transformEntities(sc.entities, sc.constraints, sc.targetIds, params,
                                  nextEntityId, nextConstraintId);
    sc.nextEntityId = nextEntityId + static_cast<int>(sc.result.addedEntities.size());
    sc.nextConstraintId = nextConstraintId + static_cast<int>(sc.result.addedConstraints.size());
    return sc;
}

Group makeCopyGroup(const Group& source, int newId, const CloneSetResult& clones,
                    const GroupTransformParams& params, const Point2D& centerUsed)
{
    Group g = source;
    g.id = newId;
    g.name = source.name + " copy";
    g.entityIds.clear(); g.constraintIds.clear(); g.childGroupIds.clear();
    g.parentGroupId = -1;
    for (const Entity& e : clones.entities) g.entityIds.push_back(e.id);
    for (const Constraint& c : clones.constraints) g.constraintIds.push_back(c.id);
    if (g.hasPivot) g.pivot = transformPoint(g.pivot, params, centerUsed);
    return g;
}

} // namespace sketch
} // namespace hobbycad
