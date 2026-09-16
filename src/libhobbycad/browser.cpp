// =====================================================================
//  src/libhobbycad/browser.cpp — objects browser node model
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "hobbycad/browser.h"

#include "hobbycad/format.h"
#include "hobbycad/project.h"

#include <algorithm>
#include <map>

namespace hobbycad {

namespace {

/// One description per type. Declared with a small helper because the
/// bodies are all the same shape: identity, capabilities, container-ness.
class SimpleNodeType : public NodeTypeInfo {
public:
    SimpleNodeType(NodeType t, const char* n, unsigned flags,
                   bool container, const char* tag, const char* parentKey = "",
                   const char* filePattern = "")
        : m_type(t), m_name(n), m_flags(flags), m_container(container),
          m_tag(tag), m_parentKey(parentKey), m_filePattern(filePattern) {}

    NodeType type() const override { return m_type; }
    const char* name() const override { return m_name; }
    unsigned defaultFlags() const override { return m_flags; }
    bool isContainer() const override { return m_container; }
    const char* legacyTag() const override { return m_tag; }
    const char* containerKey() const override { return m_parentKey; }

    std::vector<std::string> ownedFiles(const BrowserNode& node) const override
    {
        if (!*m_filePattern || node.id < 0) {
            return {};
        }
        return { format(m_filePattern, node.id) };
    }

private:
    NodeType m_type;
    const char* m_name;
    unsigned m_flags;
    bool m_container;
    const char* m_tag;
    const char* m_parentKey;
    const char* m_filePattern;
};

// The vocabulary. A new node type is one row here plus, if it needs
// behavior these fields cannot express, its own NodeTypeInfo subclass.
const SimpleNodeType kBuiltinTypes[] = {
    {NodeType::Root,             "Root",             NodeNone,                                                   true,  ""},
    {NodeType::Folder,           "Folder",           NodeNone,                                                   true,  ""},
    {NodeType::UserFolder,       "UserFolder",       NodeRenameable | NodeDeletable,                             true,  ""},
    {NodeType::DocumentSettings, "DocumentSettings", NodeNone,                                                   true,  ""},
    {NodeType::Setting,          "Setting",          NodeEditable,                                               false, "units",              "settings"},
    {NodeType::OriginFolder,     "OriginFolder",     NodeNone,                                                   true,  ""},
    {NodeType::OriginPlane,      "OriginPlane",      NodeVisible | NodeSketchable,                               false, "origin_plane",       "origin"},
    {NodeType::OriginAxis,       "OriginAxis",       NodeVisible,                                                false, "origin_axis",        "origin"},
    {NodeType::OriginPoint,      "OriginPoint",      NodeVisible,                                                false, "origin_point",       "origin"},
    {NodeType::Sketch,           "Sketch",           NodeVisible | NodeRenameable | NodeDeletable | NodeEditable, true,  "sketch",             "sketches",     "sketches/sketch_%d.json"},
    {NodeType::SketchCanvas,     "SketchCanvas",     NodeVisible | NodeEditable | NodeDeletable,                 false, "sketch_canvas"},
    {NodeType::Body,             "Body",             NodeVisible | NodeRenameable | NodeDeletable | NodeExportable, false, "body",             "bodies",       "geometry/body_%d.brep"},
    {NodeType::ConstructionPlane,"ConstructionPlane",NodeVisible | NodeRenameable | NodeDeletable | NodeSketchable, false, "construction_plane","construction", "construction/plane_%d.json"},
    {NodeType::ConstructionAxis, "ConstructionAxis", NodeVisible | NodeRenameable | NodeDeletable,                false, "construction_axis",  "construction"},
    {NodeType::ConstructionPoint,"ConstructionPoint",NodeVisible | NodeRenameable | NodeDeletable,                false, "construction_point", "construction"},
    {NodeType::Parameters,       "Parameters",       NodeEditable,                                               false, "parameters"},

    // An embedded project travels with this file, so it can be exported
    // back out on its own. An external one cannot be exported (it is not
    // ours), but it can be relinked when its path stops resolving.
    // Content, so it never hides when empty; top level, so no container key.
    {NodeType::Design,           "Design",           NodeVisible | NodeRenameable | NodeDeletable | NodeExportable, true,  "design"},
    {NodeType::ProjectEmbedded,  "ProjectEmbedded",  NodeVisible | NodeRenameable | NodeDeletable | NodeExportable, true, "project_embedded"},
    {NodeType::ProjectExternal,  "ProjectExternal",  NodeVisible | NodeRenameable | NodeDeletable | NodeRelinkable, true, "project_external"},

    {NodeType::Joint,            "Joint",            NodeVisible | NodeRenameable | NodeDeletable,                false, "joint"},
    {NodeType::NamedView,        "NamedView",        NodeRenameable | NodeDeletable,                              false, "named_view"},
    {NodeType::Mesh,             "Mesh",             NodeVisible | NodeRenameable | NodeDeletable | NodeExportable, false, "mesh"},
    {NodeType::Analysis,         "Analysis",         NodeVisible | NodeRenameable | NodeDeletable,                false, "analysis"},
    {NodeType::Decal,            "Decal",            NodeVisible | NodeRenameable | NodeDeletable,                false, "decal"},
};

/// The system folders, in display order. A plugin can add to this.
/// Misc is declared here rather than special-cased so it sorts, renders
/// and hides by exactly the same rules as every other folder.
std::vector<FolderSpec>& folderTable()
{
    static std::vector<FolderSpec> folders = {
        {"settings",     "Document Settings", 10, NodeType::DocumentSettings},
        {"origin",       "Origin",            20, NodeType::OriginFolder},
        {"bodies",       "Bodies",            30, NodeType::Folder},
        {"sketches",     "Sketches",          40, NodeType::Folder},
        {"construction", "Construction",      50, NodeType::Folder},
        // Sorted last, and created only when something has gone wrong.
        {kMiscFolderKey, "Misc",              900, NodeType::Folder},
    };
    return folders;
}

/// Rename one entry of the default menu, keeping everything else.
std::vector<NodeAction> relabel(std::vector<NodeAction> actions,
                                const std::string& id,
                                const char* label)
{
    for (NodeAction& a : actions) {
        if (a.id == id) {
            a.label = label;
        }
    }
    return actions;
}

/// A sketch's generic "Edit" is ambiguous next to Rename; say what it edits.
class SketchNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;
    std::vector<NodeAction> actions(const BrowserNode& n) const override
    {
        return relabel(defaultActions(n), "edit", "Edit Sketch");
    }
};

/// A canvas is edited through the calibration dialog, not a generic editor.
class CanvasNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;
    std::vector<NodeAction> actions(const BrowserNode& n) const override
    {
        return relabel(defaultActions(n), "edit", "Calibrate...");
    }
};

