// =====================================================================
//  src/hobbycad/gui/entityrenderer.h — sketch entity rendering
// =====================================================================
//
//  Draws committed sketch geometry: each entity (the per-type switch in
//  drawEntity), its selection handles, and the unconstrained-endpoint
//  dots. Extracted from SketchCanvas so the canvas no longer owns this
//  rendering pass. It holds a back-reference to its canvas for the view
//  transform and the model/display state it reads (all read-only) and is a
//  friend of it, mirroring ConstraintRenderer and SnapEngine.
//
//  Tool-in-progress previews, the grid, and transform / background-image
//  overlays deliberately stay on the canvas: those render live interaction
//  state, not committed geometry.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_ENTITYRENDERER_H
#define HOBBYCAD_GUI_ENTITYRENDERER_H

class QPainter;

namespace hobbycad {

class SketchCanvas;
struct SketchEntity;

/// Renders committed sketch entities for one SketchCanvas.
class EntityRenderer {
public:
    explicit EntityRenderer(SketchCanvas& canvas) : m_canvas(canvas) {}

    /// Draw one committed entity (dispatches on entity type).
    void drawEntity(QPainter& painter, const SketchEntity& entity);
    /// Draw the selection handles for one selected entity.
    void drawSelectionHandles(QPainter& painter, const SketchEntity& entity);
    /// Mark the endpoints the solver reports as still free (heuristic fallback).
    void drawUnconstrainedPoints(QPainter& painter);
    /// Curvature comb on Bezier splines (spine length proportional to |kappa|,
    /// signed so it flips side at inflections). Gated by the canvas toggle.
    void drawCurvatureComb(QPainter& painter);

private:
    bool isPointConstrained(int entityId, int pointIndex) const;

    SketchCanvas& m_canvas;   ///< non-owning back-reference
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_ENTITYRENDERER_H
