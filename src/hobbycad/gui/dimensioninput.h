// =====================================================================
//  src/hobbycad/gui/dimensioninput.h — In-progress dimension input fields
// =====================================================================
//
//  The typed Length/Angle fields shown while a sketch entity is being
//  drawn: their state, keyboard editing, expression evaluation, and
//  on-canvas rendering. Extracted from SketchCanvas so the widget no
//  longer owns this whole subsystem.
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

#include <hobbycad/units.h>

#include <QString>
#include <QVector>
#include <QPair>

class QKeyEvent;
class QPainter;
class QPointF;

namespace hobbycad {

class ParameterEngine;

/// One dimension field offered by the active tool ("Length", "Angle", ...).
struct DimFieldDef {
    QString label;              ///< Display name
    bool isAngle = false;      ///< true = angle in degrees, false = length in mm
    double currentValue = 0.0; ///< Live value from the mouse, updated each frame
};

/// Runtime editing state for one field.
struct DimFieldState {
    QString inputBuffer;       ///< Characters typed so far (empty = not typing)
    bool locked = false;       ///< Enter was pressed; the value is locked
    double lockedValue = 0.0;  ///< The locked value (mm or degrees)
    int cursorPos = 0;         ///< Cursor position within inputBuffer
    bool selectAll = false;    ///< Whole buffer selected (next char replaces all)
};

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

/// The dimension-field input subsystem.
class DimensionInput {
public:
    void setHost(DimensionInputHost* h) { m_host = h; }
    void setDisplayUnit(LengthUnit u) { m_unit = u; }
    void setParameterEngine(ParameterEngine* e) { m_paramEngine = e; }

    // ---- Lifecycle -------------------------------------------------------
    /// Full reset (end of an entity): fields, states, active, and the
    /// accumulated locked-for-constraints list.
    void clearAll();
    /// Add a field (called by the active tool's initDimFields).
    void addField(const QString& label, bool isAngle);
    /// Before a re-init: carry any locked fields into the constraints list,
    /// then drop the fields/states (the locked list is kept).
    void reinitForNextStage();
    /// After the tool has added its fields: create per-field state and make
    /// the first field active.
    void beginStates();
    /// Append every currently-locked field to the constraints list (used when
    /// an entity is committed) without dropping fields/states.
    void flushLocked();
    /// Clear the accumulated locked-for-constraints list.
    void clearLocked();

    // ---- Data ------------------------------------------------------------
    bool empty() const { return m_fields.isEmpty(); }
    int  fieldCount() const { return m_fields.size(); }
    int  activeIndex() const { return m_active; }
    void setFieldValue(int i, double v)
    { if (i >= 0 && i < m_fields.size()) m_fields[i].currentValue = v; }
    bool allLocked() const;
    double getLocked(int i) const;   ///< lockedValue, or -1 if not locked
    const QVector<QPair<QString, double>>& lockedForConstraints() const { return m_locked; }

    // ---- Input + render --------------------------------------------------
    /// Pre-fill a field's buffer with its live value and select it.
    void prefill(int fieldIndex);
    /// Handle a key while a field is active; returns true if it was consumed.
    bool handleKey(QKeyEvent* event);
    /// Draw one field at a screen position.
    void draw(QPainter& painter, const QPointF& position,
              int fieldIndex, double rotation) const;

private:
    double evaluate(const QString& buffer, bool isAngle) const;

    QVector<DimFieldDef> m_fields;
    QVector<DimFieldState> m_states;
    int m_active = -1;
    QVector<QPair<QString, double>> m_locked;
    LengthUnit m_unit = LengthUnit::Millimeters;
    ParameterEngine* m_paramEngine = nullptr;   ///< non-owning
    DimensionInputHost* m_host = nullptr;        ///< non-owning
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_DIMENSIONINPUT_H
