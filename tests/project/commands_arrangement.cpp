// =====================================================================
//  tests/project/commands_arrangement.cpp — command registry, default
//  arrangement and binding rules
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The toolbar, the menus, the bindings dialog and the canvas each kept
//  their own list of tools, texts and keys, and the lists disagreed: a
//  tooltip promised T for Text while T ran Trim, and X for Constraint
//  while X toggled construction. They now read one registry
//  (hobbycad/commands.h) and one default arrangement
//  (hobbycad/layout/arrangement.h). These checks hold the two together.
// =====================================================================
#include <hobbycad/bindings.h>
#include <hobbycad/commands.h>
#include <hobbycad/layout/arrangement.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::commands;
using namespace hobbycad::layout;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static bool sameText(const Text& t, const char* id)
{
    return t.empty() || (t.disambiguation && std::strcmp(t.disambiguation, id) == 0);
}

/// A bindings table from the default arrangement, with platform keys
/// replaced by stand-ins that cannot collide with anything spelled out.
static bindings::Table defaultTable()
{
    bindings::Table table;
    for (const auto& b : defaultArrangement().defaultBindings()) {
        bindings::Slots slots = b.second;
        for (std::string& s : slots) {
            const std::string name = bindings::standardKeyName(s);
            if (!name.empty()) s = "Std+" + name;
        }
        table.addCommand(b.first, slots);
    }
    return table;
}

/// Every shown element of a container, as ids, separators included.
static std::vector<std::string> shown(const Arrangement& a, const std::string& container)
{
    std::vector<std::string> out;
    for (const Element* e : a.children(container)) out.push_back(e->id);
    return out;
}

