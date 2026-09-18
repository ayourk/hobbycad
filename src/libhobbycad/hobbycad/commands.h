// =====================================================================
//  src/libhobbycad/hobbycad/commands.h — every command a front end offers
// =====================================================================
//
//  The capability tier of the front-end support layer: WHAT exists and
//  what it means, never WHERE it is shown. A command has an id, the text a
//  front end displays (English, extracted for translation), an icon name,
//  what it acts on (a sketch tool, a creation mode, a constraint type) and
//  the conditions under which it can be used. Menus, toolbars, their order
//  and the default key bindings are the arrangement tier
//  (hobbycad/layout/arrangement.h), which refers to commands by id.
//
//  Text is translated by the front end at display time with
//  commandContext() as the context and the command id as the
//  disambiguation, so the same English word used by two commands can be
//  translated two ways.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_COMMANDS_H
#define HOBBYCAD_COMMANDS_H

#include "core.h"
#include "translate.h"

#include <string>
#include <vector>

namespace hobbycad {

/// Active sketch tool (base entity type or operation).
enum class SketchTool {
    Select,
    Line,
    Rectangle,
    Circle,
    Arc,
    Spline,
    Polygon,
    Slot,
    Ellipse,
    Point,
    Text,
    Dimension,
    Constraint,
    Trim,
    Extend,
    Split,
    Offset,
    Fillet,
    Chamfer,
    RectPattern,
    CircPattern,
    Project
};

/// Creation mode variants for tools with more than one input method. The
/// numbering restarts at 0 for every tool, so a mode means something only
/// together with its tool.
enum class CreationMode {
    Default = 0,

    // Line modes
    LineTwoPoint = 0,
    LineHorizontal,
    LineVertical,
    LineTangent,
    LineConstruction,

    // Rectangle modes
    RectCorner = 0,       ///< Corner to corner (default)
    RectCenter,           ///< Center + corner
    RectThreePoint,       ///< 3-point (angled)
    RectParallelogram,    ///< 3-point parallelogram

    // Circle modes
    CircleCenterRadius = 0,  ///< Center + radius (default)
    CircleTwoPoint,          ///< Diameter (2 points)
    CircleThreePoint,        ///< Through 3 points
    CircleTwoTangent,        ///< Tangent to 2 entities + radius
    CircleThreeTangent,      ///< Tangent to 3 entities

    // Arc modes
    ArcThreePoint = 0,    ///< 3-point arc (default)
    ArcCenterStartEnd,    ///< Center + start + end
    ArcStartEndRadius,    ///< Start + end + radius
    ArcTangent,           ///< Tangent to existing curve

    // Ellipse modes
    EllipseCenterAxes = 0,   ///< Center + axes (default)
    EllipseThreePoint,       ///< 3-point
    EllipseArc,              ///< Elliptical arc: ellipse, then start and end on it
    EllipseSpanRiseArc,      ///< Two ends, then the apex (half ellipse)
    EllipseCornerArc,        ///< Corner, a point on each leg (quarter ellipse)
    EllipseEndpointsArc,     ///< Two perimeter points, center, axis direction

    // Spline modes
    SplineControlPoints = 0, ///< Bezier with editable handles (default)
    SplineFitPoints,         ///< Catmull-Rom (through points, no handles)
    SplineRational,          ///< Rational (weighted) Bezier
    SplineConic,             ///< Conic arc by rho, a rational Bezier with its rho

    // Polygon modes
    PolygonInscribed = 0,    ///< Inscribed in circle (default)
    PolygonCircumscribed,    ///< Circumscribed around circle
    PolygonFreeform,         ///< Click each vertex, close by clicking start

    // Slot modes
    SlotCenterToCenter = 0,  ///< Center to center (default)
    SlotOverall,             ///< Overall length
    SlotArcRadius,           ///< Arc slot: start, arc center, end (on the arc)
    SlotArcEnds              ///< Arc slot: start, end, arc center (free)
};

/// Tools of the 3D model workspace.
enum class ModelTool {
    None = 0,
    Sketch,
    SketchOnFace,
    ConstructionPlane,
    Extrude,
    CutExtrude,
    Revolve,
    CutRevolve,
    Loft,
    CutLoft,
    Sweep,
    CutSweep,
    Box,
    Cylinder,
    Sphere,
    Torus,
    Coil,
    Pipe,
    Fillet,
    Chamfer,
    SimpleHole,
    Counterbore,
    Countersink,
    ThreadedHole,
    MoveCopy,
    Align,
    Mirror,
    Pattern,
    Parameters,
    _Count
};

namespace commands {

/// Text a front end shows: the English source and the disambiguation it
/// is extracted with (the command id).
struct Text {
    const char* source = nullptr;
    const char* disambiguation = nullptr;

