// =====================================================================
//  src/libhobbycad/sketch/view.cpp — where a sketch sits on screen
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/view.h>

#include <hobbycad/units.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

Point2D SketchView::toScreen(const Point2D& p) const
{
    double x = (p.x - center.x) * zoom;
    const double y = (p.y - center.y) * zoom;
    if (flipped) x = -x;   // the far side: u mirrored
    const double r = degreesToRadians(rotationDeg);
    const double rx = x * std::cos(r) - y * std::sin(r);
    const double ry = x * std::sin(r) + y * std::cos(r);
    return {rx + width / 2.0, -ry + height / 2.0};   // screen y points down
}

Point2D SketchView::toSketch(const Point2D& s) const
{
    const double x = s.x - width / 2.0;
    const double y = -(s.y - height / 2.0);
    const double r = degreesToRadians(-rotationDeg);
    double rx = x * std::cos(r) - y * std::sin(r);
    const double ry = x * std::sin(r) + y * std::cos(r);
    if (flipped) rx = -rx;
    return {rx / zoom + center.x, ry / zoom + center.y};
}

std::array<Point2D, 4> SketchView::sketchCorners(const Point2D& a, const Point2D& b) const
{
    return {toSketch({a.x, a.y}), toSketch({b.x, a.y}), toSketch({b.x, b.y}),
            toSketch({a.x, b.y})};
}

bool SketchView::axisAligned() const
{
    const double quarters = rotationDeg / 90.0;
    return std::fabs(quarters - std::round(quarters)) < 1e-9;
}

}  // namespace sketch
}  // namespace hobbycad
