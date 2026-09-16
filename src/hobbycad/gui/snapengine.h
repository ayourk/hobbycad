// =====================================================================
//  src/hobbycad/gui/snapengine.h — GUI snapping & inference service
// =====================================================================
//
//  Snapping and geometric inference are strictly a GUI/interaction
//  concern: the geometry kernel and the constraint model know nothing
//  about them. This is the interaction-layer service that sits between the
//  pointer and the active tool, exactly as Fusion / SolidWorks / Onshape /
//  FreeCAD structure it.
//
//  It is a PURE QUERY plus transient visual state: given the raw pointer
//  position and the (read-only) sketch geometry it returns an adjusted
//  position and remembers the active snap and the candidate inferences, and
//  it draws the snap indicator / alignment guides / inference guides. It
//  does NOT mutate the model and does NOT create constraints; turning an
//  accepted snap or inference into a real constraint is a commit-time model
//  operation the canvas/tool performs, not the snap engine.
//
//  Extracted from SketchCanvas; holds a back-reference for the geometry it
//  reads (entities, zoom, transforms, grid, selection, drag axis) and is a
//  friend of it. The transient snap state is `mutable` so the query methods
//  keep the const surface the canvas exposed (and drop the old const_cast).
//
//  The snap/inference ALGORITHMS live in the library
//  (hobbycad/sketch/snap.h, hobbycad/sketch/inference.h); this is only the
//  GUI wiring around them.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_SNAPENGINE_H
#define HOBBYCAD_GUI_SNAPENGINE_H

#include <hobbycad/sketch/snap.h>
#include <hobbycad/sketch/inference.h>

#include <QPointF>

#include <optional>
#include <vector>

class QPainter;

namespace hobbycad {

class SketchCanvas;

/// GUI-side snapping + inference service for one SketchCanvas.
class SnapEngine {
public:
    explicit SnapEngine(SketchCanvas& canvas) : m_canvas(canvas) {}

    // ---- Pure snap/inference queries (set transient state) --------------
    /// Grid + entity snap for a raw world position; records the active snap.
    QPointF snapPoint(const QPointF& world) const;
    /// 45-degree angle snap of `target` about `origin`; records the snapped angle.
    QPointF snapToAngle(const QPointF& origin, const QPointF& target) const;
    /// Geometric inference for a segment p0->current while drawing: nudge the
    /// moving end onto an axis / parallel / perpendicular and remember the
    /// alignment (so the tool can commit it as a constraint). Returns the
    /// adjusted point; records the active inferences.
    QPointF computeInferences(const QPointF& p0, const QPointF& current) const;

    // ---- Overlay rendering ---------------------------------------------
    void drawSnapIndicator(QPainter& painter, const sketch::SnapPoint& snap) const;
    void drawSnapGuides(QPainter& painter) const;
    void drawInferenceGuides(QPainter& painter) const;

    // ---- Transient state ------------------------------------------------
    const std::optional<sketch::SnapPoint>& activeSnap() const { return m_activeSnap; }
    bool   hasActiveSnap() const { return m_activeSnap.has_value(); }
    void   clearActiveSnap() const { m_activeSnap.reset(); }
    bool   angleSnapActive() const { return m_angleSnapActive; }
    void   clearAngleSnap() const { m_angleSnapActive = false; }
    double snappedAngle() const { return m_snappedAngle; }
    const std::vector<sketch::Inference>& activeInferences() const { return m_activeInferences; }
    bool   hasActiveInferences() const { return !m_activeInferences.empty(); }
    void   clearInferences() const { m_activeInferences.clear(); }

    // ---- Configuration --------------------------------------------------
    bool   snapToEntities() const { return m_snapToEntities; }
    void   setSnapToEntities(bool v) { m_snapToEntities = v; }
    double entitySnapTolerance() const { return m_entitySnapTolerance; }

private:
    SketchCanvas& m_canvas;   ///< non-owning back-reference

    // Transient per-move state (mutable so the query methods stay const).
    mutable std::optional<sketch::SnapPoint> m_activeSnap;      ///< active snap indicator, if any
    mutable bool   m_angleSnapActive = false;                  ///< Ctrl angle snap engaged this move
    mutable double m_snappedAngle = 0.0;                       ///< the angle snapped to (degrees)
    mutable std::vector<sketch::Inference> m_activeInferences; ///< inferences to draw / commit

    bool   m_snapToEntities = true;    ///< entity snap points enabled
    double m_entitySnapTolerance = 10.0; ///< snap tolerance in pixels
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_SNAPENGINE_H
