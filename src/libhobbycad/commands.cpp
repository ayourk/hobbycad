// =====================================================================
//  src/libhobbycad/commands.cpp — every command a front end offers
// =====================================================================
//
//  The table below is data: one entry per command. Keep it in the order
//  commands are listed in hobbycad/commands.h's groups (file, edit,
//  construct, view, help, sketch, constraints, design, navigation).
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/commands.h>

#include <unordered_map>

namespace hobbycad {
namespace commands {

namespace {

/// Builds one Command; each call sets one aspect and returns the builder.
class CommandBuilder {
public:
    CommandBuilder(const char* id, Kind kind, const char* context)
    {
        m_c.id = id;
        m_c.kind = kind;
        m_c.context = context;
    }
    CommandBuilder& withIcon(const char* icon, const char* fallback)
    {
        m_c.icon = icon;
        m_c.iconFallback = fallback;
        return *this;
    }
    CommandBuilder& withLabel(Text t) { m_c.label = t; return *this; }
    CommandBuilder& withMenuText(Text t) { m_c.menuText = t; return *this; }
    CommandBuilder& withToolbarText(Text t) { m_c.toolbarText = t; return *this; }
    CommandBuilder& withTooltip(Text t) { m_c.tooltip = t; return *this; }
    CommandBuilder& forTool(SketchTool t) { m_c.sketchTool = t; return *this; }
    CommandBuilder& inMode(CreationMode m)
    {
        m_c.mode = m;
        m_c.hasMode = true;
        return *this;
    }
    CommandBuilder& asVariant() { m_c.variant = true; return *this; }
    CommandBuilder& forModelTool(ModelTool t) { m_c.modelTool = t; return *this; }
    CommandBuilder& withArg(int a) { m_c.arg = a; return *this; }
    CommandBuilder& inRadioGroup(const char* g) { m_c.radioGroup = g; return *this; }
    CommandBuilder& checked() { m_c.checkedByDefault = true; return *this; }
    CommandBuilder& needing(unsigned flags) { m_c.needs = flags; return *this; }
    const Command& command() const { return m_c; }

private:
    Command m_c;
};

CommandBuilder cmd(const char* id, Kind kind, const char* context)
{
    return CommandBuilder(id, kind, context);
}

std::vector<Command> buildCommands()
{
    std::vector<Command> all;
    auto add = [&all](const CommandBuilder& b) { all.push_back(b.command()); };

    add(cmd("menu.file", Kind::Menu, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "File", "menu.file"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&File", "menu.file")));
    add(cmd("file.new", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "New Document", "file.new"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&New", "file.new")));
    add(cmd("file.open", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Open", "file.open"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Open...", "file.open")));
    add(cmd("file.save", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Save", "file.save"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Save", "file.save")));
    add(cmd("file.saveAs", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Save As", "file.saveAs"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Save &As...", "file.saveAs")));
    add(cmd("file.close", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Close", "file.close"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Close", "file.close")));
    add(cmd("menu.file.import", Kind::Menu, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Import", "menu.file.import"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Import", "menu.file.import")));
    add(cmd("file.import.step", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Import STEP", "file.import.step"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "STEP File...", "file.import.step"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Import geometry from STEP file",
                "file.import.step")));
    add(cmd("file.import.dxf", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Import DXF", "file.import.dxf"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "DXF File (Sketch)...",
                "file.import.dxf"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Import DXF geometry into the active sketch",
                "file.import.dxf"))
            .needing(RequiresSketch));
    add(cmd("menu.file.export", Kind::Menu, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Export", "menu.file.export"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Export", "menu.file.export")));
    add(cmd("file.export.step", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Export STEP", "file.export.step"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "STEP File...", "file.export.step"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Export geometry to STEP file",
                "file.export.step")));
    add(cmd("file.export.stl", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Export STL", "file.export.stl"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "STL File...", "file.export.stl"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Export geometry to STL file for 3D printing",
                "file.export.stl")));
    add(cmd("file.export.dxf", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Export DXF", "file.export.dxf"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "DXF File (Sketch)...",
                "file.export.dxf"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Export sketch to DXF file",
                "file.export.dxf"))
            .needing(RequiresSketch));
    add(cmd("file.export.svg", Kind::Action, "file")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Export SVG", "file.export.svg"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "SVG File (Sketch)...",
                "file.export.svg"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Export sketch to SVG file",
                "file.export.svg"))
            .needing(RequiresSketch));
    add(cmd("file.quit", Kind::Action, "file")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Quit", "file.quit"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Quit", "file.quit")));
    add(cmd("menu.edit", Kind::Menu, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Edit", "menu.edit"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Edit", "menu.edit")));
    add(cmd("edit.undo", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Undo", "edit.undo"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Undo", "edit.undo")));
    add(cmd("edit.redo", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Redo", "edit.redo"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Redo", "edit.redo")));
    add(cmd("edit.cut", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut", "edit.cut"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cu&t", "edit.cut"))
            .needing(RequiresSelection));
    add(cmd("edit.copy", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Copy", "edit.copy"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Copy", "edit.copy"))
            .needing(RequiresSelection));
    add(cmd("edit.paste", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Paste", "edit.paste"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Paste", "edit.paste")));
    add(cmd("edit.delete", Kind::Action, "edit")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Delete", "edit.delete"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Delete", "edit.delete"))
            .needing(RequiresSelection));
    add(cmd("edit.selectAll", Kind::Action, "edit")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Select All", "edit.selectAll"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Select &All", "edit.selectAll")));
    add(cmd("menu.construct", Kind::Menu, "construct")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Construct", "menu.construct"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Construct", "menu.construct")));
    add(cmd("construct.plane", Kind::Action, "construct")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "New Construction Plane",
                "construct.plane"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "New Construction &Plane...",
                "construct.plane"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a new construction plane",
                "construct.plane")));
    add(cmd("menu.view", Kind::Menu, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "View", "menu.view"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&View", "menu.view")));
    add(cmd("view.terminal", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Toggle Terminal", "view.terminal"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Terminal", "view.terminal")));
    add(cmd("view.project", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Toggle Project", "view.project"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "P&roject", "view.project"))
            .checked());
    add(cmd("view.properties", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Toggle Properties",
                "view.properties"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Properties", "view.properties"))
            .checked());
    add(cmd("view.toolbar", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Toggle Toolbar", "view.toolbar"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Tool&bar", "view.toolbar"))
            .checked());
    add(cmd("view.changelog", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Toggle Change History",
                "view.changelog"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Change &History",
                "view.changelog")));
    add(cmd("view.drawThenConstrain", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Draw, then Constrain",
                "view.drawThenConstrain"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Draw, then Constrain",
                "view.drawThenConstrain"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Place geometry roughly and fix it with constraints afterwards, "
                "instead of snapping and typing dimensions as you place it",
                "view.drawThenConstrain")));
    add(cmd("view.showUnconstrained", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show Unconstrained Points",
                "view.showUnconstrained"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show &Unconstrained Points",
                "view.showUnconstrained"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Mark endpoints that are not constrained to other geometry",
                "view.showUnconstrained"))
            .checked());
    add(cmd("view.showConstraints", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show Constraints",
                "view.showConstraints"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show &Constraints",
                "view.showConstraints"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show the constraint symbols on the canvas; dimensions are "
                "unaffected",
                "view.showConstraints"))
            .checked());
    add(cmd("view.showDimensions", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show Dimensions",
                "view.showDimensions"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show &Dimensions",
                "view.showDimensions"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show the dimensional labels on the canvas; constraint symbols "
                "are unaffected",
                "view.showDimensions"))
            .checked());
    add(cmd("view.showProfiles", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show Profiles",
                "view.showProfiles"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Show &Profiles",
                "view.showProfiles"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Shade closed sketch profiles in blue",
                "view.showProfiles")));
    add(cmd("view.fitSketch", Kind::Action, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Fit Sketch to View",
                "view.fitSketch"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Fit Sketch to View",
                "view.fitSketch"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Zoom and pan so the whole sketch is visible",
                "view.fitSketch")));
    add(cmd("menu.view.workspace", Kind::Menu, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Workspace", "menu.view.workspace"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Workspace",
                "menu.view.workspace")));
    add(cmd("view.workspace.design", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Design", "view.workspace.design"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Design", "view.workspace.design"))
            .inRadioGroup("view.workspace")
            .checked());
    add(cmd("view.workspace.render", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Render", "view.workspace.render"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Render", "view.workspace.render"))
            .inRadioGroup("view.workspace"));
    add(cmd("view.workspace.animation", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Animation",
                "view.workspace.animation"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Animation",
                "view.workspace.animation"))
            .inRadioGroup("view.workspace"));
    add(cmd("view.workspace.simulation", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Simulation",
                "view.workspace.simulation"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Simulation",
                "view.workspace.simulation"))
            .inRadioGroup("view.workspace"));
    add(cmd("view.resetView", Kind::Action, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Reset View", "view.resetView"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Reset &View", "view.resetView")));
    add(cmd("view.lookAt", Kind::Action, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Look At Sketch Plane",
                "view.lookAt"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Look &At Sketch Plane",
                "view.lookAt"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Orient the camera square onto the active sketch plane",
                "view.lookAt"))
            .needing(RequiresViewport));
    add(cmd("view.slice", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Slice at Sketch Plane",
                "view.slice"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Slice at Sketch Plane",
                "view.slice"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Section the model at the active sketch plane to see inside",
                "view.slice"))
            .needing(RequiresViewport));
    add(cmd("view.rotateLeft", Kind::Action, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Left 90\xC2\xB0",
                "view.rotateLeft"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate &Left 90\xC2\xB0",
                "view.rotateLeft")));
    add(cmd("view.rotateRight", Kind::Action, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Right 90\xC2\xB0",
                "view.rotateRight"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Ri&ght 90\xC2\xB0",
                "view.rotateRight")));
    add(cmd("view.showGrid", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Show Grid", "view.showGrid"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Show Gri&d", "view.showGrid"))
            .checked());
    add(cmd("view.snapToGrid", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Snap to Grid", "view.snapToGrid"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Snap to Grid",
                "view.snapToGrid")));
    add(cmd("view.zUpOrientation", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Z-Up Orientation",
                "view.zUpOrientation"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Z-Up Orientation",
                "view.zUpOrientation"))
            .checked());
    add(cmd("view.orbitSelected", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Orbit Selected Object",
                "view.orbitSelected"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Orbit Selected Object",
                "view.orbitSelected")));
    add(cmd("menu.view.theme", Kind::Menu, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Theme", "menu.view.theme"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Theme", "menu.view.theme")));
    add(cmd("view.theme.light", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Light", "view.theme.light"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Light", "view.theme.light"))
            .inRadioGroup("view.theme"));
    add(cmd("view.theme.dark", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Dark", "view.theme.dark"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Dark", "view.theme.dark"))
            .inRadioGroup("view.theme"));
    add(cmd("view.theme.edit", Kind::Action, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Edit Theme", "view.theme.edit"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Edit...", "view.theme.edit")));
    add(cmd("menu.view.selectionFilter", Kind::Menu, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Selection Filter",
                "menu.view.selectionFilter"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Selection &Filter",
                "menu.view.selectionFilter")));
    add(cmd("view.filter.all", Kind::Toggle, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "All", "view.filter.all"))
            .inRadioGroup("view.filter")
            .checked());
    add(cmd("view.filter.points", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Points only", "view.filter.points"))
            .inRadioGroup("view.filter"));
    add(cmd("view.filter.curves", Kind::Toggle, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Curves only", "view.filter.curves"))
            .inRadioGroup("view.filter"));
    add(cmd("menu.view.language", Kind::Menu, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Language", "menu.view.language"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "L&anguage", "menu.view.language")));
    add(cmd("view.customize", Kind::Action, "view")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands",
                                                "Customize Menus and Toolbars",
                                                "view.customize"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands",
                                                   "C&ustomize...",
                                                   "view.customize"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Move, rename, hide and restore what the menus and toolbars show",
                "view.customize")));
    add(cmd("view.preferences", Kind::Action, "view")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Preferences", "view.preferences"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Pre&ferences...",
                "view.preferences")));
    add(cmd("menu.help", Kind::Menu, "global")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Help", "menu.help"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Help", "menu.help")));
    add(cmd("help.about", Kind::Action, "global")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "About HobbyCAD", "help.about"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&About HobbyCAD...",
                "help.about")));
    add(cmd("global.commandSearch", Kind::Action, "global")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Command Search",
                "global.commandSearch"))
            .needing(NotImplemented));
    add(cmd("menu.sketch", Kind::Menu, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sketch", "menu.sketch"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "S&ketch", "menu.sketch")));
    add(cmd("sketch.select", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Select", "sketch.select"))
            .forTool(SketchTool::Select)
            .needing(RequiresSketch));
    add(cmd("group.sketch.create", Kind::Group, "sketch")
            .withIcon("draw-freehand", "SP_FileDialogNewFolder")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Create", "group.sketch.create"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create geometry",
                "group.sketch.create")));
    add(cmd("sketch.line", Kind::Tool, "sketch")
            .withIcon("draw-line", "SP_ArrowForward")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Line", "sketch.line"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Line", "sketch.line"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw line", "sketch.line"))
            .forTool(SketchTool::Line)
            .needing(RequiresSketch));
    add(cmd("sketch.rectangle", Kind::Tool, "sketch")
            .withIcon("draw-rectangle", "SP_DialogApplyButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Rectangle", "sketch.rectangle"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Rectangle", "sketch.rectangle"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Draw rectangle",
                "sketch.rectangle"))
            .forTool(SketchTool::Rectangle)
            .needing(RequiresSketch));
    add(cmd("sketch.circle", Kind::Tool, "sketch")
            .withIcon("draw-circle", "SP_DialogHelpButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Circle", "sketch.circle"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Circle", "sketch.circle"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw circle", "sketch.circle"))
            .forTool(SketchTool::Circle)
            .needing(RequiresSketch));
    add(cmd("sketch.arc", Kind::Tool, "sketch")
            .withIcon("draw-arc", "SP_BrowserReload")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Arc", "sketch.arc"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Arc", "sketch.arc"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw arc", "sketch.arc"))
            .forTool(SketchTool::Arc)
            .needing(RequiresSketch));
    add(cmd("sketch.spline", Kind::Tool, "sketch")
            .withIcon("draw-bezier-curves", "SP_DesktopIcon")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Spline", "sketch.spline"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Draw a Bezier or Catmull-Rom spline",
                "sketch.spline"))
            .forTool(SketchTool::Spline)
            .needing(RequiresSketch));
    add(cmd("sketch.polygon", Kind::Tool, "sketch")
            .withIcon("draw-polygon", "SP_DialogResetButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Polygon", "sketch.polygon"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw polygon", "sketch.polygon"))
            .forTool(SketchTool::Polygon)
            .needing(RequiresSketch));
    add(cmd("sketch.slot", Kind::Tool, "sketch")
            .withIcon("draw-rectangle", "SP_BrowserStop")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Slot", "sketch.slot"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw slot", "sketch.slot"))
            .forTool(SketchTool::Slot)
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse", Kind::Tool, "sketch")
            .withIcon("draw-ellipse", "SP_MessageBoxInformation")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Ellipse", "sketch.ellipse"))
            .withMenuText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Ellipse", "sketch.ellipse"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Draw ellipse", "sketch.ellipse"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseCenterAxes)
            .needing(RequiresSketch));
    add(cmd("sketch.point", Kind::Tool, "sketch")
            .withIcon("draw-circle", "SP_DialogCancelButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Point", "sketch.point"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "&Point", "sketch.point"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Place point", "sketch.point"))
            .forTool(SketchTool::Point)
            .needing(RequiresSketch));
    add(cmd("sketch.line.twoPoint", Kind::Tool, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Two Point", "sketch.line.twoPoint"))
            .forTool(SketchTool::Line)
            .inMode(CreationMode::LineTwoPoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.line.tangent", Kind::Tool, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Tangent", "sketch.line.tangent"))
            .forTool(SketchTool::Line)
            .inMode(CreationMode::LineTangent)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.line.construction", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Construction",
                "sketch.line.construction"))
            .forTool(SketchTool::Line)
            .inMode(CreationMode::LineConstruction)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.rectangle.corner", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Corner to Corner",
                "sketch.rectangle.corner"))
            .forTool(SketchTool::Rectangle)
            .inMode(CreationMode::RectCorner)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.rectangle.center", Kind::Tool, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Center", "sketch.rectangle.center"))
            .forTool(SketchTool::Rectangle)
            .inMode(CreationMode::RectCenter)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.rectangle.threePoint", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "3-Point (Angled)",
                "sketch.rectangle.threePoint"))
            .forTool(SketchTool::Rectangle)
            .inMode(CreationMode::RectThreePoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.rectangle.parallelogram", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Parallelogram",
                "sketch.rectangle.parallelogram"))
            .forTool(SketchTool::Rectangle)
            .inMode(CreationMode::RectParallelogram)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.circle.centerRadius", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Center + Radius",
                "sketch.circle.centerRadius"))
            .forTool(SketchTool::Circle)
            .inMode(CreationMode::CircleCenterRadius)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.circle.twoPoint", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "2-Point (Diameter)",
                "sketch.circle.twoPoint"))
            .forTool(SketchTool::Circle)
            .inMode(CreationMode::CircleTwoPoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.circle.threePoint", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "3-Point",
                "sketch.circle.threePoint"))
            .forTool(SketchTool::Circle)
            .inMode(CreationMode::CircleThreePoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.circle.twoTangent", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Tangent to 2",
                "sketch.circle.twoTangent"))
            .forTool(SketchTool::Circle)
            .inMode(CreationMode::CircleTwoTangent)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.circle.threeTangent", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Tangent to 3",
                "sketch.circle.threeTangent"))
            .forTool(SketchTool::Circle)
            .inMode(CreationMode::CircleThreeTangent)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.arc.centerStartEnd", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Center + Start + End",
                "sketch.arc.centerStartEnd"))
            .forTool(SketchTool::Arc)
            .inMode(CreationMode::ArcCenterStartEnd)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.arc.startEndRadius", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Start + End + Radius",
                "sketch.arc.startEndRadius"))
            .forTool(SketchTool::Arc)
            .inMode(CreationMode::ArcStartEndRadius)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.arc.tangent", Kind::Tool, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Tangent", "sketch.arc.tangent"))
            .forTool(SketchTool::Arc)
            .inMode(CreationMode::ArcTangent)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.arc.threePoint", Kind::Tool, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "3-Point", "sketch.arc.threePoint"))
            .forTool(SketchTool::Arc)
            .inMode(CreationMode::ArcThreePoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.spline.cubicBezier", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Cubic Bezier",
                "sketch.spline.cubicBezier"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Cubic Bezier",
                "sketch.spline.cubicBezier"))
            .forTool(SketchTool::Spline)
            .inMode(CreationMode::SplineControlPoints)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.spline.catmullRom", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Catmull-Rom",
                "sketch.spline.catmullRom"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Catmull-Rom Spline",
                "sketch.spline.catmullRom"))
            .forTool(SketchTool::Spline)
            .inMode(CreationMode::SplineFitPoints)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.spline.rational", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rational Bezier",
                "sketch.spline.rational"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "&Rational Bezier",
                "sketch.spline.rational"))
            .forTool(SketchTool::Spline)
            .inMode(CreationMode::SplineRational)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.spline.conic", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Conic Arc (Rho)",
                "sketch.spline.conic"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Conic Arc (Rh&o)",
                "sketch.spline.conic"))
            .forTool(SketchTool::Spline)
            .inMode(CreationMode::SplineConic)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.polygon.inscribed", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Inscribed",
                "sketch.polygon.inscribed"))
            .forTool(SketchTool::Polygon)
            .inMode(CreationMode::PolygonInscribed)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.polygon.circumscribed", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Circumscribed",
                "sketch.polygon.circumscribed"))
            .forTool(SketchTool::Polygon)
            .inMode(CreationMode::PolygonCircumscribed)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.polygon.freeform", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Freeform",
                "sketch.polygon.freeform"))
            .forTool(SketchTool::Polygon)
            .inMode(CreationMode::PolygonFreeform)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.slot.centerToCenter", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Center to Center",
                "sketch.slot.centerToCenter"))
            .forTool(SketchTool::Slot)
            .inMode(CreationMode::SlotCenterToCenter)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.slot.overall", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Overall Length",
                "sketch.slot.overall"))
            .forTool(SketchTool::Slot)
            .inMode(CreationMode::SlotOverall)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.slot.arcRadius", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Arc Slot (Radius)",
                "sketch.slot.arcRadius"))
            .forTool(SketchTool::Slot)
            .inMode(CreationMode::SlotArcRadius)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.slot.arcEnds", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Arc Slot (Ends)",
                "sketch.slot.arcEnds"))
            .forTool(SketchTool::Slot)
            .inMode(CreationMode::SlotArcEnds)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.centerAxes", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Center + Axes",
                "sketch.ellipse.centerAxes"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseCenterAxes)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.threePoint", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "3-Point",
                "sketch.ellipse.threePoint"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseThreePoint)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.arc", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Elliptical Arc",
                "sketch.ellipse.arc"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseArc)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.spanRise", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Span + Rise Elliptical Arc",
                "sketch.ellipse.spanRise"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseSpanRiseArc)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.corner", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Corner Elliptical Arc",
                "sketch.ellipse.corner"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseCornerArc)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("sketch.ellipse.endpoints", Kind::Tool, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Endpoints Elliptical Arc",
                "sketch.ellipse.endpoints"))
            .forTool(SketchTool::Ellipse)
            .inMode(CreationMode::EllipseEndpointsArc)
            .asVariant()
            .needing(RequiresSketch));
    add(cmd("group.sketch.constrain", Kind::Group, "sketch")
            .withIcon("measure", "SP_FileDialogInfoView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Dimension",
                "group.sketch.constrain"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Add dimension",
                "group.sketch.constrain")));
    add(cmd("sketch.dimension", Kind::Tool, "sketch")
            .withIcon("measure", "SP_FileDialogInfoView")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Dimension", "sketch.dimension"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Add dimension", "sketch.dimension"))
            .forTool(SketchTool::Dimension)
            .needing(RequiresSketch));
    add(cmd("sketch.constraint", Kind::Tool, "sketch")
            .withIcon("draw-connector", "SP_DialogOkButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Constraint", "sketch.constraint"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Add constraint",
                "sketch.constraint"))
            .forTool(SketchTool::Constraint)
            .needing(RequiresSketch));
    add(cmd("sketch.text", Kind::Tool, "sketch")
            .withIcon("draw-text", "SP_FileDialogDetailedView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Text", "sketch.text"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Add text", "sketch.text"))
            .forTool(SketchTool::Text)
            .needing(RequiresSketch));
    add(cmd("group.sketch.modify", Kind::Group, "sketch")
            .withIcon("edit-cut", "SP_DialogDiscardButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Trim", "group.sketch.modify"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Trim entity at intersections",
                "group.sketch.modify")));
    add(cmd("sketch.trim", Kind::Tool, "sketch")
            .withIcon("edit-cut", "SP_DialogDiscardButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Trim", "sketch.trim"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Trim entity at intersections",
                "sketch.trim"))
            .forTool(SketchTool::Trim)
            .needing(RequiresSketch));
    add(cmd("sketch.extend", Kind::Tool, "sketch")
            .withIcon("format-indent-more", "SP_ArrowRight")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Extend", "sketch.extend"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Extend entity to nearest intersection",
                "sketch.extend"))
            .forTool(SketchTool::Extend)
            .needing(RequiresSketch));
    add(cmd("sketch.split", Kind::Tool, "sketch")
            .withIcon("view-split-left-right", "SP_DialogNoButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Split", "sketch.split"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Split entity at intersections",
                "sketch.split"))
            .forTool(SketchTool::Split)
            .needing(RequiresSketch));
    add(cmd("sketch.offset", Kind::Tool, "sketch")
            .withIcon("object-order-raise", "SP_FileDialogContentsView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Offset", "sketch.offset"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Offset geometry", "sketch.offset"))
            .forTool(SketchTool::Offset)
            .needing(RequiresSketch));
    add(cmd("sketch.fillet", Kind::Tool, "sketch")
            .withIcon("draw-bezier-curves", "SP_DialogApplyButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Fillet", "sketch.fillet"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Fillet corners", "sketch.fillet"))
            .forTool(SketchTool::Fillet)
            .needing(RequiresSketch));
    add(cmd("sketch.chamfer", Kind::Tool, "sketch")
            .withIcon("draw-polygon", "SP_DialogDiscardButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Chamfer", "sketch.chamfer"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Chamfer corners", "sketch.chamfer"))
            .forTool(SketchTool::Chamfer)
            .needing(RequiresSketch));
    add(cmd("sketch.transform.move", Kind::Action, "sketch")
            .withIcon("transform-move", "SP_ArrowUp")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Move", "sketch.transform.move"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Move the selected geometry",
                "sketch.transform.move"))
            .withArg(0)
            .needing(RequiresSketch | RequiresSelection));
    add(cmd("sketch.transform.rotate", Kind::Action, "sketch")
            .withIcon("object-rotate-left", "SP_BrowserReload")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Rotate", "sketch.transform.rotate"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate the selected geometry",
                "sketch.transform.rotate"))
            .withArg(2)
            .needing(RequiresSketch | RequiresSelection));
    add(cmd("sketch.transform.scale", Kind::Action, "sketch")
            .withIcon("zoom-fit-best", "SP_FileDialogDetailedView")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Scale", "sketch.transform.scale"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Scale the selected geometry",
                "sketch.transform.scale"))
            .withArg(3)
            .needing(RequiresSketch | RequiresSelection));
    add(cmd("sketch.transform.mirror", Kind::Action, "sketch")
            .withIcon("object-flip-horizontal", "SP_DialogResetButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Mirror", "sketch.transform.mirror"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Mirror the selected geometry",
                "sketch.transform.mirror"))
            .withArg(4)
            .needing(RequiresSketch | RequiresSelection));
    add(cmd("sketch.transform.copy", Kind::Action, "sketch")
            .withIcon("edit-copy", "SP_DialogSaveButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Copy", "sketch.transform.copy"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Copy the selected geometry",
                "sketch.transform.copy"))
            .withArg(1)
            .needing(RequiresSketch | RequiresSelection));
    add(cmd("group.sketch.pattern", Kind::Group, "sketch")
            .withIcon("view-grid", "SP_FileDialogListView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rect Pattern",
                "group.sketch.pattern"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rect\nPattern",
                "group.sketch.pattern"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create rectangular pattern",
                "group.sketch.pattern")));
    add(cmd("sketch.rectPattern", Kind::Tool, "sketch")
            .withIcon("view-grid", "SP_FileDialogListView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rect Pattern",
                "sketch.rectPattern"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create rectangular pattern",
                "sketch.rectPattern"))
            .forTool(SketchTool::RectPattern)
            .needing(RequiresSketch));
    add(cmd("sketch.circPattern", Kind::Tool, "sketch")
            .withIcon("view-refresh", "SP_BrowserReload")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Circ Pattern",
                "sketch.circPattern"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create circular pattern",
                "sketch.circPattern"))
            .forTool(SketchTool::CircPattern)
            .needing(RequiresSketch));
    add(cmd("sketch.project", Kind::Tool, "sketch")
            .withIcon("transform-move", "SP_ArrowDown")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Projection", "sketch.project"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Project geometry from other sketches",
                "sketch.project"))
            .forTool(SketchTool::Project)
            .needing(RequiresSketch));
    add(cmd("sketch.toggle3d", Kind::Toggle, "sketch")
            .withIcon("draw-cuboid", "SP_FileDialogDetailedView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "3D", "sketch.toggle3d"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Toggle 3D sketch mode",
                "sketch.toggle3d"))
            .needing(RequiresSketch));
    add(cmd("sketch.flip", Kind::Toggle, "sketch")
            .withIcon("", "SP_BrowserReload")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Flip", "sketch.flip"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Draw from the far side of the plane (heads/tails)",
                "sketch.flip"))
            .needing(RequiresSketch));
    add(cmd("sketch.finish", Kind::Action, "sketch")
            .withIcon("", "SP_DialogApplyButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Finish Sketch", "sketch.finish"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Finish editing the sketch",
                "sketch.finish"))
            .needing(RequiresSketch));
    add(cmd("sketch.curvatureComb", Kind::Toggle, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Curvature Comb",
                "sketch.curvatureComb"))
            .needing(RequiresSketch));
    add(cmd("sketch.rotateCCW", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Canvas CCW",
                "sketch.rotateCCW"))
            .needing(RequiresSketch));
    add(cmd("sketch.rotateCW", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Canvas CW",
                "sketch.rotateCW"))
            .needing(RequiresSketch));
    add(cmd("sketch.rotateReset", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Reset Canvas Rotation",
                "sketch.rotateReset"))
            .needing(RequiresSketch));
    add(cmd("sketch.construction", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Toggle Construction Mode",
                "sketch.construction"))
            .needing(RequiresSketch));
    add(cmd("sketch.toggleGrid", Kind::Action, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Toggle Grid", "sketch.toggleGrid"))
            .needing(RequiresSketch));
    add(cmd("menu.constraints", Kind::Menu, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Constraints", "menu.constraints"))
            .withMenuText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "C&onstraints",
                "menu.constraints")));
    add(cmd("sketch.constrain.coincident", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Coincident",
                "sketch.constrain.coincident"))
            .withArg(8)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.horizontal", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Horizontal",
                "sketch.constrain.horizontal"))
            .withArg(4)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.vertical", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Vertical",
                "sketch.constrain.vertical"))
            .withArg(5)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.parallel", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Parallel",
                "sketch.constrain.parallel"))
            .withArg(6)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.perpendicular", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Perpendicular",
                "sketch.constrain.perpendicular"))
            .withArg(7)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.tangent", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Tangent",
                "sketch.constrain.tangent"))
            .withArg(9)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.curvature", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Curvature (G2)",
                "sketch.constrain.curvature"))
            .withArg(20)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.equal", Kind::Action, "sketch")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Equal", "sketch.constrain.equal"))
            .withArg(10)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.midpoint", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Midpoint",
                "sketch.constrain.midpoint"))
            .withArg(11)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.concentric", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Concentric",
                "sketch.constrain.concentric"))
            .withArg(13)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.collinear", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Collinear",
                "sketch.constrain.collinear"))
            .withArg(14)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.pointOnSpline", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Point on Spline",
                "sketch.constrain.pointOnSpline"))
            .withArg(21)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.tangentAngle", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Tangent Angle",
                "sketch.constrain.tangentAngle"))
            .withArg(23)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.symmetric", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Symmetric",
                "sketch.constrain.symmetric"))
            .withArg(12)
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.angleTwoLines", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Angle (2 lines)",
                "sketch.constrain.angleTwoLines"))
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.fix", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Fix / Unfix",
                "sketch.constrain.fix"))
            .needing(RequiresSketch));
    add(cmd("sketch.constrain.auto", Kind::Action, "sketch")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Auto Constrain",
                "sketch.constrain.auto"))
            .needing(RequiresSketch));
    add(cmd("group.design.sketch", Kind::Group, "design")
            .withIcon("draw-freehand", "SP_FileDialogDetailedView")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sketch", "group.design.sketch"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a 2D sketch",
                "group.design.sketch")));
    add(cmd("design.sketch", Kind::Tool, "design")
            .withIcon("draw-freehand", "SP_FileDialogDetailedView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sketch", "design.sketch"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create sketch on a plane",
                "design.sketch"))
            .forModelTool(ModelTool::Sketch));
    add(cmd("design.sketchOnFace", Kind::Tool, "design")
            .withIcon("draw-polygon", "SP_FileDialogContentsView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Sketch on Face",
                "design.sketchOnFace"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Sketch on\nFace",
                "design.sketchOnFace"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create sketch on existing face",
                "design.sketchOnFace"))
            .forModelTool(ModelTool::SketchOnFace));
    add(cmd("group.design.plane", Kind::Group, "design")
            .withIcon("draw-rectangle", "SP_FileDialogListView")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Plane", "group.design.plane"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create construction plane",
                "group.design.plane")));
    add(cmd("design.constructionPlane", Kind::Tool, "design")
            .withIcon("draw-rectangle", "SP_FileDialogListView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Construction Plane",
                "design.constructionPlane"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Construction\nPlane",
                "design.constructionPlane"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a construction plane",
                "design.constructionPlane"))
            .forModelTool(ModelTool::ConstructionPlane));
    add(cmd("group.design.solid", Kind::Group, "design")
            .withIcon("go-up", "SP_ArrowUp")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Solid", "group.design.solid"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create solid geometry",
                "group.design.solid")));
    add(cmd("design.extrude", Kind::Tool, "design")
            .withIcon("go-up", "SP_ArrowUp")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Extrude", "design.extrude"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Extrude to add material",
                "design.extrude"))
            .forModelTool(ModelTool::Extrude)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.cutExtrude", Kind::Tool, "design")
            .withIcon("go-down", "SP_ArrowDown")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut Extrude", "design.cutExtrude"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut\nExtrude", "design.cutExtrude"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Extrude to remove material",
                "design.cutExtrude"))
            .forModelTool(ModelTool::CutExtrude)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.revolve", Kind::Tool, "design")
            .withIcon("object-rotate-right", "SP_BrowserReload")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Revolve", "design.revolve"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Revolve to add material",
                "design.revolve"))
            .forModelTool(ModelTool::Revolve)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.cutRevolve", Kind::Tool, "design")
            .withIcon("object-rotate-left", "SP_BrowserStop")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut Revolve", "design.cutRevolve"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut\nRevolve", "design.cutRevolve"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Revolve to remove material",
                "design.cutRevolve"))
            .forModelTool(ModelTool::CutRevolve)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.loft", Kind::Tool, "design")
            .withIcon("draw-bezier-curves", "SP_DesktopIcon")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Loft", "design.loft"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Loft to add material",
                "design.loft"))
            .forModelTool(ModelTool::Loft)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.cutLoft", Kind::Tool, "design")
            .withIcon("edit-cut", "SP_DialogNoButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut Loft", "design.cutLoft"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut\nLoft", "design.cutLoft"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Loft to remove material",
                "design.cutLoft"))
            .forModelTool(ModelTool::CutLoft)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.sweep", Kind::Tool, "design")
            .withIcon("draw-path", "SP_ArrowForward")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sweep", "design.sweep"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Sweep to add material",
                "design.sweep"))
            .forModelTool(ModelTool::Sweep)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.cutSweep", Kind::Tool, "design")
            .withIcon("draw-eraser", "SP_DialogDiscardButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut Sweep", "design.cutSweep"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cut\nSweep", "design.cutSweep"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Sweep to remove material",
                "design.cutSweep"))
            .forModelTool(ModelTool::CutSweep)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.box", Kind::Tool, "design")
            .withIcon("draw-cube", "SP_ComputerIcon")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Box", "design.box"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Create a box", "design.box"))
            .forModelTool(ModelTool::Box)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.cylinder", Kind::Tool, "design")
            .withIcon("draw-cylinder", "SP_DriveHDIcon")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Cylinder", "design.cylinder"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a cylinder",
                "design.cylinder"))
            .forModelTool(ModelTool::Cylinder)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.sphere", Kind::Tool, "design")
            .withIcon("draw-sphere", "SP_DialogHelpButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sphere", "design.sphere"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Create a sphere", "design.sphere"))
            .forModelTool(ModelTool::Sphere)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.torus", Kind::Tool, "design")
            .withIcon("draw-donut", "SP_DialogResetButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Torus", "design.torus"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Create a torus", "design.torus"))
            .forModelTool(ModelTool::Torus)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.coil", Kind::Tool, "design")
            .withIcon("draw-spiral", "SP_BrowserReload")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Coil", "design.coil"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a coil/helix",
                "design.coil"))
            .forModelTool(ModelTool::Coil)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.pipe", Kind::Tool, "design")
            .withIcon("draw-path", "SP_ArrowRight")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Pipe", "design.pipe"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a pipe along a path",
                "design.pipe"))
            .forModelTool(ModelTool::Pipe)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("group.design.fillet", Kind::Group, "design")
            .withIcon("format-stroke-color", "SP_DialogApplyButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Fillet", "group.design.fillet"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Round or bevel edges",
                "group.design.fillet")));
    add(cmd("design.fillet", Kind::Tool, "design")
            .withIcon("format-stroke-color", "SP_DialogApplyButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Fillet", "design.fillet"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Round edges", "design.fillet"))
            .forModelTool(ModelTool::Fillet)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.chamfer", Kind::Tool, "design")
            .withIcon("draw-line", "SP_DialogOkButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Chamfer", "design.chamfer"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Bevel edges", "design.chamfer"))
            .forModelTool(ModelTool::Chamfer)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("group.design.hole", Kind::Group, "design")
            .withIcon("draw-circle", "SP_DialogDiscardButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Simple Hole", "group.design.hole"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Simple\nHole", "group.design.hole"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a simple hole",
                "group.design.hole")));
    add(cmd("design.hole", Kind::Tool, "design")
            .withIcon("draw-circle", "SP_DialogDiscardButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Simple Hole", "design.hole"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Simple\nHole", "design.hole"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a simple hole",
                "design.hole"))
            .forModelTool(ModelTool::SimpleHole)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.counterbore", Kind::Tool, "design")
            .withIcon("draw-ellipse", "SP_DialogNoButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Counterbore", "design.counterbore"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Counter-\nbore",
                "design.counterbore"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a counterbore hole",
                "design.counterbore"))
            .forModelTool(ModelTool::Counterbore)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.countersink", Kind::Tool, "design")
            .withIcon("draw-polygon", "SP_DialogYesButton")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Countersink", "design.countersink"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Counter-\nsink",
                "design.countersink"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a countersink hole",
                "design.countersink"))
            .forModelTool(ModelTool::Countersink)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.threadedHole", Kind::Tool, "design")
            .withIcon("draw-spiral", "SP_DialogSaveButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Threaded Hole",
                "design.threadedHole"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Threaded\nHole",
                "design.threadedHole"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create a threaded hole",
                "design.threadedHole"))
            .forModelTool(ModelTool::ThreadedHole)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("group.design.move", Kind::Group, "design")
            .withIcon("transform-move", "SP_ArrowRight")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Move", "group.design.move"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Transform objects",
                "group.design.move")));
    add(cmd("design.move", Kind::Tool, "design")
            .withIcon("transform-move", "SP_ArrowRight")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Move/Copy", "design.move"))
            .withToolbarText(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Move/\nCopy", "design.move"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Move or copy objects",
                "design.move"))
            .forModelTool(ModelTool::MoveCopy)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.align", Kind::Tool, "design")
            .withIcon("align-horizontal-center", "SP_ToolBarHorizontalExtensionButton")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Align", "design.align"))
            .withTooltip(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Align objects", "design.align"))
            .forModelTool(ModelTool::Align)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("group.design.mirror", Kind::Group, "design")
            .withIcon("object-flip-horizontal", "SP_ArrowBack")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Mirror", "group.design.mirror"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Mirror or pattern objects",
                "group.design.mirror")));
    add(cmd("design.mirror", Kind::Tool, "design")
            .withIcon("object-flip-horizontal", "SP_ArrowBack")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Mirror", "design.mirror"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Mirror bodies or features",
                "design.mirror"))
            .forModelTool(ModelTool::Mirror)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.pattern", Kind::Tool, "design")
            .withIcon("edit-copy", "SP_FileDialogDetailedView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Pattern", "design.pattern"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Create rectangular or circular pattern",
                "design.pattern"))
            .forModelTool(ModelTool::Pattern)
            .needing(RequiresViewport | NotImplemented));
    add(cmd("group.design.params", Kind::Group, "design")
            .withIcon("document-properties", "SP_FileDialogInfoView")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Params", "group.design.params"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Manage parameters",
                "group.design.params")));
    add(cmd("design.parameters", Kind::Tool, "design")
            .withIcon("document-properties", "SP_FileDialogInfoView")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Change Parameters",
                "design.parameters"))
            .withToolbarText(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Change\nParameters",
                "design.parameters"))
            .withTooltip(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Edit document parameters",
                "design.parameters"))
            .forModelTool(ModelTool::Parameters));
    add(cmd("design.joint", Kind::Action, "design")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Joint", "design.joint"))
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.measure", Kind::Action, "design")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Measure", "design.measure"))
            .needing(RequiresViewport | NotImplemented));
    add(cmd("design.toggleVisibility", Kind::Action, "design")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Toggle Visibility",
                "design.toggleVisibility"))
            .needing(RequiresViewport | NotImplemented));
    add(cmd("nav.rotateUp", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Up (continuous)",
                "nav.rotateUp"))
            .needing(RequiresViewport));
    add(cmd("nav.rotateDown", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Rotate Down (continuous)",
                "nav.rotateDown"))
            .needing(RequiresViewport));
    add(cmd("nav.axisX", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Set Rotation Axis to X",
                "nav.axisX"))
            .needing(RequiresViewport));
    add(cmd("nav.axisY", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Set Rotation Axis to Y",
                "nav.axisY"))
            .needing(RequiresViewport));
    add(cmd("nav.axisZ", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Set Rotation Axis to Z",
                "nav.axisZ"))
            .needing(RequiresViewport));
    add(cmd("nav.rotateLeft", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Snap Rotate Left 90\xC2\xB0",
                "nav.rotateLeft"))
            .needing(RequiresViewport));
    add(cmd("nav.rotateRight", Kind::Input, "nav")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3(
                "hobbycad::Commands",
                "Snap Rotate Right 90\xC2\xB0",
                "nav.rotateRight"))
            .needing(RequiresViewport));
    add(cmd("viewport.rotate", Kind::Input, "viewport")
            .withLabel(
                HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Rotate View", "viewport.rotate"))
            .needing(RequiresViewport));
    add(cmd("viewport.pan", Kind::Input, "viewport")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Pan View", "viewport.pan"))
            .needing(RequiresViewport));
    add(cmd("viewport.zoom", Kind::Input, "viewport")
            .withLabel(HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Zoom View", "viewport.zoom"))
            .needing(RequiresViewport));

    return all;
}

}  // namespace

