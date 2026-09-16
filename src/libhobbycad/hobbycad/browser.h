// =====================================================================
//  src/libhobbycad/hobbycad/browser.h — objects browser node model
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  A Qt-free description of the objects browser tree: what nodes exist
//  for a project, what kind each one is, and what state it carries.
//
//  This lives in the library rather than the GUI because it answers a
//  question about the DOCUMENT, not about a widget: a wxWidgets or
//  headless front end needs exactly the same tree. The GUI's job is
//  reduced to rendering these nodes and reporting clicks.
//
//  It replaces node types encoded as magic strings in Qt::UserRole,
//  which could not express nesting, could not be tested outside a built
//  window, and grew a new string comparison in mainwindow.cpp per type.
//
#ifndef HOBBYCAD_BROWSER_H
#define HOBBYCAD_BROWSER_H

#include <string>
#include <vector>

namespace hobbycad {

class Project;

/// What a browser node is.
///
/// Aaron expects "many, many different possible types of leaf nodes", so
/// this is written to be appended to. Adding a value must not require
/// touching a switch that decides layout (see NodeFlags below, which is
/// how behavior is attached instead).
enum class NodeType {
    Root,                ///< The project itself
    Folder,              ///< SYSTEM container (Bodies, Sketches, ...): fixed
                         ///  vocabulary, created by the builder, not persisted
    UserFolder,          ///< A folder the USER made: named, persisted, a drop
                         ///  target. Does not exist yet; see "Folders" below
    DocumentSettings,    ///< Container for document-wide settings
    Setting,             ///< One editable setting (units, grid spacing)

    OriginFolder,        ///< Container for the origin geometry
    OriginPlane,         ///< XY / XZ / YZ
    OriginAxis,          ///< X / Y / Z
    OriginPoint,         ///< The origin itself

    Sketch,
    SketchCanvas,        ///< Background image, Fusion's "Canvas"
    Body,                ///< A solid
    ConstructionPlane,
    ConstructionAxis,
    ConstructionPoint,
    Parameters,          ///< Leaf that opens the parameters dialog

    /// One model within a project: its own Origin, sketches, bodies and
    /// construction geometry. This is what Fusion calls a design.
    ///
    /// **Declared, not built.** Today a HobbyCAD project holds exactly one
    /// model and the browser root IS that model, so no Design node is ever
    /// created. The type exists so the vocabulary is settled before the
    /// file format commits to it; see devdoc section 16.
    Design,

    /// Another HobbyCAD project appearing inside this one.
    /// Embedded: its content is stored in this project and travels with it.
    /// External: this project stores a reference and resolves it on load.
    ProjectEmbedded,
    ProjectExternal,

    // Reserved for features that do not exist yet. Listed so the
    // vocabulary is agreed before each is built, not invented ad hoc.
    Joint,
    NamedView,
    Mesh,
    Analysis,
    Decal,
};

/// How a referenced project is stored.
enum class ReferenceKind {
    None,        ///< Not a reference
    Embedded,    ///< Content lives inside this project
    External,    ///< Content lives elsewhere; we store a path
};

/// Whether an external reference could be found.
enum class ReferenceState {
    NotApplicable,
    Resolved,    ///< Found and loaded
    Missing,     ///< The path does not resolve; needs relinking
    Stale,       ///< Found, but changed since it was last resolved
};

/// A visual marker on a node. Rendering picks the icon; the model only
/// says what the node's situation IS.
enum class NodeBadge {
    None,
    Warning,          ///< Triangle with an exclamation mark
    Error,            ///< Red circle with a cross
    ExternalLink,     ///< This node is a link to something outside
    MissingLink,      ///< A link that cannot be resolved
    Modified,         ///< Differs from the last save (version control)
    Untracked,        ///< Not under version control
    IntentToTrack,    ///< Will be included on the next save
};

/// Per-node capabilities. Attaching behavior to flags rather than to a
/// switch over NodeType is what keeps "many, many types" cheap: a new
/// type declares what it can do instead of being added to every handler.
enum NodeFlags : unsigned {
    NodeNone       = 0u,
    NodeVisible    = 1u << 0,   ///< Has a visibility toggle
    NodeRenameable = 1u << 1,
    NodeDeletable  = 1u << 2,
    NodeEditable   = 1u << 3,   ///< Double-click opens an editor
    NodeSketchable = 1u << 4,   ///< Can be used as a sketch plane
    NodeExportable = 1u << 5,   ///< Can be written out on its own
    NodeRelinkable = 1u << 6,   ///< Offers "relink" when it cannot resolve
};

/// One node in the objects browser.
struct BrowserNode {
    NodeType type = NodeType::Folder;