/// Parameters is a leaf that opens a dialog; "Edit" understates it.
class ParametersNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;
    std::vector<NodeAction> actions(const BrowserNode& n) const override
    {
        return relabel(defaultActions(n), "edit", "Open Parameters...");
    }
};

/// An external reference can be turned into an embedded copy. See devdoc
/// section 13: that conversion is a copy, and it stops tracking the
/// original, so the label says so rather than reading as a toggle.
class ExternalProjectNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;

    // hidesWhenEmpty() is false by default, which is what a referenced
    // project needs: a broken link has no children BECAUSE it is broken.

    std::vector<NodeAction> actions(const BrowserNode& n) const override
    {
        std::vector<NodeAction> a = defaultActions(n);
        a.push_back({"embed", "Embed a Copy", true, false});
        return a;
    }
};

/// An embedded project can be pushed back out to a file and linked to.
class EmbeddedProjectNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;

    // An empty embedded project is still a project; the false default is
    // correct here.

    std::vector<NodeAction> actions(const BrowserNode& n) const override
    {
        std::vector<NodeAction> a = defaultActions(n);
        a.push_back({"externalize", "Link to a Project File...", true, false});
        return a;
    }
};

/// The system grouping folders: Bodies, Sketches, Construction, Misc.
/// The ONLY types that vanish when empty: they exist to organize other
/// nodes, so an empty one misrepresents the document.
class SystemFolderNodeType : public SimpleNodeType {
public:
    using SimpleNodeType::SimpleNodeType;
    bool hidesWhenEmpty() const override { return true; }
};

