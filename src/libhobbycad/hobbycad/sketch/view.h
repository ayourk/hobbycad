// =====================================================================
//  src/libhobbycad/hobbycad/sketch/view.h — where a sketch sits on screen
// =====================================================================
//
//  Capability tier of the front-end support layer. The mapping between
//  sketch coordinates (y up) and a view's pixels (y down): the sketch point
//  at the middle of the view, the scale, the view's rotation, and whether
//  the sketch is seen from its far side. Pick tolerances, window selection
//  and drag floors are all given in pixels and read through this.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_VIEW_H
#define HOBBYCAD_SKETCH_VIEW_H

#include "../core.h"
#include "../types.h"

#include <array>

namespace hobbycad {
namespace sketch {

/// How a sketch is shown in a view.
struct HOBBYCAD_EXPORT SketchView {
    Point2D center;            ///< the sketch point at the middle of the view
    double zoom = 1.0;         ///< pixels per sketch unit
    double rotationDeg = 0.0;  ///< how far the sketch is turned on screen, counter-clockwise
    bool flipped = false;      ///< seen from the far side: the sketch's u is mirrored
    double width = 0.0;        ///< view size in pixels
    double height = 0.0;

    /// Pixels (x right, y down) for a sketch point.
    Point2D toScreen(const Point2D& sketchPoint) const;
    /// The sketch point under a pixel.
    Point2D toSketch(const Point2D& screenPoint) const;
    /// A pixel distance in sketch units.
    double sketchLength(double pixels) const { return pixels / zoom; }

    /// The sketch-space corners of a screen rectangle, in order round it.
    std::array<Point2D, 4> sketchCorners(const Point2D& screenA, const Point2D& screenB) const;
    /// True when screen rectangles are axis-aligned in the sketch too (the
    /// rotation is a multiple of a quarter turn).
    bool axisAligned() const;
};

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_VIEW_H
