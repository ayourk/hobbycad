// =====================================================================
//  tests/fit/fit.cpp — 2D B-spline least-squares fit
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The STL->STEP cross-section-fit foundation: fit a smooth B-spline to a
//  sampled boundary within a tolerance, with fewer control points than data.
#include <hobbycad/brep/fit.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::brep;
static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }

int main() {
    std::printf("2D B-spline least-squares fit\n");
    // 41 points sampled along a semicircle, radius 10, angle 0..pi
    std::vector<Point2D> pts;
    const int N = 40; const double R = 10.0;
    for (int i = 0; i <= N; ++i) { double a = M_PI * i / N; pts.push_back({R*std::cos(a), R*std::sin(a)}); }

    {
        auto f = fitBSpline2D(pts, 0.1);
        std::printf("    >> tol 0.1: success=%d maxErr=%.5f poles=%zu degree=%d\n",
                    (int)f.success, f.maxError, f.controlPoints.size(), f.degree);
        ck(f.success, "fit succeeds on a sampled semicircle");
        ck(f.success && f.maxError <= 0.1, "max error within the 0.1 tolerance");
        ck(f.controlPoints.size() >= 4 && f.controlPoints.size() < pts.size(),
           "fewer control points than data points, but a real polygon");
        ck(f.degree >= 3, "cubic or higher");
        ck(f.weights.size() == f.controlPoints.size(), "a weight per pole");
    }
    {
        auto loose = fitBSpline2D(pts, 0.2);
        auto tight = fitBSpline2D(pts, 0.01);
        std::printf("    >> loose 0.2 poles=%zu err=%.5f ; tight 0.01 poles=%zu err=%.5f\n",
                    loose.controlPoints.size(), loose.maxError,
                    tight.controlPoints.size(), tight.maxError);
        ck(loose.success && tight.success, "both tolerances fit");
        ck(tight.maxError <= 0.01, "tight fit is within 0.01");
        ck(tight.controlPoints.size() >= loose.controlPoints.size(),
           "tighter tolerance uses at least as many control points");
    }
    {
        std::vector<Point2D> one{{0,0}};
        ck(!fitBSpline2D(one, 0.1).success, "a single point is rejected");
        ck(!fitBSpline2D(pts, 0.0).success, "a non-positive tolerance is rejected");
    }
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
