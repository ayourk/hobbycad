// =====================================================================
//  src/hobbycad/gui/placementpainter.h — drawing a placement preview
// =====================================================================
//
//  Paints the shapes sketch::placementPreview() describes, in the canvas's
//  preview style, and hands the stage's live values to its dimension
//  fields. Nothing about the geometry is decided here.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_PLACEMENTPAINTER_H
#define HOBBYCAD_GUI_PLACEMENTPAINTER_H

#include <hobbycad/sketch/placement.h>

class QPainter;

namespace hobbycad {

class SketchCanvas;

/// Paint `preview` with the painter's current pen as the preview pen.
void paintPlacementPreview(SketchCanvas& canvas, QPainter& painter,
                           const sketch::PlacementPreview& preview);

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_PLACEMENTPAINTER_H