    /// Stable identity within its kind: a feature id for a sketch, an
    /// index for a body, a plane id for a construction plane. -1 when the
    /// node is a fixed part of the tree (folders, origin geometry).
    int id = -1;

    std::string name;
    std::string detail;          ///< Optional secondary text (a path, a value)

    /// For a system folder, the key types match against. Empty otherwise.
    std::string folderKey;

    unsigned flags = NodeNone;
    bool visible = true;         ///< Meaningful only with NodeVisible

    ReferenceKind refKind = ReferenceKind::None;
    ReferenceState refState = ReferenceState::NotApplicable;
    std::string refPath;         ///< For External: where it should be found

    NodeBadge badge = NodeBadge::None;

    std::vector<BrowserNode> children;

    bool has(NodeFlags f) const { return (flags & f) != 0u; }

    /// Whether this node GROUPS others. Asks the type, not the child
    /// count: an empty Bodies folder is still a container, and treating
    /// it as a leaf was wrong.
    bool isContainer() const;
};

/// Maximum nesting depth for embedded projects.
///
/// An embedded project can itself contain embedded projects, which is
/// natural in a tree and unbounded in cost. Aaron set the limit at 16
/// (2026-08-27). It is enforced when a project is embedded, not at load
/// time, so the failure is reported while the user can still act on it.
constexpr int kMaxEmbeddedProjectDepth = 16;

/// Options controlling how the tree is built.
struct BrowserOptions {
    /// Leave out folders that have no content, as Fusion does. An empty
    /// Construction folder on every new project is noise.
    ///
    /// Folders are created on demand by placeNode(), so the default costs
    /// nothing; setting this false materializes every declared folder for
    /// callers that hold pointers to them.
    ///
    /// Misc is exempt either way. It is an error indicator, so it appears
    /// only when a node actually failed to place, and is never hidden once
    /// it has content.
    bool hideEmptyFolders = true;
};

/// One entry in a node's context menu.
struct NodeAction {
    std::string id;              ///< Stable identifier the handler switches on
    std::string label;           ///< User-visible text
    bool separatorBefore = false;
    bool destructive = false;    ///< Rendered apart; Delete and friends
};

/// Per-type description: one object per NodeType.
///
/// Aaron: *"I was kind of expecting leaf nodes to be expressed as
/// objects."* This is that, arranged so the cost of a new node type stays
/// flat. Node INSTANCES stay plain values (BrowserNode) because the tree is
/// rebuilt from the document rather than owned and mutated; value
/// semantics make it cheap to copy, diff and test. The per-TYPE behavior
/// that would otherwise be a switch lives here instead, in one object per
/// type, looked up once.
///
/// Adding a node type means writing one subclass and registering it. It
/// does not mean editing a switch in the builder, another in the widget,
/// and a third in whatever handles clicks, which is what the enum-plus-
/// switch arrangement cost, and why "many, many types" needed this first.
class NodeTypeInfo {
public:
    virtual ~NodeTypeInfo() = default;

    virtual NodeType type() const = 0;

    /// Stable identifier, for diagnostics, tests and serialization.
    virtual const char* name() const = 0;