class UnknownNodeType : public NodeTypeInfo {
public:
    NodeType type() const override { return NodeType::Folder; }
    const char* name() const override { return "Unknown"; }
};
const UnknownNodeType kUnknown;

/// Lookup table, built once. Registration overwrites an entry, so a
/// plugin can replace a built-in description as well as add one.
std::map<NodeType, const NodeTypeInfo*>& registry()
{
    static std::map<NodeType, const NodeTypeInfo*> table = [] {
        std::map<NodeType, const NodeTypeInfo*> t;
        for (const SimpleNodeType& info : kBuiltinTypes) {
            t[info.type()] = &info;
        }
        // Types whose menu needs more than the flags can express.
        static const SketchNodeType kSketch(
            NodeType::Sketch, "Sketch",
            NodeVisible | NodeRenameable | NodeDeletable | NodeEditable,
            true, "sketch", "sketches", "sketches/sketch_%d.json");
        static const CanvasNodeType kCanvas(
            NodeType::SketchCanvas, "SketchCanvas",
            NodeVisible | NodeEditable | NodeDeletable, false, "sketch_canvas");
        static const ParametersNodeType kParams(
            NodeType::Parameters, "Parameters", NodeEditable, false, "parameters");
        static const ExternalProjectNodeType kExternal(
            NodeType::ProjectExternal, "ProjectExternal",
            NodeVisible | NodeRenameable | NodeDeletable | NodeRelinkable,
            true, "project_external");
        static const SystemFolderNodeType kSystemFolder(
            NodeType::Folder, "Folder", NodeNone, true, "");
        static const EmbeddedProjectNodeType kEmbedded(
            NodeType::ProjectEmbedded, "ProjectEmbedded",
            NodeVisible | NodeRenameable | NodeDeletable | NodeExportable,
            true, "project_embedded");
        for (const NodeTypeInfo* info : {
                 static_cast<const NodeTypeInfo*>(&kSketch),
                 static_cast<const NodeTypeInfo*>(&kCanvas),
                 static_cast<const NodeTypeInfo*>(&kParams),
                 static_cast<const NodeTypeInfo*>(&kExternal),
                 static_cast<const NodeTypeInfo*>(&kEmbedded),
                 static_cast<const NodeTypeInfo*>(&kSystemFolder)}) {
            t[info->type()] = info;
        }
        return t;
    }();
    return table;
}

}  // namespace

const NodeTypeInfo& nodeTypeInfo(NodeType type)
{
    const auto& table = registry();
    const auto it = table.find(type);
    return it != table.end() ? *it->second
                             : static_cast<const NodeTypeInfo&>(kUnknown);
}

std::vector<std::string> NodeTypeInfo::ownedFiles(const BrowserNode&) const
{
    return {};
}

std::vector<NodeAction> defaultActions(const BrowserNode& node)
{
    std::vector<NodeAction> actions;

    // Order is deliberate and matches how often each is wanted: act on the
    // thing, then change it, then get rid of it. Delete is last, separated,
    // and marked destructive so it is never adjacent to Edit.
    if (node.has(NodeEditable)) {
        actions.push_back({"edit", "Edit", false, false});
    }
    if (node.has(NodeSketchable)) {
        actions.push_back({"sketch", "Create Sketch", false, false});
    }
    if (node.has(NodeVisible)) {
        actions.push_back({"toggle_visibility",
                           node.visible ? "Hide" : "Show", true, false});
    }
    if (node.has(NodeRenameable)) {
        actions.push_back({"rename", "Rename", true, false});
    }
    if (node.has(NodeExportable)) {
        actions.push_back({"export", "Export...", false, false});
    }
    if (node.has(NodeRelinkable)) {
        actions.push_back({"relink", "Relink...", false, false});
    }
    if (node.has(NodeDeletable)) {
        actions.push_back({"delete", "Delete", true, true});
    }
    return actions;
}

