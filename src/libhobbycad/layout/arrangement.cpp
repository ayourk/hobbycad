// =====================================================================
//  src/libhobbycad/layout/arrangement.cpp — where HobbyCAD puts things
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/layout/arrangement.h>

#include <hobbycad/commands.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <unordered_set>

namespace hobbycad {
namespace layout {

namespace {

/// Spacing between neighbors in a default container.
constexpr double kStep = 100.0;

}  // namespace

// ---- Arrangement ------------------------------------------------------

void Arrangement::addContainer(const Container& container)
{
    if (m_containerIndex.count(container.id)) return;
    m_containerIndex.emplace(container.id, m_containers.size());
    m_containers.push_back(container);
}

std::string Arrangement::append(ElementKind kind, const std::string& id,
                                const std::string& container)
{
    double last = 0.0;
    for (const Element& e : m_elements) {
        if (e.container == container) last = std::max(last, e.order);
    }
    return addElement(kind, id, container, last + kStep);
}

std::string Arrangement::addElement(ElementKind kind, const std::string& id,
                                    const std::string& container, double order)
{
    Element e;
    e.kind = kind;
    e.container = container;
    e.order = order;
    if (kind == ElementKind::Separator) {
        const int n = ++m_separatorCount[container];
        e.id = "separator";
        e.key = container + "/separator." + std::to_string(n);
    } else {
        e.id = id;
        e.key = container + "/" + id;
    }
    if (m_elementIndex.count(e.key)) return e.key;
    m_elementIndex.emplace(e.key, m_elements.size());
    m_elements.push_back(e);
    return e.key;
}

bool Arrangement::addKeyedElement(const std::string& key, ElementKind kind,
                                  const std::string& id, const std::string& container,
                                  double order)
{
    if (key.empty() || m_elementIndex.count(key)) return false;
    Element element;
    element.key = key;
    element.id = id;
    element.kind = kind;
    element.container = container;
    element.order = order;
    m_elementIndex.emplace(key, m_elements.size());
    m_elements.push_back(element);
    return true;
}

const Container* Arrangement::container(const std::string& id) const
{
    const auto it = m_containerIndex.find(id);
    return it == m_containerIndex.end() ? nullptr : &m_containers[it->second];
}

const Element* Arrangement::element(const std::string& key) const
{
    const auto it = m_elementIndex.find(key);
    return it == m_elementIndex.end() ? nullptr : &m_elements[it->second];
}

Element* Arrangement::element(const std::string& key)
{
    const auto it = m_elementIndex.find(key);
    return it == m_elementIndex.end() ? nullptr : &m_elements[it->second];
}

std::vector<const Element*> Arrangement::children(const std::string& containerId,
                                                  bool includeHidden) const
{
    // Elements are kept in the order they were added, so a stable sort by
    // number alone breaks ties by that order.
    std::vector<const Element*> out;
    for (const Element& e : m_elements) {
        if (e.container != containerId) continue;
        if (e.hidden && !includeHidden) continue;
        out.push_back(&e);
    }
    std::stable_sort(out.begin(), out.end(), [](const Element* a, const Element* b) {
        return a->order < b->order;
    });
    return out;
}

std::vector<std::string> Arrangement::commandIds(const std::string& containerId) const
{
    std::vector<std::string> out;
    for (const Element* e : children(containerId)) {
        if (e->kind == ElementKind::Command) out.push_back(e->id);
    }
    return out;
}

const Container* Arrangement::variantsOf(const std::string& commandId) const
{
    return container(kVariantsPrefix + commandId);
}

const Container* Arrangement::groupOf(const std::string& commandId) const
{
    for (const Element& e : m_elements) {
        if (e.kind != ElementKind::Command || e.id != commandId) continue;
        const Container* c = container(e.container);
        if (!c) continue;
        if (c->kind == ContainerKind::ToolGroup) return c;
        if (c->kind == ContainerKind::Variants) {
            const std::string owner = c->id.substr(std::char_traits<char>::length(kVariantsPrefix));
            if (const Container* g = groupOf(owner)) return g;
        }
    }
    return nullptr;
}

void Arrangement::addDefaultBinding(const std::string& commandId,
                                    const bindings::Slots& slots)
{
    for (auto& b : m_bindings) {
        if (b.first == commandId) {
            b.second = slots;
            return;
        }
    }
    m_bindings.emplace_back(commandId, slots);
}

std::vector<std::string> Arrangement::problems() const
{
    std::vector<std::string> out;

    // Where each container is shown; a container shown twice would be
    // built twice, and one shown inside itself never finishes.
    std::unordered_map<std::string, std::string> parentOf;
    for (const Element& e : m_elements) {
        if (!container(e.container)) {
            out.push_back("element " + e.key + " is in unknown container " + e.container);
        }
        if (!std::isfinite(e.order)) {
            out.push_back("element " + e.key + " has no usable sort order");
        }
        switch (e.kind) {
        case ElementKind::Command:
            if (!commands::findCommand(e.id)) {
                out.push_back("element " + e.key + " names unknown command " + e.id);
            }
            break;
        case ElementKind::Container:
            if (!container(e.id)) {
                out.push_back("element " + e.key + " names unknown container " + e.id);
            } else if (parentOf.count(e.id)) {
                out.push_back("container " + e.id + " is shown in both " + parentOf[e.id]
                              + " and " + e.container);
            } else {
                parentOf.emplace(e.id, e.container);
            }
            break;
        case ElementKind::Separator:
        case ElementKind::Placeholder:
            break;
        }
    }
    for (const Container& c : m_containers) {
        if (!c.title.empty() && !commands::findCommand(c.title)) {
            out.push_back("container " + c.id + " is titled by unknown command " + c.title);
        }
        if (!c.activates.empty() && !commands::findCommand(c.activates)) {
            out.push_back("container " + c.id + " activates unknown command " + c.activates);
        }
        // A variants container hangs off its tool, not off a parent
        // element, so its depth is its tool's container's depth plus one.
        std::string at = c.id;
        int depth = 1;
        std::unordered_set<std::string> seen{at};
        for (;;) {
            std::string up;
            const auto p = parentOf.find(at);
            if (p != parentOf.end()) {
                up = p->second;
            } else if (at.compare(0, std::char_traits<char>::length(kVariantsPrefix),
                                  kVariantsPrefix) == 0) {
                const std::string owner =
                    at.substr(std::char_traits<char>::length(kVariantsPrefix));
                for (const Element& e : m_elements) {
                    if (e.kind == ElementKind::Command && e.id == owner) {
                        up = e.container;
                        break;
                    }
                }
            }
            if (up.empty()) break;
            if (!seen.insert(up).second) {
                out.push_back("container " + c.id + " is inside itself");
                break;
            }
            at = up;
            if (++depth > kMaxDepth) {
                out.push_back("container " + c.id + " is nested deeper than "
                              + std::to_string(kMaxDepth));
                break;
            }
        }
    }
    return out;
}

// ---- The built-in default -------------------------------------------

namespace {

/// Writes one container's contents in order.
class Filler {
public:
    Filler(Arrangement& a, std::string container)
        : m_a(a), m_container(std::move(container)) {}