    /// Capabilities every node of this type starts with.
    virtual unsigned defaultFlags() const { return NodeNone; }

    /// True if this type groups other nodes, even when it currently has
    /// none. An empty Bodies folder is still a container.
    virtual bool isContainer() const { return false; }

    /// Legacy Qt::UserRole tag, during the migration off magic strings.
    /// Empty once nothing compares strings any more.
    virtual const char* legacyTag() const { return ""; }

    /// Key of the system folder nodes of this type belong in, or "" for
    /// the top level.
    ///
    /// This is what lets the STRUCTURE decide placement. Without it the
    /// builder had to know, imperatively, that bodies go in Bodies and
    /// sketches go in Sketches, so adding a type meant editing the
    /// builder as well as registering the type. Now a type declares where
    /// it lives and placeNode() routes it.
    virtual const char* containerKey() const { return ""; }

    /// Whether a container of this type disappears once it holds nothing.
    ///
    /// **Defaults to false, and only pure grouping folders opt in.** The
    /// default was originally isContainer(), which was wrong and dangerous:
    /// a Sketch is a container (it holds its Canvas), so a sketch with no
    /// background image (most of them) reported itself as hideable, and
    /// deleting a background image deleted the whole sketch with it.
    ///
    /// The distinction is authorship, not shape. A node the BUILDER creates
    /// to organize other nodes is a rendering of the document, and an empty
    /// one is a lie about it. A node someone AUTHORED is content, and stays
    /// until they remove it. Content that happens to be able to hold
    /// children is still content.
    ///
    /// So this is false for sketches, project references (embedded and
    /// external), user folders, and the always-populated Origin and
    /// Document Settings groups, where an empty one would mean a bug, and
    /// hiding a bug is what the Misc folder exists to avoid. It is true
    /// only for the system folders: Bodies, Sketches, Construction, Misc.
    ///
    /// For an external reference it is load-bearing: a link that will not
    /// resolve has no children precisely because it is broken, so hiding it
    /// would erase the evidence and leave nothing to relink.
    ///
    /// Note this is asked of the TYPE, and the emptiness itself is derived
    /// from children rather than counted. A stored leaf count is derived
    /// state kept next to its source: every add and remove path has to
    /// update it, and the one that forgets leaves either a folder that
    /// never disappears or, far worse, one that vanishes while still
    /// holding children, which reads as data loss.
    virtual bool hidesWhenEmpty() const { return false; }

    /// Context menu for a node of this type.
    ///
    /// The default derives the menu from the node's capability flags, so a
    /// new type gets a sensible menu by declaring what it can do. Override
    /// only to rename an entry for clarity ("Edit Sketch" rather than
    /// "Edit") or to add an action the flags cannot express.
    virtual std::vector<NodeAction> actions(const BrowserNode& node) const;

