// =====================================================================
//  src/hobbycad/gui/tools/sketchtoolhandler.h — per-tool handler interface
// =====================================================================
//
//  One handler per sketch tool. Replaces the per-tool `case` labels that are
//  currently spread across eight dispatch points in sketchcanvas.cpp
//  (mousePress/Move/Release, keyPress, wheel, drawPreview, startEntity,
//  updateEntity/finishEntity/initDimFields).
//
//  MIGRATION CONTRACT (this is what makes the change incremental):
//  every hook returns bool and the base returns false, meaning "I did not
//  handle this". SketchCanvas offers each event to the active handler first
//  and falls through to the existing switch when the handler declines. A tool
//  that has not been migrated yet has no handler at all, so it behaves
//  exactly as before. The app builds and runs after every single tool.
//
//  ⚠ mousePress and mouseRelease are SEPARATE hooks on purpose. Staged modes
//  accept two input styles: clicking each point, and press-drag-release to
//  drag THROUGH a stage (detected per stage via m_wasDragged at 5 px). Press
//  places a point on click; release places the next one if the user dragged.
//  A single "onClick" abstraction cannot express this, and collapsing the two
//  silently deletes drag-through placement.
//
//  ⚠ wheel() is not optional either: during Slot the wheel adjusts slot
//  radius, and during Polygon it adjusts side count, instead of zooming.
//
//  Layering: handlers are the Qt half. Placement RULES (stage meanings,
//  per-stage dimension fields, point-list normalization, completion tests)
//  are libhobbycad's sketch/placement.h; the drawing tools' handlers derive
//  from PlacementToolHandler, which translates events into those calls
//  (coding_standards 12.4).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_SKETCHTOOLHANDLER_H
#define HOBBYCAD_SKETCHTOOLHANDLER_H

#include "../sketchtoolbar.h"   // SketchTool

#include <QString>

class QMouseEvent;
class QKeyEvent;
class QWheelEvent;
class QPainter;
class QPen;
class QPointF;

namespace hobbycad {

class SketchCanvas;
struct SketchEntity;

class SketchToolHandler {
public:
    virtual ~SketchToolHandler() = default;

    /// Which tool this handler serves.
    virtual SketchTool tool() const = 0;

    /// Tool became active / was canceled.
    virtual void begin(SketchCanvas&) {}
    virtual void cancel(SketchCanvas&) {}

    // Event hooks. Return true only if the event was fully handled.
    virtual bool mousePress(SketchCanvas&, QMouseEvent*, const QPointF&)   { return false; }
    virtual bool mouseMove(SketchCanvas&, QMouseEvent*, const QPointF&)    { return false; }
    virtual bool mouseRelease(SketchCanvas&, QMouseEvent*, const QPointF&) { return false; }
    virtual bool keyPress(SketchCanvas&, QKeyEvent*)                       { return false; }
    virtual bool wheel(SketchCanvas&, QWheelEvent*)                        { return false; }
    virtual bool drawPreview(SketchCanvas&, QPainter&)                     { return false; }

    /// Rewrite the click-order point list into this tool/mode's canonical
    /// storage layout, and report whether the result is usable.
    ///
    /// This is NOT a formality: the stored layout differs per mode. Rect
    /// Center discards its center point, Rect ThreePoint expands 3 clicks
    /// into 4 corners, Circle TwoPoint stores 3 points, Slot ArcEnds
    /// REORDERS to [center, start, end]. A wrong layout renders identically
    /// on screen, so mistakes here are invisible until something downstream
    /// reads the points.
    ///
    /// Returns true if this handler owns the entity; `valid` then says
    /// whether to commit it. Returning false falls through to the canvas.
    virtual bool normalize(SketchCanvas&, SketchEntity&, bool& valid)
    { (void)valid; return false; }

    /// Set the entity type (and any type-level flags) for a placement that is
    /// just starting. Return true if this handler owns the active tool.
    virtual bool beginEntity(SketchCanvas&, SketchEntity&) { return false; }

    /// Track the in-progress entity to the mouse between clicks. Called from
    /// updateEntity(); return true if this handler owns the tool.
    virtual bool updateEntity(SketchCanvas&, const QPointF&) { return false; }

    /// Populate the dimension fields for the current stage. Return true if
    /// this handler owns them (the canvas then skips its own switch).
    virtual bool initDimFields(SketchCanvas&) { return false; }

    /// True when this tool collects more than two points, so the second
    /// click must not finish the entity. Arc (except Tangent), the 3-point
    /// circle, Spline and Point say yes; every other tool finishes on the
    /// second click. This is a policy question the tool owns, which is why
    /// it is a hook rather than a condition list on the canvas.
    virtual bool isMultiClick(const SketchCanvas&) const { return false; }

    /// True when the first click should start the entity outright rather
    /// than go through the shared two-point path. The 3-point arc modes and
    /// the 3-point circle need this because their first point is a real
    /// entity point, not a rubber-band anchor.
    virtual bool beginsOnFirstClick(const SketchCanvas&) const { return false; }