std::vector<NodeAction> NodeTypeInfo::actions(const BrowserNode& node) const
{
    return defaultActions(node);
}

const std::vector<FolderSpec>& folderSpecs()
{
    return folderTable();
}

void registerFolder(const FolderSpec& spec)
{
    std::vector<FolderSpec>& folders = folderTable();
    for (FolderSpec& existing : folders) {
        if (existing.key == spec.key) {
            existing = spec;
            return;
        }
    }
    folders.push_back(spec);
    std::sort(folders.begin(), folders.end(),
              [](const FolderSpec& a, const FolderSpec& b) { return a.order < b.order; });
}

namespace {

/// Find a declared folder by key, or nullptr.
const FolderSpec* specForKey(const std::string& key)
{
    for (const FolderSpec& f : folderTable()) {
        if (f.key == key) return &f;
    }
    return nullptr;
}

/// Append into a folder, creating it in declared order if absent.
void insertInto(BrowserNode& root, const FolderSpec& spec, BrowserNode node)
{
    for (BrowserNode& child : root.children) {
        if (child.folderKey == spec.key) {
            child.children.push_back(std::move(node));
            return;
        }
    }

    BrowserNode created;
    created.type = spec.type;
    created.name = spec.name;
    created.folderKey = spec.key;
    created.flags = nodeTypeInfo(spec.type).defaultFlags();
    if (spec.key == kMiscFolderKey) {
        // The folder itself is the signal: its mere presence means a node
        // could not be placed.
        created.badge = NodeBadge::Warning;
        created.detail = "Nodes whose container could not be resolved";
    }
    created.children.push_back(std::move(node));

    const auto at = std::find_if(
        root.children.begin(), root.children.end(),
        [&](const BrowserNode& existing) {
            const FolderSpec* other = specForKey(existing.folderKey);
            return other && other->order > spec.order;
        });
    root.children.insert(at, std::move(created));
}

}  // namespace

const char* const kMiscFolderKey = "misc";

PlacementResult placeNode(BrowserNode& root, BrowserNode node)
{
    const char* key = nodeTypeInfo(node.type).containerKey();

    // No container declared: the node belongs at the top level. Parameters
    // and referenced projects sit there deliberately.
    if (!key || !*key) {
        root.children.push_back(std::move(node));
        return PlacementResult::Placed;
    }

    if (const FolderSpec* spec = specForKey(key)) {
        insertInto(root, *spec, std::move(node));
        return PlacementResult::Placed;
    }

    // Unresolved container. Put it somewhere visible and say why, rather
    // than dropping it (the node vanishes and the bug with it) or refusing
    // (the caller has to invent a destination).
    node.detail = std::string("Unresolved container: ") + key;
    if (node.badge == NodeBadge::None) {
        node.badge = NodeBadge::Warning;
    }

    const FolderSpec* misc = specForKey(kMiscFolderKey);
    if (!misc) {
        // Only reachable if Misc was deregistered; still do not lose it.
        root.children.push_back(std::move(node));
        return PlacementResult::Fallback;
    }
    insertInto(root, *misc, std::move(node));
    return PlacementResult::Fallback;
}

void registerNodeType(const NodeTypeInfo* info)
{
    if (info) {
        registry()[info->type()] = info;
    }
}

const char* nodeTypeName(NodeType type)
{
    return nodeTypeInfo(type).name();
}

bool BrowserNode::isContainer() const
{
    return nodeTypeInfo(type).isContainer();
}

bool hidesNow(const BrowserNode& node)
{
    // Emptiness is DERIVED, never stored. children.empty() cannot fall out
    // of step with the children; a cached count can, and does.
    return node.children.empty() && nodeTypeInfo(node.type).hidesWhenEmpty();
}

