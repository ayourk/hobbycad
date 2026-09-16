// =====================================================================
//  tests/project/projection_ellipse.cpp — the conic family under angled
//  projection: circle->ellipse, ellipse->circle, arc->elliptical-arc.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/types.h>
#include <cstdio>
#include <cmath>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 1e-3) { return std::fabs(a - b) < e; }

int main() {
    std::printf("conic family under angled projection\n");
    const double c60 = std::cos(60.0 * M_PI / 180.0), s60 = std::sin(60.0 * M_PI / 180.0);
    PlaneBasis xy;  // origin 0, u=+X, v=+Y, n=+Z (defaults)

    Entity circle = createCircle(1, {0, 0}, 10.0);

    { // Parallel target: stays a circle.
      Entity child; child.type = EntityType::Line;
      bool ok = updateProjectionFromSource(child, circle, xy, xy);
      ck(ok && child.type == EntityType::Circle && near(child.radius, 10.0),
         "circle onto a parallel plane stays a circle (r=10)"); }

    { // Circle onto a 60-deg tilt about X: ellipse major=10, minor=5, rot~0.
      PlaneBasis tilt;
      tilt.uAxis  = Vec3(1.0f, 0.0f, 0.0f);
      tilt.vAxis  = Vec3(0.0f, (float)c60, (float)s60);
      tilt.normal = Vec3(0.0f, (float)-s60, (float)c60);
      Entity child; child.type = EntityType::Line;
      bool ok = updateProjectionFromSource(child, circle, xy, tilt);
      ck(ok && child.type == EntityType::Ellipse && near(child.majorRadius, 10.0)
             && near(child.minorRadius, 10.0 * c60),
         "circle -> ellipse (major 10, minor r*cos60)"); }

    { // Ellipse (major=10 X, minor=5 Y) onto a 60-deg tilt about Y: X
      // foreshortens by cos60 -> major'=5=minor -> a CIRCLE.
      PlaneBasis tiltY;
      tiltY.uAxis  = Vec3((float)c60, 0.0f, (float)s60);
      tiltY.vAxis  = Vec3(0.0f, 1.0f, 0.0f);
      tiltY.normal = Vec3((float)-s60, 0.0f, (float)c60);
      Entity el = createEllipse(2, {0, 0}, 10.0, 5.0, 0.0);
      Entity child; child.type = EntityType::Line;
      bool ok = updateProjectionFromSource(child, el, xy, tiltY);
      ck(ok && child.type == EntityType::Circle && near(child.radius, 5.0),
         "an ellipse can project to a circle (foreshortening undone, r=5)"); }

    { // Arc (quarter circle r=10, 0..90) onto a 60-deg tilt about X ->
      // elliptical arc: major=10, minor=5, start~0, sweep~90.
      PlaneBasis tilt;
      tilt.uAxis  = Vec3(1.0f, 0.0f, 0.0f);
      tilt.vAxis  = Vec3(0.0f, (float)c60, (float)s60);
      tilt.normal = Vec3(0.0f, (float)-s60, (float)c60);
      Entity arc = createArc(3, {0, 0}, 10.0, 0.0, 90.0);
      Entity child; child.type = EntityType::Line;
      bool ok = updateProjectionFromSource(child, arc, xy, tilt);
      ck(ok && child.type == EntityType::Ellipse && near(child.majorRadius, 10.0)
             && near(child.minorRadius, 5.0),
         "arc -> elliptical arc (major 10, minor 5)");
      ck(near(child.ellipseStart, 0.0, 0.5) && near(child.ellipseSweep, 90.0, 0.5),
         "the arc's param range maps (start~0, sweep~90)"); }

    if (fails == 0) std::printf("projection_ellipse: ALL PASS\n");
    else            std::printf("projection_ellipse: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
