// =====================================================================
//  tests/dxf/underspec.cpp — where the DXF spec is under-specified
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  These are the ambiguous corners of the format: OCS/extrusion (the classic
//  negative-Z mirror), silent units, arc winding wrap, and constructs our
//  model can only partly represent. Each is either handled correctly or
//  surfaced as a warning, never silently wrong.
#include <hobbycad/sketch/dxf_import.h>
#include <cstdio>
#include <cmath>
#include <string>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 1e-4) { return std::fabs(a - b) < e; }
static bool hasWarn(const DXFImportResult& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true;
    return false;
}
static std::string doc(const std::string& body) {
    return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}
static std::string withHeader(const std::string& hdr, const std::string& body) {
    return "0\nSECTION\n2\nHEADER\n" + hdr + "0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}

int main() {
    std::printf("DXF under-specified corners\n");

    { // OCS extrusion (0,0,1): identity
      auto r = importDXFString(doc("0\nLINE\n8\n0\n10\n2\n20\n3\n11\n2\n21\n3\n"
                                   "210\n0\n220\n0\n230\n1\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, 2), "extrusion +Z is identity"); }

    { // OCS extrusion (0,0,-1): classic X mirror via Arbitrary Axis Algorithm
      auto r = importDXFString(doc("0\nLINE\n8\n0\n10\n2\n20\n3\n11\n6\n21\n3\n"
                                   "210\n0\n220\n0\n230\n-1\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, -2)
         && near(r.entities[0].points[0].y, 3), "extrusion -Z mirrors X (AAA)"); }

    { // Units: $INSUNITS = 1 (inches) -> millimeters, auto
      auto r = importDXFString(withHeader("9\n$INSUNITS\n70\n1\n",
                                          "0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(near(r.entities[0].points[0].x, 25.4) && near(r.entities[0].points[1].x, 50.8),
         "$INSUNITS inches -> mm"); }

    { // Units: absent -> assume mm, but warn about the gap
      auto r = importDXFString(doc("0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(near(r.entities[0].points[0].x, 1) && hasWarn(r, "$INSUNITS"),
         "no units -> mm assumed + warned"); }

    { // Units: unitless ($INSUNITS = 0) -> warn
      auto r = importDXFString(withHeader("9\n$INSUNITS\n70\n0\n",
                                          "0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(hasWarn(r, "unitless"), "$INSUNITS=0 -> unitless warning"); }

    { // ARC with end angle < start angle wraps CCW through 0
      auto r = importDXFString(doc("0\nARC\n8\n0\n10\n0\n20\n0\n40\n5\n50\n350\n51\n10\n"));
      ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Arc && near(r.entities[0].radius, 5),
         "ARC end<start wraps (still a valid 20deg arc)"); }

    { // Rotated ellipse: major axis at 45 deg -> rotation preserved, no warning
      auto r = importDXFString(doc("0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n3\n21\n3\n40\n0.5\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].ellipseRotation, 45.0)
             && !hasWarn(r, "rotated ELLIPSE"),
         "rotated ellipse keeps its major-axis angle (no longer flattened)"); }

    { // Partial ellipse: start/end params (0 .. pi/2) are read as the arc range
      auto r = importDXFString(doc("0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n40\n0.5\n"
                                   "41\n0\n42\n1.5707963\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].ellipseSweep, 90.0, 0.1)
             && !hasWarn(r, "arc range"),
         "partial ellipse keeps its arc range (sweep 90, no warning)"); }

    { // INSERT of a block with no BLOCKS definition: name recorded, warned undefined
      auto r = importDXFString(doc("0\nINSERT\n8\n0\n2\nMYBLOCK\n10\n0\n20\n0\n"));
      ck(r.blocks.size() == 1 && r.blocks[0] == "MYBLOCK" && hasWarn(r, "undefined block"),
         "INSERT of an undefined block records the name + warns"); }

    { // Non-planar extrusion (tilted): flattened with a warning
      auto r = importDXFString(doc("0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n5\n"
                                   "210\n1\n220\n0\n230\n0\n"));
      ck(hasWarn(r, "non-XY plane"), "tilted extrusion -> flattened + warned"); }

    { // Construction-layer filter
      DXFImportOptions o; o.ignoreConstructionLayers = true;
      auto r = importDXFString(doc("0\nLINE\n8\nCONSTRUCTION\n10\n0\n20\n0\n11\n1\n21\n1\n"
                                   "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n2\n21\n2\n"), 1, o);
      ck(r.entities.size() == 1, "construction layer skipped when requested"); }

    { // ELLIPSE is a WCS entity: extrusion is only the normal, NOT an OCS.
      // A -Z extrusion must NOT mirror the center (unlike LINE/ARC).
      auto r = importDXFString(doc("0\nELLIPSE\n8\n0\n10\n2\n20\n3\n11\n4\n21\n0\n40\n0.5\n"
                                   "210\n0\n220\n0\n230\n-1\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, 2)
         && near(r.entities[0].points[0].y, 3), "ELLIPSE is WCS: -Z extrusion does NOT mirror center"); }

    { // SPLINE is also WCS: control points not run through the OCS transform.
      auto r = importDXFString(doc("0\nSPLINE\n8\n0\n71\n3\n210\n0\n220\n0\n230\n-1\n"
                                   "10\n5\n20\n0\n10\n6\n20\n1\n10\n7\n20\n0\n10\n8\n20\n1\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, 5),
         "SPLINE is WCS: -Z extrusion does NOT mirror control points"); }

    { // Decimeter units: $INSUNITS=14 -> 100 mm per unit (was a bug: 1e-4)
      auto r = importDXFString(withHeader("9\n$INSUNITS\n70\n14\n",
                                          "0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(near(r.entities[0].points[0].x, 100) && near(r.entities[0].points[1].x, 200),
         "$INSUNITS=14 decimeters -> 100 mm/unit"); }

    { // $MEASUREMENT fallback when $INSUNITS absent: 0 = imperial -> inches
      auto r = importDXFString(withHeader("9\n$MEASUREMENT\n70\n0\n",
                                          "0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(near(r.entities[0].points[0].x, 25.4) && hasWarn(r, "$MEASUREMENT=0"),
         "no $INSUNITS, $MEASUREMENT=0 -> inches"); }

    { // $MEASUREMENT=1 metric -> millimeters
      auto r = importDXFString(withHeader("9\n$MEASUREMENT\n70\n1\n",
                                          "0\nLINE\n8\n0\n10\n1\n20\n0\n11\n2\n21\n0\n"));
      ck(near(r.entities[0].points[0].x, 1) && hasWarn(r, "$MEASUREMENT=1"),
         "no $INSUNITS, $MEASUREMENT=1 -> millimeters"); }

    if (fails == 0) std::printf("dxf underspec: ALL PASS\n");
    else            std::printf("dxf underspec: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
