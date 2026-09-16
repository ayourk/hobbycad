// =====================================================================
//  src/hobbycad/gui/tools/optoolhandlers.h — single-click operations
// =====================================================================
//
//  The nine editing operations: trim, extend, split, offset, fillet,
//  chamfer, the two patterns, and project.
//
//  Grouped in one pair of files rather than nine, because each is a single
//  mousePress branch of ~30 lines that delegates the real work to
//  libhobbycad. Split any of them out the moment it grows a preview, a
//  dimension field or a second stage.
//
//  These need almost nothing new from the canvas: 12 of the 14 operations
//  they call were ALREADY public on SketchCanvas (trimEntityAt, filletCorner,
//  createRectangularPattern, ...). Only hitTest had to be exposed, as pick().
//
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#ifndef HOBBYCAD_OPTOOLHANDLERS_H
#define HOBBYCAD_OPTOOLHANDLERS_H

#include "sketchtoolhandler.h"
#include <QSet>

#include <memory>
#include <vector>
#include <QCoreApplication>

namespace hobbycad {

class TrimHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Trim; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseMove(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
    bool mouseRelease(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
private:
    // Drag-through state: entities already handled during the current
    // press-drag, so brushing back over one does not trim it twice.
    bool m_dragging = false;
    QSet<int> m_handled;
};

class ExtendHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Extend; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class SplitHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Split; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class OffsetHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Offset; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class FilletHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Fillet; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class ChamferHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Chamfer; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class RectPatternHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::RectPattern; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class CircPatternHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::CircPattern; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

class ProjectHandler : public SketchToolHandler {
    Q_DECLARE_TR_FUNCTIONS(hobbycad::SketchCanvas)
public:
    SketchTool tool() const override { return SketchTool::Project; }
    bool initDimFields(SketchCanvas&) override { return true; }   // none
    void begin(SketchCanvas& canvas) override;   // open the source picker
    QString hint(const SketchCanvas& canvas) const override;
    bool mousePress(SketchCanvas& canvas, QMouseEvent* event, const QPointF& world) override;
};

/// All nine, ready to register.
std::vector<std::unique_ptr<SketchToolHandler>> makeOperationToolHandlers();

}  // namespace hobbycad
#endif
