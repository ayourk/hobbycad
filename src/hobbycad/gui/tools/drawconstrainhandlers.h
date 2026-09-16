// =====================================================================
//  src/hobbycad/gui/tools/drawconstrainhandlers.h — draw-then-constrain
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  The second interaction mode: draw roughly, then constrain, the way
//  FreeCAD and SolveSpace users already work. HobbyCAD's default is the
//  opposite (place precisely up front, with snapping and typed
//  dimensions), and both are worth having.
//
//  ONLY CLICK SEMANTICS DIFFER. Entities, the nineteen constraint types,
//  the solver, decomposition, undo, snapping and profile detection are all
//  shared; a handler here changes how a click is interpreted and nothing
//  below that. So each one SUBCLASSES its placement-first handler and
//  switches off the affordances that belong to precise placement, rather
//  than reimplementing geometry that already works.
//
//  Two things are switched off:
//
//    typed dimensions   initDimFields() adds no fields. A size typed while
//                       placing is precise placement by another name; here
//                       the size comes from a dimension applied after.
//    angle snap         only where the base tool had it. A 45-degree snap
//                       makes a line LOOK horizontal without being
//                       constrained horizontal, which is the confusion
//                       this mode exists to remove.
//
//  Hints are DERIVED from the placement-first hint rather than rewritten,
//  by dropping its ", or type ..." clause. Copying forty hint strings to
//  edit four words out of each would leave two sets to keep in step, and
//  they would drift.
//
//  A tool with no draw-then-constrain handler here uses its placement-first
//  handler instead. Point and Spline deliberately have none: neither offers a
//  typed dimension or an angle snap, so a variant would differ in nothing.
//
// =====================================================================

#ifndef HOBBYCAD_DRAWCONSTRAINHANDLERS_H
#define HOBBYCAD_DRAWCONSTRAINHANDLERS_H

#include "arctoolhandler.h"
#include "circletoolhandler.h"
#include "ellipsetoolhandler.h"
#include "linetoolhandler.h"
#include "polygontoolhandler.h"
#include "rectangletoolhandler.h"
#include "slottoolhandler.h"

namespace hobbycad {

/// Strip a hint's typed-dimension clause, keeping any trailing
/// parenthetical (Shift, scroll) that still applies.
///
/// Not currently used by a handler: typed dimensions turned out to belong
/// in both modes. Kept because it is the piece that would be needed if a
/// mode ever does without them, and it is covered by tests.
QString hintWithoutTypedDimension(QString hint);

/// A tool in draw-then-constrain mode: its placement-first handler with
/// typed dimensions switched off and the hint derived accordingly.
///
/// A template rather than a macro so the debugger, IDE navigation and the
/// type checker all still work (coding_standards.txt section 11).
template <typename Base>
class DrawConstrainOf : public Base {
public:
    // Typed dimensions are NOT switched off here. Typing a length after the
    // first point already creates a real dimensional constraint (the
    // second point then swings at fixed radius rather than sliding), so it
    // is constraint-driven drawing, which is what this mode is for. Fusion
    // offers the same during placement, and Dune 3D allows expressions in
    // those fields. Hints therefore keep their "or type ..." clause.
};

/// The same, for a tool that also has an angle snap to switch off.
template <typename Base>
class DrawConstrainNoSnapOf : public DrawConstrainOf<Base> {
public:
    bool supportsAngleSnap(const SketchCanvas&) const override { return false; }
};

// Only these three have an angle snap to drop.
using LineDrawConstrainHandler      = DrawConstrainNoSnapOf<LineToolHandler>;
using RectangleDrawConstrainHandler = DrawConstrainNoSnapOf<RectangleToolHandler>;
using SlotDrawConstrainHandler      = DrawConstrainNoSnapOf<SlotToolHandler>;

using CircleDrawConstrainHandler    = DrawConstrainOf<CircleToolHandler>;
using ArcDrawConstrainHandler       = DrawConstrainOf<ArcToolHandler>;
using PolygonDrawConstrainHandler   = DrawConstrainOf<PolygonToolHandler>;
using EllipseDrawConstrainHandler   = DrawConstrainOf<EllipseToolHandler>;

}  // namespace hobbycad

#endif  // HOBBYCAD_DRAWCONSTRAINHANDLERS_H
