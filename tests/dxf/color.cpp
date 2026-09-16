// =====================================================================
//  tests/dxf/color.cpp — per-entity color: import, export, round-trip
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Code 420 (24-bit true color) overrides code 62 (ACI index) on import;
//  an ACI index resolves through the fixed AutoCAD palette. On export an
//  entity's RGB is written as a 420 group. BYLAYER/BYBLOCK and absent color
//  stay the default (-1); layer-color resolution is not implemented.
#include <hobbycad/sketch/dxf_import.h>
#include <hobbycad/sketch/export.h>
#include <hobbycad/sketch/entity.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }
static std::string doc(const std::string& body) {
    return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}

int main() {
    std::printf("DXF per-entity color\n");

    { // ACI index 1 (red) -> 0xFF0000
      auto r = importDXFString(doc("0\nLINE\n8\n0\n62\n1\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(r.entities.size() == 1 && r.entities[0].color == 0xFF0000,
         "ACI 1 resolves to red (0xFF0000)"); }

    { // ACI index 5 (blue) -> 0x0000FF
      auto r = importDXFString(doc("0\nLINE\n8\n0\n62\n5\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(!r.entities.empty() && r.entities[0].color == 0x0000FF, "ACI 5 resolves to blue"); }

    { // True color 420 = 0x336699
      auto r = importDXFString(doc("0\nLINE\n8\n0\n420\n3368601\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(!r.entities.empty() && r.entities[0].color == 0x336699, "true color 420 read as RGB"); }

    { // 420 overrides 62
      auto r = importDXFString(doc("0\nLINE\n8\n0\n62\n1\n420\n255\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(!r.entities.empty() && r.entities[0].color == 0x0000FF /*255=blue*/,
         "true color (420) overrides the ACI index (62)"); }

    { // No color code -> default (-1), not forced to a palette color
      auto r = importDXFString(doc("0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(!r.entities.empty() && r.entities[0].color == -1, "absent color stays default (-1)"); }

    { // BYLAYER (256) / BYBLOCK (0) -> default (-1)
      auto r = importDXFString(doc("0\nLINE\n8\n0\n62\n256\n10\n0\n20\n0\n11\n5\n21\n0\n"));
      ck(!r.entities.empty() && r.entities[0].color == -1, "BYLAYER (256) stays default"); }

    { // Export: an entity RGB is written as a 420 group
      std::vector<Entity> es{ createLine(1, {0,0}, {5,0}) };
      es[0].color = 0x112233;
      const std::string dxf = sketchToDXF(es);
      ck(has(dxf, "\n420\n1122867\n"), "export writes true color 420 for a colored entity"); }

    { // Export: a default-color entity writes no 420
      std::vector<Entity> es{ createLine(1, {0,0}, {5,0}) };
      const std::string dxf = sketchToDXF(es);
      ck(!has(dxf, "\n420\n"), "export of a default-color entity writes no 420"); }

    { // Round-trip: export a colored entity, re-import, color preserved
      std::vector<Entity> es{ createCircle(1, {0,0}, 4) };
      es[0].color = 0x8040C0;
      const std::string dxf = sketchToDXF(es);
      auto r = importDXFString(dxf);
      ck(!r.entities.empty() && r.entities[0].color == 0x8040C0,
         "color survives an export -> import round-trip"); }

    if (fails == 0) std::printf("dxf color: ALL PASS\n");
    else            std::printf("dxf color: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