int pruneEmptyFolders(BrowserNode& root)
{
    int removed = 0;

    // Depth first, so a folder emptied by pruning its own children is
    // itself pruned in the same pass.
    for (BrowserNode& child : root.children) {
        removed += pruneEmptyFolders(child);
    }

    const auto dead = std::remove_if(
        root.children.begin(), root.children.end(),
        [](const BrowserNode& n) { return hidesNow(n); });
    removed += static_cast<int>(std::distance(dead, root.children.end()));
    root.children.erase(dead, root.children.end());
    return removed;
}

bool removeNode(BrowserNode& root, NodeType type, int id)
{
    for (auto it = root.children.begin(); it != root.children.end(); ++it) {
        if (it->type == type && it->id == id) {
            root.children.erase(it);
            return true;
        }
    }

    // Recurse, and prune on the way back up, but erase the emptied folder
    // by ITERATOR, not by type and id. Every system folder shares
    // NodeType::Folder with id -1, so removing "the folder with that type
    // and id" would delete whichever one came first, not the one that was
    // just emptied.
    for (auto it = root.children.begin(); it != root.children.end(); ++it) {
        if (removeNode(*it, type, id)) {
            if (hidesNow(*it)) {
                root.children.erase(it);
            }
            return true;
        }
    }
    return false;
}

int embeddedProjectDepth(const BrowserNode& root)
{
    int deepest = 0;
    for (const BrowserNode& child : root.children) {
        const int below = embeddedProjectDepth(child);
        const int here = (child.type == NodeType::ProjectEmbedded) ? below + 1 : below;
        if (here > deepest) {
            deepest = here;
        }
    }
    return deepest;
}

bool canEmbedProjectAt(const BrowserNode& root)
{
    return embeddedProjectDepth(root) < kMaxEmbeddedProjectDepth;
}

NodeBadge badgeForReference(ReferenceKind kind, ReferenceState state)
{
    if (kind == ReferenceKind::External) {
        // A reference that cannot be found is the case Aaron called out:
        // it must be visibly distinct and offer a relink, never silently
        // render as an empty node.
        if (state == ReferenceState::Missing) return NodeBadge::MissingLink;
        if (state == ReferenceState::Stale)   return NodeBadge::Warning;
        return NodeBadge::ExternalLink;
    }
    return NodeBadge::None;
}

namespace {

/// Make a node of a type, taking its capabilities from the registry so
/// flags are declared in exactly one place.
BrowserNode make(NodeType type, std::string name, int id = -1)
{
    BrowserNode n;
    n.type = type;
    n.name = std::move(name);
    n.id = id;
    n.flags = nodeTypeInfo(type).defaultFlags();
    return n;
}

BrowserNode folder(NodeType type, std::string name)
{
    return make(type, std::move(name));
}

BrowserNode originGeometry()
{
    BrowserNode origin = folder(NodeType::OriginFolder, "Origin");

    struct PlaneDef { const char* name; int id; };
    // Ids match SketchPlane so a click can be turned straight into a plane.
    const PlaneDef planes[] = {
        {"XY Plane", static_cast<int>(SketchPlane::XY)},
        {"XZ Plane", static_cast<int>(SketchPlane::XZ)},
        {"YZ Plane", static_cast<int>(SketchPlane::YZ)},
    };
    for (const PlaneDef& p : planes) {
        origin.children.push_back(make(NodeType::OriginPlane, p.name, p.id));
    }

    const char* axes[] = {"X Axis", "Y Axis", "Z Axis"};
    for (int i = 0; i < 3; ++i) {
        // A revolve axis is picked from here.
        origin.children.push_back(make(NodeType::OriginAxis, axes[i], i));
    }

    origin.children.push_back(make(NodeType::OriginPoint, "Origin Point", 0));

    return origin;
}

}  // namespace

