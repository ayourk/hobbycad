// =====================================================================
//  src/hobbycad/gui/tools/placementtoolhandler.h — the drawing tools'
//  shared handler
// =====================================================================
//
//  A drawing tool's rules are sketch::placement* in the library: its
//  stages, prompts, fields, cursor, entity and preview. This base turns the
//  canvas's events and state into those calls; each tool adds only what is
//  still its own: its mode on the canvas, how clicks reach it (staged, the
//  canvas's two-click path, picking curves first), and the wheel.
//
//  Part of HobbyCAD.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_PLACEMENTTOOLHANDLER_H
#define HOBBYCAD_GUI_PLACEMENTTOOLHANDLER_H

#include "sketchtoolhandler.h"

#include <hobbycad/sketch/placement.h>

#include <QPointF>

namespace hobbycad {

/// The placement a canvas mode stands for. The canvas's per-tool mode enums
/// list their modes in CreationMode's order for that tool (checked in
/// placementtoolhandler.cpp).
template <typename Mode>
sketch::PlacementKind placementKind(SketchTool tool, Mode mode)
{
    return {tool, static_cast<CreationMode>(static_cast<int>(mode))};
}

class PlacementToolHandler : public SketchToolHandler {
public:
    /// The placement the canvas's mode for this tool stands for.
    virtual sketch::PlacementKind kind(const SketchCanvas& canvas) const = 0;

    bool initDimFields(SketchCanvas& canvas) override;
    QString hint(const SketchCanvas& canvas) const override;
    QString cursorHint(const SketchCanvas& canvas) const override;
    bool supportsAngleSnap(const SketchCanvas& canvas) const override;
    bool chainsFromLastPoint(const SketchCanvas& canvas) const override;
    bool canSwitchModeWhileDrawing(const SketchCanvas& canvas, int modeValue) const override;
    bool constrainCursor(SketchCanvas& canvas, QPointF& world, bool altHeld) override;
    bool updateEntity(SketchCanvas& canvas, const QPointF& pos) override;
    bool drawPreview(SketchCanvas& canvas, QPainter& painter) override;
    bool normalize(SketchCanvas& canvas, SketchEntity& entity, bool& valid) override;
    bool beginEntity(SketchCanvas& canvas, SketchEntity& entity) override;
    bool keyPress(SketchCanvas& canvas, QKeyEvent* event) override;
    void dimFieldsChanged(SketchCanvas& canvas) override;

protected:
    /// What the placement rules read, with the cursor at `cursor`.
    sketch::PlacementInput input(const SketchCanvas& canvas, const QPointF& cursor) const;
    /// How far the user is.
    virtual sketch::PlacementStage stage(const SketchCanvas& canvas) const;
    /// The preview to paint; the pen tool paints its own.
    virtual sketch::PlacementPreview preview(const SketchCanvas& canvas) const;

    /// Place a staged click at `world` (snapped, then onto the placement's
    /// path and locks), committing once the placement has all its clicks.
    /// A click places on press; a drag through a stage places on release
    /// (coding_standards 12.2), so both call this.
    void placeClick(SketchCanvas& canvas, const QPointF& world);
    /// The staged press and release: a click places on press, a drag
    /// through the stage on release; the release is consumed either way,
    /// so the canvas's two-click finish cannot end the placement early.
    bool stagedPress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world);
    bool stagedRelease(SketchCanvas& canvas, const QPointF& world);

    /// Where the cursor goes for `world`, moving placed clicks that a lock
    /// moves (a tangent line's start) on the canvas as well.
    QPointF placedCursor(SketchCanvas& canvas, const QPointF& world, bool keepSnap) const;

    /// A corner rectangle's turn, kept from the moment both sides locked.
    std::optional<sketch::LockedTurn> m_turn;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_PLACEMENTTOOLHANDLER_H