    bool empty() const { return !source || !*source; }
};

/// What kind of thing a command is.
enum class Kind {
    Action,      ///< does something once
    Toggle,      ///< on or off
    Tool,        ///< makes a sketch or model tool active
    Menu,        ///< titles a menu (a container in the arrangement)
    Group,       ///< titles a tool group on a toolbar
    Input,       ///< a mouse or keyboard gesture that has no menu entry
};

/// When a command can be used. A front end asks commandAvailable().
enum Requirement : unsigned {
    RequiresNothing   = 0,
    RequiresSketch    = 1u << 0,   ///< a sketch is open for editing
    RequiresViewport  = 1u << 1,   ///< a 3D viewport exists (Full mode)
    RequiresSelection = 1u << 2,   ///< something is selected
    NotImplemented    = 1u << 3,   ///< listed for completeness; not usable yet
};

/// One command.
struct Command {
    const char* id = "";
    Kind kind = Kind::Action;
    /// Which binding context the command belongs to (bindingContexts()).
    const char* context = "";
    /// Freedesktop icon theme name, and a Qt standard pixmap name to fall
    /// back to ("SP_ArrowUp"); other front ends may ignore the second.
    const char* icon = "";
    const char* iconFallback = "";
    Text label;           ///< plain name ("Line")
    Text menuText;        ///< with a mnemonic ("&Line"); empty = use label
    Text toolbarText;     ///< toolbar caption, may break lines; empty = label
    Text tooltip;         ///< empty = none
    SketchTool sketchTool = SketchTool::Select;
    CreationMode mode = CreationMode::Default;
    bool hasMode = false; ///< `mode` is set (a variant, or a tool opening in a mode)
    bool variant = false; ///< one of sketchTool's creation-mode variants
    ModelTool modelTool = ModelTool::None;
    /// A number the command carries: a sketch::ConstraintType or a
    /// sketch::TransformType, for the commands that apply one.
    int arg = -1;
    /// Toggles in one exclusive group ("view.workspace") exclude each other.
    const char* radioGroup = "";
    bool checkedByDefault = false;
    unsigned needs = RequiresNothing;   ///< Requirement flags
};

/// The translation context of every command text.
HOBBYCAD_EXPORT const char* commandContext();

/// Every command, in a stable order.
HOBBYCAD_EXPORT const std::vector<Command>& allCommands();

/// The command with this id, or nullptr.
HOBBYCAD_EXPORT const Command* findCommand(const std::string& id);

/// The command that activates a sketch tool (no variant), or nullptr.
HOBBYCAD_EXPORT const Command* commandForTool(SketchTool tool);

/// The command for a tool's creation mode, or nullptr.
HOBBYCAD_EXPORT const Command* commandForMode(SketchTool tool, CreationMode mode);

/// The command that activates a model tool, or nullptr.
HOBBYCAD_EXPORT const Command* commandForModelTool(ModelTool tool);

/// Where a binding context's keys are heard.
enum class BindingScope {
    Global,        ///< everywhere, before anything else
    Application,   ///< a window-wide shortcut
    Surface,       ///< only by one kind of view (the sketch canvas, the 3D viewport)
};

/// A binding context. A key bound in a Global or Application context
/// conflicts with the same key anywhere; two Surface contexts conflict only
/// when they share a surface, because a key reaches one view at a time.
struct BindingContext {
    const char* id;
    Text title;
    BindingScope scope;
    const char* surface;   ///< which view hears the keys; "" unless scope is Surface
};

/// Every binding context, in display order.
HOBBYCAD_EXPORT const std::vector<BindingContext>& bindingContexts();

/// The context with this id, or nullptr.
HOBBYCAD_EXPORT const BindingContext* findBindingContext(const std::string& id);

/// What the front end currently has, for commandAvailable().
struct UiState {
    bool sketchOpen = false;
    bool viewport = false;
    bool hasSelection = false;
};

/// True when `command` can be used in `state`.
HOBBYCAD_EXPORT bool commandAvailable(const Command& command, const UiState& state);

/// A label with its mnemonic markers removed ("Save &As..." becomes
/// "Save As...", "Tom && Jerry" becomes "Tom & Jerry").
HOBBYCAD_EXPORT std::string stripMnemonic(const std::string& text);

}  // namespace commands
}  // namespace hobbycad

#endif  // HOBBYCAD_COMMANDS_H