    /// True when a right-click should commit the entity in progress. Spline
    /// and the freeform polygon collect an open-ended number of points, so
    /// right-click is the only way to say "that is the last one".
    virtual bool finishesOnRightClick(const SketchCanvas&) const { return false; }

    /// Adjust the shared preview pen before the preview is drawn. The canvas
    /// sets a dashed blue pen for every tool; a tool that draws in another
    /// color (construction-geometry line mode) changes it here rather than
    /// having the canvas test for its mode. Return true if the pen was
    /// changed; the return value is informational, the pen is what matters.
    virtual bool previewPen(const SketchCanvas&, QPen&) const { return false; }

    /// A dimension field was just locked or unlocked by the user. Tools that
    /// derive state from "everything is now locked" capture it here:
    /// Rectangle records the mouse angle at that moment so the locked
    /// rectangle can be rotated afterwards.
    virtual void dimFieldsChanged(SketchCanvas&) {}

    /// True when holding Ctrl should snap the mouse to 45-degree increments
    /// from the first placed point. Only the tools whose shape is defined by
    /// a free direction want this: Line (except its already-constrained
    /// modes), Rectangle and the two linear slot modes.
    virtual bool supportsAngleSnap(const SketchCanvas&) const { return false; }

    /// Project the tracked cursor onto whatever this tool's current mode
    /// constrains it to (a horizontal or vertical axis, a tangent ray, a
    /// tangent arc path). Return true if the cursor was moved; the canvas then
    /// drops the entity snap indicator, because the snap point is no longer
    /// the position being used.
    ///
    /// Note this runs on EVERY move while drawing, not only on a click, which
    /// is what makes the constrained modes feel constrained rather than
    /// merely correcting themselves at commit time.
    virtual bool constrainCursor(SketchCanvas&, QPointF&, bool altHeld)
    { (void)altHeld; return false; }

    /// Status-bar prompt for the current stage; empty means "no hint".
    virtual QString hint(const SketchCanvas&) const { return {}; }

    /// SHORT hint drawn near the cursor for the tool's current stage, at ANY
    /// time it is active, including before the first point is placed (unlike
    /// drawPreview, which only runs while drawing). The canvas draws it in its
    /// always-run overlay pass. The default derives it from the status-bar
    /// hint(), dropping the "Tool: " prefix and any ", or type ..." / "(Shift
    /// ...)" tail, then parenthesizing, so every tool with a hint() gets a
    /// near-cursor version for free; tools with cleaner phrasing override this.
    /// Return {} for no hint.
    virtual QString cursorHint(const SketchCanvas& c) const {
        QString h = hint(c);
        if (h.isEmpty()) return {};
        const int colon = h.indexOf(QStringLiteral(": "));
        QString core = (colon >= 0) ? h.mid(colon + 2) : h;
        int cut = core.indexOf(QStringLiteral(", or"));
        if (cut >= 0) core = core.left(cut);
        cut = core.indexOf(QStringLiteral("  ("));   // the "  (Shift ...)" tail
        if (cut >= 0) core = core.left(cut);
        core = core.trimmed();
        return core.isEmpty() ? QString()
                              : (QStringLiteral("(") + core + QStringLiteral(")"));
    }

    /// Interpret a flat CreationMode value for THIS tool and set whatever
    /// per-tool mode state applies; return true when handled. A tool with no
    /// creation sub-modes returns false and nothing happens. CreationMode
    /// values restart at 0 per tool, which is why this takes a raw int: only
    /// the owning tool can say what a given value means.
    virtual bool applyCreationMode(SketchCanvas&, int /*modeValue*/)
    {
        return false;
    }

    /// May this tool change to `modeValue` with points already placed?
    ///
    /// Fusion switches creation type mid-tool from the Sketch Palette:
    /// "you can switch between Line types while the Line tool is active".
    /// Whether that is meaningful depends entirely on the tool: switching
    /// Rectangle from Corner to Center would retroactively turn the corner
    /// already clicked into a center, so the points would no longer mean
    /// what the user meant by them.
    ///
    /// Default false: the entity in progress is canceled, which is what
    /// every tool did before this existed. A tool opts in only for the
    /// switches under which its placed points keep their meaning.
    virtual bool canSwitchModeWhileDrawing(const SketchCanvas&,
                                           int /*modeValue*/) const
    {
        return false;
    }

    /// Should finishing a segment start the next one from its end point?
    ///
    /// Polyline drawing: click, click, click lays a connected run rather
    /// than one line per invocation of the tool. Both Fusion and Dune 3D
    /// work this way, and it is the precondition for switching to a
    /// tangent arc mid-run, which needs a preceding segment to be tangent
    /// TO. Escape or right-click ends the chain.
    ///
    /// Default false: a tool that finishes and stops, as every tool did
    /// before this existed.
    virtual bool chainsFromLastPoint(const SketchCanvas&) const
    {
        return false;
    }
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHTOOLHANDLER_H
