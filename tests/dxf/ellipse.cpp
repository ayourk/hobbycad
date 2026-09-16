// =====================================================================
//  tests/dxf/ellipse.cpp — oriented ellipse: import angle, export round-trip
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The ellipse model now carries a major-axis rotation. DXF import reads the
//  angle from the major-axis endpoint (no longer flattened to axis-aligned),
//  export writes the rotated endpoint back, and the rotation survives the
//  round trip. The bounding box accounts for the rotation.
#include <hobbycad/sketch/dxf_import.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/entity.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 1e-2) { return std::fabs(a - b) < e; }
static std::string doc(const std::string& body) {
    return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}

int main() {
    std::printf("DXF oriented ellipse\n");

    { // A 45-degree major axis (endpoint at cos45,sin45), ratio 0.5
      auto r = importDXFString(doc(
          "0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n0.70710678\n21\n0.70710678\n40\n0.5\n"));
      ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Ellipse,
         "ELLIPSE imports");
      ck(!r.entities.empty() && near(r.entities[0].ellipseRotation, 45.0),
         "major-axis angle read as 45 degrees (not flattened)");
      ck(!r.entities.empty() && near(r.entities[0].majorRadius, 1.0) &&
         near(r.entities[0].minorRadius, 0.5),
         "major/minor radii correct from the rotated axis"); }

    { // An axis-aligned ellipse has zero rotation
      auto r = importDXFString(doc(
          "0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n3\n21\n0\n40\n0.5\n"));
      ck(!r.entities.empty() && near(r.entities[0].ellipseRotation, 0.0),
         "axis-aligned ellipse has zero rotation"); }

    { // Export -> import round-trip preserves the rotation
      std::vector<Entity> es{ createEllipse(1, {0, 0}, 2.0, 1.0, 30.0) };
      const std::string dxf = sketchToDXF(es);
      auto r = importDXFString(dxf);
      ck(r.entities.size() == 1 && near(r.entities[0].ellipseRotation, 30.0) &&
         near(r.entities[0].majorRadius, 2.0) && near(r.entities[0].minorRadius, 1.0),
         "ellipse rotation + radii survive an export -> import round-trip"); }

    { // Bounding box accounts for the rotation: major axis turned to vertical
      Entity e = createEllipse(1, {0, 0}, 10.0, 2.0, 90.0);
      const auto bb = e.boundingBox();
      const double w = bb.maxX - bb.minX, h = bb.maxY - bb.minY;
      ck(near(w, 4.0) && near(h, 20.0),
         "rotated bounding box: a 90-degree ellipse is tall, not wide"); }


    { // Partial ellipse: 41/42 give a half sweep (0 .. pi)
      auto r = importDXFString(doc(
          "0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n40\n0.5\n41\n0\n42\n3.14159265\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].ellipseStart, 0.0)
             && near(r.entities[0].ellipseSweep, 180.0, 0.1),
         "partial ELLIPSE reads start=0, sweep=180 (no longer dropped)");
      bool warned=false; for (auto& w : r.warnings) if (w.find("arc range")!=std::string::npos) warned=true;
      ck(!warned, "no 'arc range dropped' warning for a partial ellipse"); }

    { // Full ellipse: sweep 360
      auto r = importDXFString(doc(
          "0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n40\n0.5\n41\n0\n42\n6.28318530\n"));
      ck(!r.entities.empty() && near(r.entities[0].ellipseSweep, 360.0, 0.1),
         "full ELLIPSE reads sweep=360"); }

    { // Export -> import round-trip preserves the arc range
      std::vector<Entity> es{ createEllipse(1, {0,0}, 5.0, 2.0, 0.0) };
      es[0].ellipseStart = 30.0; es[0].ellipseSweep = 200.0;
      const std::string dxf = sketchToDXF(es);
      auto r = importDXFString(dxf);
      ck(!r.entities.empty() && near(r.entities[0].ellipseStart, 30.0, 0.1)
             && near(r.entities[0].ellipseSweep, 200.0, 0.1),
         "elliptical-arc range survives export -> import"); }

    if (fails == 0) std::printf("dxf ellipse: ALL PASS\n");
    else            std::printf("dxf ellipse: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
