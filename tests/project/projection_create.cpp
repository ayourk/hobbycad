// =====================================================================
//  tests/project/projection_create.cpp
//  makeProjectionChild: build a projected child from a source entity.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Fails meaningfully if the creation core regresses: the child must carry
//  the source link, drop its own construction/centerline flags, take the new
//  id, project the points through both planes, and REFUSE unprojectable types.
#include <hobbycad/project.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/entity.h>
#include <cmath>
#include <cstdio>

using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

int main() {
    std::printf("makeProjectionChild\n");
    const PlaneBasis xy = planeBasisFor(SketchPlane::XY);
    const PlaneBasis xz = planeBasisFor(SketchPlane::XZ);

    // ---- a line on XY, projected onto XZ --------------------------------
    {
        sketch::Entity src;
        src.id = 5; src.type = sketch::EntityType::Line;
        src.points = { {0, 0}, {10, 0} };
        src.isConstruction = true;         // must NOT carry over

        sketch::Entity child;
        const bool ok = sketch::makeProjectionChild(child, src, /*sourceSketchId*/3,
                                                    /*newId*/42, xy, xz);
        check(ok, "line projects (returns true)");
        check(child.id == 42, "child takes the new id");
        check(child.projectionSourceId == 5, "child links to source entity id");
        check(child.projectionSourceSketchId == 3, "child links to source sketch id");
        check(child.type == sketch::EntityType::Line, "child keeps the line type");
        check(!child.isConstruction, "child is NOT construction (reset)");
        check(child.points.size() == 2, "child has two points");
        // XY->XZ: a line along X keeps its X extent (v = z = 0).
        check(near(child.points[0].x, 0) && near(child.points[1].x, 10),
              "X extent preserved onto XZ");
        check(near(child.points[0].y, 0) && near(child.points[1].y, 0),
              "out-of-plane collapses to 0");
    }

    // ---- a line ALONG Y on XY collapses to a point on XZ ---------------
    {
        sketch::Entity src;
        src.id = 7; src.type = sketch::EntityType::Line;
        src.points = { {0, 0}, {0, 5} };   // along Y
        sketch::Entity child;
        const bool ok = sketch::makeProjectionChild(child, src, 1, 99, xy, xz);
        check(ok, "Y-line projects");
        check(near(child.points[0].x, 0) && near(child.points[1].x, 0)
              && near(child.points[0].y, 0) && near(child.points[1].y, 0),
              "a Y-line collapses onto XZ (Y is out of plane)");
    }

    // ---- a circle projects to an ellipse -------------------------------
    {
        sketch::Entity src;
        src.id = 9; src.type = sketch::EntityType::Circle;
        src.points = { {0, 0} }; src.radius = 5;
        sketch::Entity child;
        const bool ok = sketch::makeProjectionChild(child, src, 1, 100, xy, xz);
        check(ok && child.type == sketch::EntityType::Ellipse
                  && child.majorRadius > 0.0,
              "circle projects to an ellipse");
    }

    if (failures == 0) std::printf("projection_create: ALL PASS\n");
    else               std::printf("projection_create: %d FAILURE(S)\n", failures);
    return failures ? 1 : 0;
}
