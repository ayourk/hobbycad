// =====================================================================
//  tests/project/plane_axes.cpp — planeAxisLabels matches planeBasisFor
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The panel/viewport need to know which axis each field/color is for, per
//  plane. These labels MUST track planeBasisFor's u/v/normal or the panel
//  would mislabel a YZ sketch. Fails meaningfully if they drift apart.
#include <hobbycad/project.h>
#include <cstring>
#include <cstdio>
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w);
    if (!ok) ++fails;
}
static bool eq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
int main() {
    std::printf("planeAxisLabels\n");
    auto xy = planeAxisLabels(SketchPlane::XY);
    ck(eq(xy.u,"X") && eq(xy.v,"Y") && eq(xy.normal,"Z"), "XY -> u=X, v=Y, normal=Z");
    auto xz = planeAxisLabels(SketchPlane::XZ);
    ck(eq(xz.u,"X") && eq(xz.v,"Z") && eq(xz.normal,"Y"), "XZ -> u=X, v=Z, normal=Y");
    auto yz = planeAxisLabels(SketchPlane::YZ);
    ck(eq(yz.u,"Y") && eq(yz.v,"Z") && eq(yz.normal,"X"), "YZ -> u=Y, v=Z, normal=X (hidden in 2D)");
    auto c = planeAxisLabels(SketchPlane::Custom);
    ck(eq(c.u,"U") && eq(c.v,"V") && eq(c.normal,"N"), "Custom -> U, V, N");
    if (fails == 0) std::printf("plane_axes: ALL PASS\n");
    else            std::printf("plane_axes: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
