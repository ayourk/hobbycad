// =====================================================================
//  src/libhobbycad/hobbycad/brep/fit.h — least-squares curve fitting
// =====================================================================
//
//  Fit a smooth B-spline to an ordered 2D point set (a sliced mesh
//  cross-section boundary), within a tolerance. The foundation of the
//  HobbyMesh STL -> STEP path: slice -> fitBSpline2D per section ->
//  loft -> STEP. Uses OCCT's Geom2dAPI_PointsToBSpline.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================

#ifndef HOBBYCAD_BREP_FIT_H
#define HOBBYCAD_BREP_FIT_H

#include "../core.h"
#include "../types.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace brep {

/// A B-spline fitted to a 2D point set. controlPoints are the poles; weights
/// are 1.0 each for a non-rational fit; knots is the flat knot vector (repeated
/// by multiplicity). maxError is the achieved max deviation of the input points
/// from the fitted curve (mm).
struct BSplineFit2D {
    bool success = false;
    int degree = 3;
    std::vector<Point2D> controlPoints;
    std::vector<double> weights;
    std::vector<double> knots;
    double maxError = 0.0;
    std::string error;
};

/// Least-squares fit an ordered 2D point sequence to a cubic B-spline within
/// `tolerance` (max allowed deviation, mm; ~0.1 default for 3D-print-space
/// STL->STEP, tighter for metal CAD). Points must be ordered along the curve.
/// A tighter tolerance yields more control points. Returns success=false with
/// an error message on bad input or a fit that does not converge.
HOBBYCAD_EXPORT BSplineFit2D fitBSpline2D(const std::vector<Point2D>& points,
                                          double tolerance = 0.1);

}  // namespace brep
}  // namespace hobbycad

#endif  // HOBBYCAD_BREP_FIT_H