    Filler& commands(std::initializer_list<const char*> ids)
    {
        for (const char* id : ids) m_a.append(ElementKind::Command, id, m_container);
        return *this;
    }
    Filler& separator()
    {
        m_a.append(ElementKind::Separator, std::string(), m_container);
        return *this;
    }
    Filler& sub(const std::string& id)
    {
        m_a.append(ElementKind::Container, id, m_container);
        return *this;
    }
    Filler& placeholder(const char* id)
    {
        m_a.append(ElementKind::Placeholder, id, m_container);
        return *this;
    }

private:
    Arrangement& m_a;
    std::string m_container;
};

void addMenus(Arrangement& a)
{
    a.addContainer({kMenuBar, ContainerKind::MenuBar, "", ""});
    const auto menu = [&a](const char* id) {
        a.addContainer({id, ContainerKind::Menu, id, ""});
        return Filler(a, id);
    };

    Filler(a, kMenuBar)
        .sub("menu.file").sub("menu.edit").sub("menu.construct").sub("menu.view")
        .sub("menu.sketch").sub("menu.constraints").sub("menu.help");

    menu("menu.file")
        .commands({"file.new", "file.open"}).separator()
        .commands({"file.save", "file.saveAs"}).separator()
        .commands({"file.close"}).separator()
        .sub("menu.file.import").sub("menu.file.export").separator()
        .commands({"file.quit"});
    menu("menu.file.import").commands({"file.import.step", "file.import.dxf"});
    menu("menu.file.export")
        .commands({"file.export.step", "file.export.stl"}).separator()
        .commands({"file.export.dxf", "file.export.svg"});

    menu("menu.edit")
        .commands({"edit.undo", "edit.redo"}).separator()
        .commands({"edit.cut", "edit.copy", "edit.paste", "edit.delete"}).separator()
        .commands({"edit.selectAll"});

    menu("menu.construct").commands({"construct.plane"});

    menu("menu.view")
        .commands({"view.terminal", "view.project", "view.properties", "view.toolbar",
                   "view.changelog", "view.drawThenConstrain", "view.showUnconstrained",
                   "view.showConstraints", "view.showDimensions", "view.showProfiles",
                   "view.fitSketch"})
        .separator()
        .sub("menu.view.workspace")
        .separator()
        .commands({"view.resetView", "view.lookAt", "view.slice", "view.rotateLeft",
                   "view.rotateRight"})
        .separator()
        .commands({"view.showGrid", "view.snapToGrid"})
        .separator()
        .commands({"view.zUpOrientation", "view.orbitSelected"})
        .separator()
        .sub("menu.view.theme").sub("menu.view.selectionFilter")
        .separator()
        .sub("menu.view.language")
        .commands({"view.customize", "view.preferences"});
    menu("menu.view.workspace")
        .commands({"view.workspace.design", "view.workspace.render",
                   "view.workspace.animation", "view.workspace.simulation"});
    menu("menu.view.theme")
        .commands({"view.theme.light", "view.theme.dark"}).separator()
        .commands({"view.theme.edit"});
    menu("menu.view.selectionFilter")
        .commands({"view.filter.all", "view.filter.points", "view.filter.curves"});
    menu("menu.view.language").placeholder(kLanguagesPlaceholder);

    menu("menu.sketch")
        .commands({"sketch.line", "sketch.rectangle", "sketch.circle", "sketch.arc",
                   "sketch.ellipse", "sketch.point"})
        .separator()
        .commands({"sketch.spline.cubicBezier", "sketch.spline.catmullRom",
                   "sketch.spline.rational", "sketch.spline.conic"})
        .separator()
        .commands({"sketch.curvatureComb"})
        .separator()
        .commands({"sketch.finish"});

    menu("menu.constraints")
        .commands({"sketch.constrain.coincident", "sketch.constrain.horizontal",
                   "sketch.constrain.vertical", "sketch.constrain.parallel",
                   "sketch.constrain.perpendicular", "sketch.constrain.tangent",
                   "sketch.constrain.curvature", "sketch.constrain.equal",
                   "sketch.constrain.midpoint", "sketch.constrain.concentric",
                   "sketch.constrain.collinear", "sketch.constrain.pointOnSpline",
                   "sketch.constrain.tangentAngle", "sketch.constrain.angleTwoLines",
                   "sketch.constrain.symmetric"})
        .separator()
        .commands({"sketch.constrain.fix"})
        .separator()
        .commands({"sketch.constrain.auto"});

    menu("menu.help").commands({"help.about"});
}

/// A tool group on a toolbar: `activates` is what a click runs before
/// anything was picked ("" = the click opens the list).
Filler group(Arrangement& a, const char* id, const char* activates)
{
    a.addContainer({id, ContainerKind::ToolGroup, id, activates});
    return Filler(a, id);
}

/// The variants offered under a tool.
void variants(Arrangement& a, const std::string& tool,
              std::initializer_list<const char*> modes)
{
    const std::string id = kVariantsPrefix + tool;
    a.addContainer({id, ContainerKind::Variants, "", ""});
    Filler(a, id).commands(modes);
}

void addSketchToolbar(Arrangement& a)
{
    a.addContainer({kSketchToolbar, ContainerKind::Toolbar, "", ""});
    Filler(a, kSketchToolbar)
        .sub("group.sketch.create").separator()
        .sub("group.sketch.constrain").separator()
        .sub("group.sketch.modify").separator()
        .sub("group.sketch.pattern").separator()
        .commands({"sketch.toggle3d", "sketch.flip"}).separator()
        .commands({"sketch.finish"});

    group(a, "group.sketch.create", "")
        .commands({"sketch.line", "sketch.rectangle", "sketch.circle", "sketch.arc",
                   "sketch.spline", "sketch.polygon", "sketch.slot", "sketch.ellipse",
                   "sketch.point"});
    variants(a, "sketch.line",
             {"sketch.line.twoPoint", "sketch.line.tangent", "sketch.line.construction"});
    variants(a, "sketch.rectangle",
             {"sketch.rectangle.corner", "sketch.rectangle.center",
              "sketch.rectangle.threePoint", "sketch.rectangle.parallelogram"});
    variants(a, "sketch.circle",
             {"sketch.circle.centerRadius", "sketch.circle.twoPoint",
              "sketch.circle.threePoint", "sketch.circle.twoTangent",
              "sketch.circle.threeTangent"});
    variants(a, "sketch.arc",
             {"sketch.arc.centerStartEnd", "sketch.arc.startEndRadius", "sketch.arc.tangent",
              "sketch.arc.threePoint"});
    variants(a, "sketch.spline",
             {"sketch.spline.cubicBezier", "sketch.spline.catmullRom",
              "sketch.spline.rational", "sketch.spline.conic"});
    variants(a, "sketch.polygon",
             {"sketch.polygon.inscribed", "sketch.polygon.circumscribed",
              "sketch.polygon.freeform"});
    variants(a, "sketch.slot",
             {"sketch.slot.centerToCenter", "sketch.slot.overall", "sketch.slot.arcRadius",
              "sketch.slot.arcEnds"});
    variants(a, "sketch.ellipse",
             {"sketch.ellipse.centerAxes", "sketch.ellipse.threePoint", "sketch.ellipse.arc",
              "sketch.ellipse.spanRise", "sketch.ellipse.corner", "sketch.ellipse.endpoints"});

    group(a, "group.sketch.constrain", "sketch.dimension")
        .commands({"sketch.dimension", "sketch.constraint", "sketch.text"});
    group(a, "group.sketch.modify", "sketch.trim")
        .commands({"sketch.trim", "sketch.extend", "sketch.split", "sketch.offset",
                   "sketch.fillet", "sketch.chamfer", "sketch.transform.move",
                   "sketch.transform.rotate", "sketch.transform.scale",
                   "sketch.transform.mirror", "sketch.transform.copy"});
    group(a, "group.sketch.pattern", "sketch.rectPattern")
        .commands({"sketch.rectPattern", "sketch.circPattern", "sketch.project"});
}

void addModelToolbar(Arrangement& a)
{
    a.addContainer({kModelToolbar, ContainerKind::Toolbar, "", ""});
    Filler(a, kModelToolbar)
        .sub("group.design.sketch").sub("group.design.plane").separator()
        .sub("group.design.solid").separator()
        .sub("group.design.fillet").sub("group.design.hole").separator()
        .sub("group.design.move").sub("group.design.mirror").separator()
        .sub("group.design.params");

    group(a, "group.design.sketch", "design.sketch")
        .commands({"design.sketch", "design.sketchOnFace"});
    group(a, "group.design.plane", "design.constructionPlane")
        .commands({"design.constructionPlane"});
    group(a, "group.design.solid", "design.extrude")
        .commands({"design.extrude", "design.cutExtrude"}).separator()
        .commands({"design.revolve", "design.cutRevolve"}).separator()
        .commands({"design.loft", "design.cutLoft"}).separator()
        .commands({"design.sweep", "design.cutSweep"}).separator()
        .commands({"design.box", "design.cylinder", "design.sphere", "design.torus",
                   "design.coil", "design.pipe"});
    group(a, "group.design.fillet", "design.fillet")
        .commands({"design.fillet", "design.chamfer"});
    group(a, "group.design.hole", "design.hole")
        .commands({"design.hole", "design.counterbore", "design.countersink",
                   "design.threadedHole"});
    group(a, "group.design.move", "design.move")
        .commands({"design.move", "design.align"});
    group(a, "group.design.mirror", "design.mirror")
        .commands({"design.mirror", "design.pattern"});
    group(a, "group.design.params", "design.parameters")
        .commands({"design.parameters"});
}

/// Default bindings, in the order a bindings editor lists them. "std:" names
/// a platform key (bindings::standardKeyName()).
void addBindings(Arrangement& a)
{
    const auto bind = [&a](const char* id, const char* one = "", const char* two = "") {
        a.addDefaultBinding(id, bindings::Slots{one, two, ""});
    };
    bind("global.commandSearch", "/");

    bind("file.new", "std:New");
    bind("file.open", "std:Open");
    bind("file.save", "std:Save");
    bind("file.saveAs", "std:SaveAs");
    bind("file.close", "std:Close");
    bind("file.quit", "std:Quit");

    bind("edit.undo", "std:Undo");
    // Redo answers to both habits on every platform: Ctrl+Shift+Z
    // (FreeCAD, Blender, Inkscape) and Ctrl+Y (AutoCAD, SolidWorks,
    // Fusion). The platform key would give Windows users Ctrl+Y twice and
    // no Ctrl+Shift+Z. Qt reads Ctrl as Command on macOS.
    bind("edit.redo", "Ctrl+Shift+Z", "Ctrl+Y");
    bind("edit.cut", "std:Cut");
    bind("edit.copy", "std:Copy");
    bind("edit.paste", "std:Paste");
    bind("edit.delete", "std:Delete");
    bind("edit.selectAll", "std:SelectAll");

    bind("view.terminal", "Ctrl+`");
    bind("view.project", "Ctrl+R");
    bind("view.properties", "Ctrl+P");
    bind("view.toolbar");
    bind("view.resetView", "Home");
    bind("view.rotateLeft");
    bind("view.rotateRight");
    bind("view.showGrid", "Ctrl+Shift+G");
    bind("view.snapToGrid", "Ctrl+G");
    bind("view.zUpOrientation");
    bind("view.orbitSelected");
    bind("view.preferences", "std:Preferences");

    bind("construct.plane");

    bind("sketch.select", "S");
    bind("sketch.line", "L");
    bind("sketch.rectangle", "R");
    bind("sketch.circle", "C");
    bind("sketch.arc", "A");
    bind("sketch.point", "P");
    bind("sketch.dimension", "D");
    bind("sketch.construction", "X");
    bind("sketch.offset", "O");
    bind("sketch.trim", "T");
    bind("sketch.fillet", "F");
    bind("sketch.rotateCCW", "Q");
    bind("sketch.rotateCW", "E");
    bind("sketch.rotateReset", "Ctrl+0");
    bind("sketch.toggleGrid", "G");

    bind("design.extrude", "E");
    bind("design.move", "M");
    bind("design.fillet", "F");
    bind("design.chamfer");
    bind("design.hole", "H");
    bind("design.joint", "J");
    bind("design.measure", "I");
    bind("design.toggleVisibility", "V");

    bind("nav.rotateUp", "Up");
    bind("nav.rotateDown", "Down");
    bind("nav.axisX", "X");
    bind("nav.axisY", "Y");
    bind("nav.axisZ", "Z");
    bind("nav.rotateLeft", "Left");
    bind("nav.rotateRight", "Right");

    bind("viewport.rotate", "RightButton+Drag");
    bind("viewport.pan", "MiddleButton+Drag");
    bind("viewport.zoom", "Wheel");
}

Arrangement buildDefault()
{
    Arrangement a;
    addMenus(a);
    addSketchToolbar(a);
    addModelToolbar(a);
    addBindings(a);
    return a;
}

}  // namespace

const Arrangement& defaultArrangement()
{
    static const Arrangement a = buildDefault();
    return a;
}

}  // namespace layout
}  // namespace hobbycad