int main()
{
    std::printf("command registry, default arrangement and bindings\n");
    const Arrangement& arr = defaultArrangement();

    // ---- registry ---------------------------------------------------------
    {
        std::set<std::string> ids;
        bool unique = true, labeled = true, disambiguated = true, known = true;
        bool noKeyInTips = true;
        for (const Command& c : allCommands()) {
            unique = ids.insert(c.id).second && unique;
            labeled = labeled && !c.label.empty();
            disambiguated = disambiguated && sameText(c.label, c.id)
                            && sameText(c.menuText, c.id) && sameText(c.toolbarText, c.id)
                            && sameText(c.tooltip, c.id);
            known = known && findBindingContext(c.context) != nullptr;
            // A key written into a tooltip goes stale when the binding
            // changes; the front end appends the bound key instead.
            if (!c.tooltip.empty()) {
                const std::string tip = c.tooltip.source;
                const std::size_t n = tip.size();
                if (n >= 3 && tip[n - 1] == ')' && tip[n - 3] == '(') noKeyInTips = false;
            }
        }
        check(unique, "command ids are unique");
        check(labeled, "every command has a label");
        check(disambiguated, "every command text is disambiguated by its command id");
        check(known, "every command belongs to a known binding context");
        check(noKeyInTips, "no tooltip spells out a key");
        check(findCommand("sketch.line") && !findCommand("sketch.nothing"),
              "findCommand finds known ids only");
    }

    // ---- tools and modes round-trip ----------------------------------------
    {
        bool allTools = true;
        for (int t = static_cast<int>(SketchTool::Select);
             t <= static_cast<int>(SketchTool::Project); ++t) {
            const Command* c = commandForTool(static_cast<SketchTool>(t));
            allTools = allTools && c && c->sketchTool == static_cast<SketchTool>(t)
                       && !c->variant;
        }
        check(allTools, "every sketch tool has a command");

        bool allVariants = true;
        int variants = 0;
        for (const Command& c : allCommands()) {
            if (!c.variant) continue;
            ++variants;
            allVariants = allVariants && c.hasMode
                          && commandForMode(c.sketchTool, c.mode) == &c;
        }
        check(variants == 33 && allVariants, "every variant is found by its tool and mode");

        const Command* ellipse = commandForTool(SketchTool::Ellipse);
        check(ellipse && std::strcmp(ellipse->id, "sketch.ellipse") == 0 && ellipse->hasMode
                  && ellipse->mode == CreationMode::EllipseCenterAxes,
              "the Ellipse command opens in Center + Axes and is not a variant");
        // Modes restart at 0 per tool: Line's and Circle's first modes share
        // a value and must still resolve to their own commands.
        const Command* l0 = commandForMode(SketchTool::Line, CreationMode::LineTwoPoint);
        const Command* c0 = commandForMode(SketchTool::Circle,
                                           CreationMode::CircleCenterRadius);
        check(l0 && c0 && std::strcmp(l0->id, "sketch.line.twoPoint") == 0
                  && std::strcmp(c0->id, "sketch.circle.centerRadius") == 0,
              "a mode resolves only together with its tool");

        bool allModel = true;
        for (int t = 1; t < static_cast<int>(ModelTool::_Count); ++t) {
            const Command* c = commandForModelTool(static_cast<ModelTool>(t));
            allModel = allModel && c && c->modelTool == static_cast<ModelTool>(t);
        }
        check(allModel && !commandForModelTool(ModelTool::None),
              "every model tool has a command");
    }

    // ---- availability and mnemonics ----------------------------------------
    {
        const Command* line = findCommand("sketch.line");
        const Command* cut = findCommand("edit.cut");
        const Command* box = findCommand("design.box");
        UiState none;
        UiState sketching;
        sketching.sketchOpen = true;
        UiState everything;
        everything.sketchOpen = everything.viewport = everything.hasSelection = true;
        check(line && !commandAvailable(*line, none) && commandAvailable(*line, sketching),
              "a sketch tool needs an open sketch");
        check(cut && !commandAvailable(*cut, sketching) && commandAvailable(*cut, everything),
              "cut needs a selection");
        check(box && !commandAvailable(*box, everything),
              "an unimplemented command is never available");
        check(stripMnemonic("Save &As...") == "Save As..."
                  && stripMnemonic("Tom && Jerry") == "Tom & Jerry"
                  && stripMnemonic("Plain") == "Plain",
              "stripMnemonic removes markers and keeps a doubled ampersand");
    }

    // ---- the default arrangement ---------------------------------------------
    {
        const std::vector<std::string> problems = arr.problems();
        for (const std::string& p : problems) std::printf("         %s\n", p.c_str());
        check(problems.empty(), "the default arrangement has no problems");

        check(shown(arr, kMenuBar)
                  == std::vector<std::string>{"menu.file", "menu.edit", "menu.construct",
                                              "menu.view", "menu.sketch",
                                              "menu.constraints", "menu.help"},
              "the menu bar keeps its order");
        check(shown(arr, "menu.file")
                  == std::vector<std::string>{"file.new", "file.open", "separator",
                                              "file.save", "file.saveAs", "separator",
                                              "file.close", "separator",
                                              "menu.file.import", "menu.file.export",
                                              "separator", "file.quit"},
              "the File menu keeps its order and separators");

        bool titled = true;
        for (const Command& c : allCommands()) {
            if (c.kind != Kind::Menu && c.kind != Kind::Group) continue;
            const Container* k = arr.container(c.id);
            titled = titled && k && k->title == c.id
                     && (k->kind == ContainerKind::ToolGroup) == (c.kind == Kind::Group);
        }
        check(titled, "every menu and group command titles its container");

        // Every sketch tool but Select is reachable from the sketch toolbar,
        // and every variant sits under its own tool.
        bool reachable = true, underOwnTool = true;
        for (const Command& c : allCommands()) {
            if (c.kind != Kind::Tool || std::strcmp(c.context, "sketch") != 0) continue;
            if (c.sketchTool == SketchTool::Select) continue;
            const Container* g = arr.groupOf(c.id);
            reachable = reachable && g && arr.element(std::string(kSketchToolbar) + "/" + g->id);
            if (c.variant) {
                const Command* tool = commandForTool(c.sketchTool);
                const Container* v = tool ? arr.variantsOf(tool->id) : nullptr;
                underOwnTool = underOwnTool && v
                               && arr.element(v->id + "/" + c.id) != nullptr;
            }
        }
        check(reachable, "every sketch tool is on the sketch toolbar");
        check(underOwnTool, "every variant is listed under its own tool");

        bool activatesMember = true;
        for (const Container& k : arr.containers()) {
            if (k.kind != ContainerKind::ToolGroup || k.activates.empty()) continue;
            activatesMember = activatesMember && arr.element(k.id + "/" + k.activates);
        }
        const Container* create = arr.container("group.sketch.create");
        check(activatesMember && create && create->activates.empty(),
              "a group's default tool is in the group; Create opens its list first");

        bool modelReachable = true;
        for (int t = 1; t < static_cast<int>(ModelTool::_Count); ++t) {
            const Command* c = commandForModelTool(static_cast<ModelTool>(t));
            const Container* g = c ? arr.groupOf(c->id) : nullptr;
            modelReachable = modelReachable && g
                             && arr.element(std::string(kModelToolbar) + "/" + g->id);
        }
        check(modelReachable, "every model tool is on the model toolbar");

        check(shown(arr, "menu.view.language")
                  == std::vector<std::string>{kLanguagesPlaceholder},
              "the language menu is a placeholder the front end fills");
    }

    // ---- sort order ---------------------------------------------------------
    {
        Arrangement a = defaultArrangement();
        // Swapping two neighbors changes one element's number only.
        Element* redo = a.element("menu.edit/edit.redo");
        const Element* undo = a.element("menu.edit/edit.undo");
        if (redo && undo) redo->order = undo->order / 2.0;
        const std::vector<std::string> got = shown(a, "menu.edit");
        check(got.size() > 2 && got[0] == "edit.redo" && got[1] == "edit.undo",
              "a lower number moves an element before its neighbor");

        Element* cut = a.element("menu.edit/edit.cut");
        if (cut) cut->hidden = true;
        const std::vector<std::string> hidden = shown(a, "menu.edit");
        bool gone = true;
        for (const std::string& id : hidden) gone = gone && id != "edit.cut";
        check(gone && a.children("menu.edit", true).size() == hidden.size() + 1,
              "a hidden element is left out unless asked for");

        // Equal numbers keep the order the elements were added in, which
        // here is not the order of their ids.
        Arrangement t;
        t.addContainer({"menu.t", ContainerKind::Menu, "", ""});
        t.addElement(ElementKind::Command, "edit.cut", "menu.t", 5.0);
        t.addElement(ElementKind::Command, "edit.copy", "menu.t", 5.0);
        t.addElement(ElementKind::Command, "edit.paste", "menu.t", 1.5);
        check(shown(t, "menu.t")
                  == std::vector<std::string>{"edit.paste", "edit.cut", "edit.copy"},
              "fractional numbers sort, and ties keep insertion order");
    }

    // ---- structural problems ------------------------------------------------
    {
        const auto nested = [](int levels) {
            Arrangement a;
            std::string parent = "m0";
            a.addContainer({parent, ContainerKind::Menu, "", ""});
            for (int i = 1; i < levels; ++i) {
                const std::string id = "m" + std::to_string(i);
                a.addContainer({id, ContainerKind::Menu, "", ""});
                a.append(ElementKind::Container, id, parent);
                parent = id;
            }
            return a.problems();
        };
        check(nested(kMaxDepth).empty(), "nesting 16 deep is accepted");
        check(!nested(kMaxDepth + 1).empty(), "nesting 17 deep is reported");

        Arrangement twice;
        twice.addContainer({"a", ContainerKind::Menu, "", ""});
        twice.addContainer({"b", ContainerKind::Menu, "", ""});
        twice.addContainer({"c", ContainerKind::Menu, "", ""});
        twice.append(ElementKind::Container, "c", "a");
        twice.append(ElementKind::Container, "c", "b");
        check(!twice.problems().empty(), "a container shown in two places is reported");

        Arrangement loop;
        loop.addContainer({"a", ContainerKind::Menu, "", ""});
        loop.addContainer({"b", ContainerKind::Menu, "", ""});
        loop.append(ElementKind::Container, "b", "a");
        loop.append(ElementKind::Container, "a", "b");
        check(!loop.problems().empty(), "a container inside itself is reported");

        Arrangement unknown;
        unknown.addContainer({"a", ContainerKind::Menu, "no.such.title", ""});
        unknown.append(ElementKind::Command, "no.such.command", "a");
        unknown.append(ElementKind::Command, "edit.cut", "no.such.container");
        check(unknown.problems().size() == 3,
              "unknown commands, titles and containers are each reported");
    }

    // ---- binding strings ------------------------------------------------------
    {
        using bindings::InputKind;
        check(bindings::classify("") == InputKind::None
                  && bindings::classify("Ctrl+S") == InputKind::Keyboard
                  && bindings::classify("RightButton+Drag") == InputKind::Mouse
                  && bindings::classify("Wheel") == InputKind::Mouse
                  && bindings::classify("Ctrl+Click") == InputKind::Mouse,
              "bindings are classified as keys or mouse gestures");
        check(bindings::splitAlternatives("Ctrl+Shift+Z, Ctrl+Y")
                  == std::vector<std::string>{"Ctrl+Shift+Z", "Ctrl+Y"},
              "alternatives split on commas");
        check(bindings::splitAlternatives("Ctrl+,") == std::vector<std::string>{"Ctrl+,"}
                  && bindings::splitAlternatives(",") == std::vector<std::string>{","}
                  && bindings::splitAlternatives("Ctrl+,,Ctrl+Y")
                         == std::vector<std::string>{"Ctrl+,", "Ctrl+Y"},
              "a comma that is the key itself is kept");
        check(bindings::keySequences({"Ctrl+Shift+Z,Ctrl+Y", "MiddleButton+Drag", "F2"})
                  == std::vector<std::string>{"Ctrl+Shift+Z", "Ctrl+Y", "F2"},
              "keySequences skips mouse gestures");
        check(bindings::standardKeyName("std:Save") == "Save"
                  && bindings::standardKeyName("S").empty(),
              "platform keys are recognized by their prefix");
    }

    // ---- binding table -------------------------------------------------------
    {
        bindings::Table table = defaultTable();
        const auto pairs = table.conflicts();
        for (const auto& p : pairs) {
            std::printf("         %s <-> %s\n", p.first.c_str(), p.second.c_str());
        }
        check(pairs.empty(), "the default bindings do not collide");

        bool known = true;
        for (const std::string& id : table.commandIds()) known = known && findCommand(id);
        check(known, "every default binding names a known command");

        // A key reaches one surface at a time.
        check(table.findConflict("sketch.construction", "X").empty(),
              "X on the canvas and X in the 3D view do not collide");
        check(table.findConflict("sketch.rotateCW", "E").empty(),
              "E on the canvas and E for Extrude do not collide");
        check(table.findConflict("design.move", "Up") == "nav.rotateUp",
              "two commands of the 3D view collide");
        check(table.findConflict("sketch.line", "Ctrl+R") == "view.project",
              "a window-wide shortcut collides with a canvas key");
        check(table.findConflict("sketch.line", "/") == "global.commandSearch",
              "a global key collides with everything");
        check(table.findConflict("sketch.line", "Q, Ctrl+Y") == "edit.redo",
              "each alternative is checked against each alternative");
        check(table.findConflict("sketch.line", "L").empty(),
              "a command does not collide with itself");

        check(table.commandForKey("sketch", "L") == "sketch.line"
                  && table.commandForKey("sketch", "T") == "sketch.trim"
                  && table.commandForKey("sketch", "Up").empty()
                  && table.commandForKey("nav", "E") == "design.extrude"
                  && table.commandForKey("sketch", "Ctrl+R").empty(),
              "a key finds the command heard where it was pressed");

        check(table.set("sketch.line", 1, "Shift+L") && !table.isDefault("sketch.line", 1)
                  && table.commandForKey("sketch", "Shift+L") == "sketch.line",
              "a changed slot is used and is no longer the default");
        table.restore("sketch.line");
        check(table.isDefault("sketch.line", 1)
                  && table.commandForKey("sketch", "Shift+L").empty(),
              "restoring a command brings its defaults back");
        check(!table.set("sketch.line", 3, "K") && !table.set("no.such", 0, "K"),
              "an unknown slot or command is refused");
        table.set("edit.undo", 0, "");
        table.set("sketch.arc", 0, "K");
        table.restoreAll();
        check(table.isDefault("edit.undo", 0) && table.isDefault("sketch.arc", 0),
              "restoring everything brings every default back");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
