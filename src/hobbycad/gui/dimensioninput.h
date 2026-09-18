// =====================================================================
//  src/hobbycad/gui/dimensioninput.h — In-progress dimension input fields
// =====================================================================
//
//  The typed Length/Angle fields shown while a sketch entity is being
//  drawn. Their state, editing and evaluation are the library's
//  sketch::DimensionInput; this adapter turns Qt key events into its key
//  presses, runs the host's follow-ups, and paints the fields.
//
//  Canvas-coupled actions (re-applying the preview after a value is
//  locked, asking whether the current tool chains, repainting) go
//  through DimensionInputHost, which SketchCanvas implements.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_DIMENSIONINPUT_H
#define HOBBYCAD_GUI_DIMENSIONINPUT_H

#include <hobbycad/sketch/dimension_input.h>

#include <QString>

class QKeyEvent;
class QPainter;
class QPointF;

namespace hobbycad {

class ParameterEngine;

/// Callbacks the input needs from its host (the sketch canvas).
class DimensionInputHost {
public:
    virtual ~DimensionInputHost() = default;
    /// True when the active tool chains segments, so an empty Enter should
    /// end the chain rather than being swallowed here.
    virtual bool dimChainsFromLastPoint() const = 0;
    /// A value was just locked: let the tool capture any derived state and
    /// refresh the preview with the new constraint.
    virtual void dimAfterLock() = 0;
    /// Re-apply the preview after a value was unlocked.
    virtual void dimReapplyPreview() = 0;
    /// Request a repaint.
    virtual void dimRepaint() = 0;
};

/// The dimension-field input, as the canvas uses it.
class DimensionInput {
public:
    void setHost(DimensionInputHost* h) { m_host = h; }
    void setDisplayUnit(LengthUnit u) { m_input.setDisplayUnit(u); }
    void setParameterEngine(const ParameterEngine* e) { m_input.setParameterEngine(e); }

    // ---- Lifecycle (see sketch::DimensionInput) --------------------------
    void clearAll() { m_input.clearAll(); }
    /// Add a field (called by the active tool's initDimFields). The label is
    /// the translated display text; meaning is carried by `id` alone.
    void addField(sketch::DimField id, const QString& label)
    { m_input.addField(id, label.toStdString()); }
    void reinitForNextStage() { m_input.reinitForNextStage(); }
    void beginStates() { m_input.beginStates(); }
    void flushLocked() { m_input.flushLocked(); }
    void clearLocked() { m_input.clearLocked(); }

    // ---- Data ------------------------------------------------------------
    bool empty() const { return m_input.empty(); }
    int  fieldCount() const { return m_input.fieldCount(); }
    int  activeIndex() const { return m_input.activeIndex(); }
    void setFieldValue(int i, double v) { m_input.setLiveValue(i, v); }
    bool allLocked() const { return m_input.allLocked(); }
    double getLocked(int i) const { return m_input.lockedValue(i); }   ///< -1 if not locked
    const sketch::LockedDims& lockedForConstraints() const
    { return m_input.lockedForConstraints(); }
    const sketch::DimensionInput& state() const { return m_input; }

    // ---- Input + render --------------------------------------------------
    void prefill(int fieldIndex) { m_input.prefill(fieldIndex); }
    /// Handle a key while a field is active; returns true if it was consumed.
    bool handleKey(QKeyEvent* event);
    /// Draw one field at a screen position.
    void draw(QPainter& painter, const QPointF& position,
              int fieldIndex, double rotation) const;

private:
    sketch::DimensionInput m_input;
    DimensionInputHost* m_host = nullptr;        ///< non-owning
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_DIMENSIONINPUT_H
