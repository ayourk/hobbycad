// tests/project/spline.cpp — Catmull-Rom spline tessellation.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/sketch/operations.h>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }
static bool near(double a, double b){ return std::fabs(a-b) < 1e-6; }
int main() {
    std::printf("tessellateSpline\n");
    // Fewer than 3 control points: returned unchanged.
    {
        std::vector<Point3> c = {{0,0,0},{10,0,0}};
        auto t = sketch::tessellateSpline(c, 8);
        ck(t.size() == 2, "2 control points -> unchanged (a line)");
    }
    // 3 collinear: stays collinear, passes through control points, count is right.
    {
        std::vector<Point3> c = {{0,0,0},{5,0,0},{10,0,0}};
        auto t = sketch::tessellateSpline(c, 4);
        ck((int)t.size() == (3-1)*4 + 1, "count == (n-1)*segs + 1");
        ck(near(t.front().x,0) && near(t.back().x,10), "endpoints preserved exactly");
        bool onLine = true; for (auto& p : t) if (std::fabs(p.y) > 1e-9) onLine = false;
        ck(onLine, "collinear control points stay collinear");
        bool hitMid = false; for (auto& p : t) if (near(p.x,5) && near(p.y,0)) hitMid = true;
        ck(hitMid, "passes through the middle control point");
    }
    // Curved: passes through the peak and has smooth intermediate samples.
    {
        std::vector<Point3> c = {{0,0,0},{5,10,0},{10,0,0}};
        auto t = sketch::tessellateSpline(c, 8);
        bool hitPeak = false; for (auto& p : t) if (near(p.x,5) && near(p.y,10)) hitPeak = true;
        ck(hitPeak, "passes through the peak control point");
        bool curved = false;
        for (auto& p : t) if (p.y > 0.1 && p.y < 9.9 && p.x > 0.1 && p.x < 9.9) curved = true;
        ck(curved, "has smooth intermediate samples off the control points");
    }
    if (fails==0) std::printf("spline: ALL PASS\n"); else std::printf("spline: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