    /// Project-relative files this node owns, if any.
    ///
    /// The seam for version control. A node does not perform git
    /// operations (see "Version control" in the docs); it says which
    /// files it is made of, and a version-control layer maps those to a
    /// status and back to a badge. The mapping is genuinely one-to-many
    /// and sometimes zero (an origin plane owns no file at all), which is
    /// why it cannot be a single path on the node.
    virtual std::vector<std::string> ownedFiles(const BrowserNode& node) const;
};

/// The capability-derived menu, exposed so overrides can start from it.
std::vector<NodeAction> defaultActions(const BrowserNode& node);

/// A system folder: fixed vocabulary, created by the builder, not persisted.
struct FolderSpec {
    std::string key;      ///< Matches NodeTypeInfo::containerKey()
    std::string name;     ///< Display name
    int order = 0;        ///< Sort position among top-level folders
    NodeType type = NodeType::Folder;
};

/// The declared system folders, in display order.
const std::vector<FolderSpec>& folderSpecs();

/// Declare a system folder. A plugin adding node types can add the folder
/// they live in without editing this header.
void registerFolder(const FolderSpec& spec);

/// Key of the fallback folder. A node whose declared container cannot be
/// found lands here rather than being dropped or refused.
extern const char* const kMiscFolderKey;

/// Outcome of placing a node.
enum class PlacementResult {
    Placed,     ///< Into the folder its type declares, or the top level
    Fallback,   ///< Its container could not be found; it went to Misc
};

/// Add a node to the tree, into the folder its type declares.
///
/// Creates the folder if it is declared but not yet present.
///
/// If the node's container key names no known folder the node is placed in
/// **Misc** and the result is Fallback. It is never dropped and never
/// refused: losing a node hides the bug, and refusing means the caller has
/// to invent somewhere to put it. Misc appears only when something is
/// already wrong, carries a warning badge, and records the unresolved key
/// on the node so the cause is visible in the tree itself.
PlacementResult placeNode(BrowserNode& root, BrowserNode node);

/// The description for a type. Never null: unknown types get a fallback.
const NodeTypeInfo& nodeTypeInfo(NodeType type);

/// Register a description, replacing any existing one for that type.
/// Exists so a plugin can add a node type without editing this header.
/// The pointer must outlive the process; static instances are intended.
void registerNodeType(const NodeTypeInfo* info);

/// Remove the first node of a type and id, and prune the folder it was in
/// if that leaves it empty.
///
/// A folder is a rendering of what the document contains, not content in
/// its own right, so an empty Bodies folder left behind after the last body
/// is deleted is a lie about the document. Misc is pruned by the same rule:
/// once the misplaced nodes are gone the warning should go with them.
///
/// @return true if a node was removed.
bool removeNode(BrowserNode& root, NodeType type, int id);

/// Drop every container that has no children AND whose type hides when
/// empty, depth first. User folders are kept.
/// Returns the number of folders removed.
int pruneEmptyFolders(BrowserNode& root);

/// Whether this node should disappear right now: a container, empty, and
/// of a type that hides when empty.
bool hidesNow(const BrowserNode& node);

/// Deepest embedded-project nesting in a tree. 0 when there are none.
int embeddedProjectDepth(const BrowserNode& root);

/// Whether embedding one more level would exceed kMaxEmbeddedProjectDepth.
bool canEmbedProjectAt(const BrowserNode& root);

/// What the tree needs to know about one sketch.
struct SketchSummary {
    int id = -1;
    std::string name;
    bool hasCanvas = false;      ///< A background image is attached
    bool canvasVisible = false;
    std::string canvasPath;
};

/// What the tree needs to know about one body.
struct BodySummary {
    int id = -1;
    std::string name;
};

/// What the tree needs to know about one construction plane.
struct PlaneSummary {
    int id = -1;
    std::string name;
};

/// Everything the tree is built from.
///
/// Deliberately NOT a Project. The GUI's live state is not in the Project
/// until it is saved, and pushing it there just to redraw a tree would mark
/// the document modified on every refresh. A caller can fill this straight
/// from whatever it currently holds.
struct BrowserInput {
    std::string projectName;
    std::string units;
    std::vector<BodySummary> bodies;
    std::vector<SketchSummary> sketches;
    std::vector<PlaneSummary> planes;
};

/// Build the browser tree from explicit inputs.
BrowserNode buildBrowserTree(const BrowserInput& input,
                             const BrowserOptions& options = {});

/// Collect the inputs from a saved project.
BrowserInput browserInputFor(const Project& project);

/// Build the browser tree for a project.
BrowserNode buildBrowserTree(const Project& project,
                             const BrowserOptions& options = {});

/// Human-readable name for a node type, for diagnostics and tests.
const char* nodeTypeName(NodeType type);

/// Badge a node should carry given its reference state. Kept separate so
/// version-control decoration can override it later without the builder
/// needing to know about git.
NodeBadge badgeForReference(ReferenceKind kind, ReferenceState state);

}  // namespace hobbycad

#endif  // HOBBYCAD_BROWSER_H
