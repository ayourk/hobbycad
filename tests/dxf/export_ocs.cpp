// tests/dxf/export_ocs.cpp — DXF export tags the plane normal as OCS extrusion
// SPDX-License-Identifier: GPL-3.0-only
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
int main() {
    std::printf("DXF export OCS extrusion\n");
    std::vector<Entity> es{ createLine(1, {2, 3}, {5, 3}) };
    { DXFExportOptions o;                                  // default +Z: flat, no extrusion
      const std::string dxf = sketchToDXF(es, o);
      ck(!has(dxf, "\n210\n"), "default (XY) export writes no extrusion; files stay flat/clean"); }
    { DXFExportOptions o; o.extrusion = Vec3(0, -1, 0);    // XZ plane normal
      const std::string dxf = sketchToDXF(es, o);
      ck(has(dxf, "210\n0\n") && has(dxf, "220\n-1\n") && has(dxf, "230\n0\n"),
         "XZ-normal export writes extrusion 210/220/230 = (0,-1,0)");
      ck(has(dxf, "\nLINE\n"), "and the entity is still written"); }
    if (fails == 0) std::printf("dxf export_ocs: ALL PASS\n");
    else            std::printf("dxf export_ocs: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
