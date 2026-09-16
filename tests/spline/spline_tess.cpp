// tests/spline/spline_tess.cpp — rational (weighted) Bezier tessellation
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/sketch/operations.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

using hobbycad::Point3;
using hobbycad::sketch::tessellateRationalSpline;
using hobbycad::sketch::tessellateSpline;

static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }

int main(){
    std::printf("rational Bezier tessellation\n");
    // one cubic segment; control polygon bows upward
    std::vector<Point3> ctrl = {{0,0,0},{0,1,0},{1,1,0},{1,0,0}};

    // all weights 1 -> identical to the non-rational Bezier
    std::vector<double> w1 = {1,1,1,1};
    auto r1 = tessellateRationalSpline(ctrl, w1, 20);
    auto nr = tessellateSpline(ctrl, 20, /*bezier=*/true);
    ck(r1.size() == nr.size(), "rational(w=1) sample count matches non-rational");
    double maxd = 0;
    for (size_t i = 0; i < std::min(r1.size(), nr.size()); ++i)
        maxd = std::max(maxd, std::hypot(r1[i].x-nr[i].x, r1[i].y-nr[i].y));
    ck(maxd < 1e-9, "rational(w=1) equals the non-rational Bezier");

    // heavier middle weights pull the curve toward the middle control points
    std::vector<double> wh = {1,5,5,1};
    auto rh = tessellateRationalSpline(ctrl, wh, 20);
    const double ymid_1 = r1[r1.size()/2].y;
    const double ymid_h = rh[rh.size()/2].y;
    std::printf("    y(mid): w=1 -> %.3f, w=5 -> %.3f\n", ymid_1, ymid_h);
    ck(ymid_h > ymid_1 + 0.05, "heavier middle weights bulge the curve upward");

    // endpoints are interpolated exactly regardless of weights
    ck(std::hypot(rh.front().x-0, rh.front().y-0) < 1e-9, "start endpoint preserved");
    ck(std::hypot(rh.back().x-1,  rh.back().y-0)  < 1e-9, "end endpoint preserved");

    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