const char* commandContext()
{
    return "hobbycad::Commands";
}

const std::vector<Command>& allCommands()
{
    static const std::vector<Command> all = buildCommands();
    return all;
}

const Command* findCommand(const std::string& id)
{
    static const std::unordered_map<std::string, const Command*> index = [] {
        std::unordered_map<std::string, const Command*> m;
        for (const Command& c : allCommands()) m.emplace(c.id, &c);
        return m;
    }();
    const auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

const Command* commandForTool(SketchTool tool)
{
    for (const Command& c : allCommands()) {
        if (c.kind == Kind::Tool && !c.variant && c.modelTool == ModelTool::None
            && c.sketchTool == tool && std::string(c.context) == "sketch") {
            return &c;
        }
    }
    return nullptr;
}

const Command* commandForMode(SketchTool tool, CreationMode mode)
{
    for (const Command& c : allCommands()) {
        if (c.variant && c.sketchTool == tool && c.mode == mode) return &c;
    }
    return nullptr;
}

const Command* commandForModelTool(ModelTool tool)
{
    if (tool == ModelTool::None) return nullptr;
    for (const Command& c : allCommands()) {
        if (c.kind == Kind::Tool && c.modelTool == tool) return &c;
    }
    return nullptr;
}

const std::vector<BindingContext>& bindingContexts()
{
    static const std::vector<BindingContext> contexts = {
        {"global",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Global", "context.global"),
         BindingScope::Global, ""},
        {"file",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "File", "context.file"),
         BindingScope::Application, ""},
        {"edit",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Edit", "context.edit"),
         BindingScope::Application, ""},
        {"view",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "View", "context.view"),
         BindingScope::Application, ""},
        {"construct",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Construct", "context.construct"),
         BindingScope::Application, ""},
        {"sketch",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Sketch", "context.sketch"),
         BindingScope::Surface, "sketchCanvas"},
        {"design",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Design", "context.design"),
         BindingScope::Surface, "viewport"},
        {"nav",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Navigation", "context.nav"),
         BindingScope::Surface, "viewport"},
        {"viewport",
         HOBBYCAD_TRANSLATE_NOOP3("hobbycad::Commands", "Viewport", "context.viewport"),
         BindingScope::Surface, "viewport"},
    };
    return contexts;
}

const BindingContext* findBindingContext(const std::string& id)
{
    for (const BindingContext& b : bindingContexts()) {
        if (id == b.id) return &b;
    }
    return nullptr;
}

bool commandAvailable(const Command& command, const UiState& state)
{
    const unsigned n = command.needs;
    if (n & NotImplemented) return false;
    if ((n & RequiresSketch) && !state.sketchOpen) return false;
    if ((n & RequiresViewport) && !state.viewport) return false;
    if ((n & RequiresSelection) && !state.hasSelection) return false;
    return true;
}

std::string stripMnemonic(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            if (i + 1 < text.size() && text[i + 1] == '&') {
                out.push_back('&');
                ++i;
            }
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

}  // namespace commands
}  // namespace hobbycad
