// =====================================================================
//  src/libhobbycad/brep/fit.cpp — least-squares 2D B-spline fitting
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/brep/fit.h>

#include <algorithm>

#include <gp_Pnt2d.hxx>
#include <NCollection_Array1.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dAPI_PointsToBSpline.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <GeomAbs_Shape.hxx>
#include <Standard_Failure.hxx>
#include "hobbycad/occt_failure.h"

namespace hobbycad {
namespace brep {

BSplineFit2D fitBSpline2D(const std::vector<Point2D>& points, double tolerance)
{
    BSplineFit2D r;
    if (points.size() < 2) {
        r.error = "fitBSpline2D needs at least two points";
        return r;
    }
    if (!(tolerance > 0.0)) {
        r.error = "fitBSpline2D tolerance must be > 0";
        return r;
    }
    try {
        NCollection_Array1<gp_Pnt2d> pts(1, static_cast<int>(points.size()));
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
            pts.SetValue(i + 1, gp_Pnt2d(points[i].x, points[i].y));

        // Cubic-to-degree-8 least-squares fit, C2, within tolerance. OCCT adds
        // knots as needed to stay inside the tolerance; a tighter tolerance ->
        // more control points.
        Geom2dAPI_PointsToBSpline builder(pts, 3, 8, GeomAbs_C2, tolerance);
        if (!builder.IsDone()) {
            r.error = "B-spline fit did not converge";
            return r;
        }
        Handle(Geom2d_BSplineCurve) c = builder.Curve();
        if (c.IsNull()) {
            r.error = "B-spline fit produced a null curve";
            return r;
        }

        r.degree = c->Degree();
        const int np = c->NbPoles();
        r.controlPoints.reserve(np);
        r.weights.reserve(np);
        for (int i = 1; i <= np; ++i) {
            const gp_Pnt2d p = c->Pole(i);
            r.controlPoints.push_back({p.X(), p.Y()});
            r.weights.push_back(c->Weight(i));
        }

        const int nk = c->NbKnots();
        const auto& kn = c->Knots();            // const-ref accessors (no copy)
        const auto& mult = c->Multiplicities();
        for (int i = 1; i <= nk; ++i)
            for (int m = 0; m < mult(i); ++m)
                r.knots.push_back(kn(i));

        // Achieved fidelity: the largest distance from an input point to the curve.
        double maxE = 0.0;
        for (const Point2D& q : points) {
            Geom2dAPI_ProjectPointOnCurve proj(gp_Pnt2d(q.x, q.y), c);
            if (proj.NbPoints() > 0)
                maxE = std::max(maxE, static_cast<double>(proj.LowerDistance()));
        }
        r.maxError = maxE;
        r.success = true;
    } catch (const Standard_Failure& e) {
        r.error = std::string("OCCT: ") + occtFailureMessage(e);
    }
    return r;
}

}  // namespace brep
}  // namespace hobbycad
