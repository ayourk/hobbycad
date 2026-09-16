// =====================================================================
//  tests/drawconstrain/snapmap.cpp — which constraint a snap implies
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Draw-then-constrain turns the snap used to place a point into a real
//  constraint. Getting this mapping wrong is quiet: the sketch still
//  looks right, and is simply not constrained, so it is pinned here.
//
//  The two "none" groups are different refusals and both matter:
//  Nearest/Quadrant/Intersection are positions ON a curve rather than a
//  point two entities can be tied by, and Origin/AxisX/AxisY have no
//  entity to reference at all.
//
// =====================================================================

#include <hobbycad/sketch/snap.h>

#include <cstdio>

using hobbycad::sketch::SnapType;
using hobbycad::sketch::ConstraintType;
using hobbycad::sketch::constraintForSnap;

static int failures = 0;

using hobbycad::sketch::EntityType;

static void wants(SnapType t, EntityType target, ConstraintType want,
                  const char* name)
{
    const auto got = constraintForSnap(t, target);
    if (!got || *got != want) {
        std::printf("  FAIL  %s: expected a constraint, got %s\n",
                    name, got ? "the wrong one" : "none");
        ++failures;
    }
}

static void wantsNone(SnapType t, EntityType target, const char* name)
{
    if (constraintForSnap(t, target).has_value()) {
        std::printf("  FAIL  %s: implies a constraint, should imply none\n", name);
        ++failures;
    }
}

int main()
{
    // Point to point, whatever it was snapped to.
    wants(SnapType::Endpoint,     EntityType::Line,   ConstraintType::Coincident, "Endpoint");
    wants(SnapType::Point,        EntityType::Line,   ConstraintType::Coincident, "Point");
    wants(SnapType::Center,       EntityType::Circle, ConstraintType::Coincident, "Center");
    wants(SnapType::ArcEndCenter, EntityType::Slot,   ConstraintType::Coincident, "ArcEndCenter");
    wants(SnapType::Midpoint,     EntityType::Line,   ConstraintType::Midpoint,   "Midpoint");

    // Point ON a curve: a different constraint per curve, and none where
    // the solver has no point-on-curve to offer.
    wants(SnapType::Nearest, EntityType::Line,   ConstraintType::PointOnLine,   "Nearest on line");
    wants(SnapType::Nearest, EntityType::Circle, ConstraintType::PointOnCircle, "Nearest on circle");
    wants(SnapType::Nearest, EntityType::Arc,    ConstraintType::PointOnCircle, "Nearest on arc");
    wantsNone(SnapType::Nearest, EntityType::Spline,  "Nearest on spline");
    wantsNone(SnapType::Nearest, EntityType::Ellipse, "Nearest on ellipse");

    // Derived positions neither entity owns as a point.
    wantsNone(SnapType::Quadrant,     EntityType::Circle, "Quadrant");
    wantsNone(SnapType::Intersection, EntityType::Line,   "Intersection");

    // No entity to reference: these carry entityId -1.
    wantsNone(SnapType::Origin, EntityType::Line, "Origin");
    wantsNone(SnapType::AxisX,  EntityType::Line, "AxisX");
    wantsNone(SnapType::AxisY,  EntityType::Line, "AxisY");

    if (failures == 0) {
        std::printf("  snap mapping: ok\n");
        return 0;
    }
    std::printf("  snap mapping: %d failure(s)\n", failures);
    return 1;
}