BrowserInput browserInputFor(const Project& project)
{
    BrowserInput in;
    in.projectName = project.name();
    in.units = project.units();

    for (const BodyData& b : project.bodies()) {
        in.bodies.push_back({b.id, b.name});
    }

    for (const SketchData& s : project.sketches()) {
        SketchSummary sum;
        sum.id = s.id;
        sum.name = s.name;
        sum.hasCanvas = s.backgroundImage.enabled
                        || !s.backgroundImage.filePath.empty();
        sum.canvasVisible = s.backgroundImage.enabled;
        sum.canvasPath = s.backgroundImage.filePath;
        in.sketches.push_back(sum);
    }
    for (const ConstructionPlaneData& p : project.constructionPlanes()) {
        in.planes.push_back({p.id, p.name});
    }
    return in;
}

BrowserNode buildBrowserTree(const Project& project,
                             const BrowserOptions& options)
{
    return buildBrowserTree(browserInputFor(project), options);
}

BrowserNode buildBrowserTree(const BrowserInput& input,
                             const BrowserOptions& options)
{
    BrowserNode root;
    root.type = NodeType::Root;
    root.name = input.projectName.empty() ? std::string("Untitled")
                                          : input.projectName;

    // Every node below is routed by placeNode(), which asks the node's TYPE
    // which folder it belongs in. Nothing here names a folder, so adding a
    // node type does not mean editing this function.

    BrowserNode units = make(NodeType::Setting, "Units");
    units.detail = input.units;
    placeNode(root, units);

    // Origin geometry is fixed rather than derived from the document: the
    // three planes, three axes and the point always exist.
    //
    // Bound to a named local deliberately: iterating originGeometry()
    // .children directly would range over a temporary destroyed before the
    // loop body runs. C++23 extends that lifetime; we build as C++17.
    const BrowserNode origin = originGeometry();
    for (const BrowserNode& n : origin.children) {
        placeNode(root, n);
    }

    for (const BodySummary& b : input.bodies) {
        // Keyed by the body's id, not its position, so hiding or selecting
        // a body survives another body being deleted.
        placeNode(root, make(NodeType::Body, b.name, b.id));
    }

    for (const SketchSummary& s : input.sketches) {
        BrowserNode n = make(NodeType::Sketch, s.name, s.id);

        // A background image is Fusion's "Canvas", and belongs to its
        // sketch rather than to a folder of its own, so it is attached
        // directly rather than routed.
        if (s.hasCanvas) {
            BrowserNode canvas = make(NodeType::SketchCanvas, "Canvas", s.id);
            canvas.detail = s.canvasPath;
            canvas.visible = s.canvasVisible;
            n.children.push_back(canvas);
        }
        placeNode(root, n);
    }

    for (const PlaneSummary& p : input.planes) {
        placeNode(root, make(NodeType::ConstructionPlane, p.name, p.id));
    }

    // Deliberately a top-level leaf that opens a dialog rather than a folder
    // of parameter nodes: a parameter is not spatial, cannot be shown or
    // hidden, and there may be hundreds.
    placeNode(root, make(NodeType::Parameters, "Parameters"));

    if (!options.hideEmptyFolders) {
        for (const FolderSpec& spec : folderSpecs()) {
            // Never materialize Misc. It is an error indicator: an empty
            // Misc on every project would say "something went wrong" on
            // every project, and the signal would stop meaning anything.
            if (spec.key == kMiscFolderKey) {
                continue;
            }
            bool present = false;
            for (const BrowserNode& child : root.children) {
                if (child.folderKey == spec.key) { present = true; break; }
            }
            if (!present) {
                BrowserNode f;
                f.type = spec.type;
                f.name = spec.name;
                f.folderKey = spec.key;
                root.children.push_back(f);
            }
        }
        std::stable_sort(root.children.begin(), root.children.end(),
                         [](const BrowserNode& a, const BrowserNode& b) {
            auto order = [](const BrowserNode& n) {
                if (n.folderKey.empty()) return 1000;   // leaves last
                for (const FolderSpec& f : folderSpecs()) {
                    if (f.key == n.folderKey) return f.order;
                }
                return 999;
            };
            return order(a) < order(b);
        });
    }

    return root;
}

}  // namespace hobbycad
