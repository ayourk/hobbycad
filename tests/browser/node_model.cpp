// =====================================================================
//  tests/browser/node_model.cpp — objects browser node model
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/browser.h>
#include <hobbycad/project.h>
#include <TopoDS_Shape.hxx>
#include <cstdio>
#include <string>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static const BrowserNode* find(const BrowserNode& n, const std::string& name) {
    if (n.name == name) return &n;
    for (const auto& c : n.children) {
        if (const BrowserNode* hit = find(c, name)) return hit;
    }
    return nullptr;
}
static int countOf(const BrowserNode& n, NodeType t) {
    int total = (n.type == t) ? 1 : 0;
    for (const auto& c : n.children) total += countOf(c, t);
    return total;
}

int main() {
    std::printf("objects browser node model\n");

    // ---- an empty project -------------------------------------------
    {
        Project p;
        const BrowserNode root = buildBrowserTree(p);

        check(find(root, "Origin") != nullptr, "Origin is always present");
        check(countOf(root, NodeType::OriginPlane) == 3, "three origin planes");
        check(countOf(root, NodeType::OriginAxis) == 3, "three origin axes");
        check(countOf(root, NodeType::OriginPoint) == 1, "one origin point");
        check(find(root, "Parameters") != nullptr, "Parameters leaf present");

        // Fusion hides empty containers; an empty Bodies folder on every
        // new project is noise.
        check(find(root, "Bodies") == nullptr, "empty Bodies folder is hidden");
        check(find(root, "Sketches") == nullptr, "empty Sketches folder is hidden");
        check(find(root, "Construction") == nullptr, "empty Construction is hidden");

        BrowserOptions keep; keep.hideEmptyFolders = false;
        const BrowserNode all = buildBrowserTree(p, keep);
        check(find(all, "Bodies") != nullptr, "...but only when asked to hide them");
    }

    // ---- flags carry behavior, not a switch over type ----------------
    {
        Project p;
        const BrowserNode root = buildBrowserTree(p);
        const BrowserNode* xy = find(root, "XY Plane");
        check(xy && xy->has(NodeSketchable), "an origin plane can be sketched on");
        const BrowserNode* xa = find(root, "X Axis");
        check(xa != nullptr, "the X axis is a real node");
        check(xa && xa->has(NodeVisible), "and it can be shown or hidden");
        check(xa && !xa->has(NodeSketchable), "but it is not a sketch plane");
    }

    // ---- sketches and bodies ----------------------------------------
    {
        Project p;
        SketchData s; s.name = "Profile";
        p.addSketch(s);
        p.addBody(TopoDS_Shape{});
        p.addBody(TopoDS_Shape{});
        const BrowserNode root = buildBrowserTree(p);

        const BrowserNode* sk = find(root, "Profile");
        check(sk != nullptr, "the sketch appears by name");
        check(sk && sk->type == NodeType::Sketch, "typed as a Sketch");
        check(sk && sk->id == p.sketches()[0].id, "and carries its feature id");
        check(sk && sk->has(NodeEditable), "a sketch is editable");

        check(countOf(root, NodeType::Body) == 2, "both bodies appear");
        const BrowserNode* b1 = find(root, "Body1");
        check(b1 && b1->has(NodeVisible), "a body has a visibility toggle");
        check(b1 && b1->has(NodeExportable), "and can be exported on its own");
    }

    // ---- a background image is the sketch's Canvas child --------------
    {
        Project p;
        SketchData s; s.name = "Traced";
        s.backgroundImage.enabled = true;
        s.backgroundImage.filePath = "/tmp/plan.png";
        p.addSketch(s);
        const BrowserNode root = buildBrowserTree(p);

        const BrowserNode* sk = find(root, "Traced");
        check(sk && sk->children.size() == 1, "the sketch has one child");
        check(sk && !sk->children.empty()
                 && sk->children[0].type == NodeType::SketchCanvas,
              "which is a Canvas, not a folder of its own");
        check(sk && !sk->children.empty()
                 && sk->children[0].detail == "/tmp/plan.png",
              "and it remembers the image path");
    }

    // ---- referenced projects: the missing-link case -------------------
    // Aaron: an external reference that cannot be located needs an
    // indicator and a way to relink. The badge must differ from a healthy
    // link, and never be None.
    {
        check(badgeForReference(ReferenceKind::External, ReferenceState::Resolved)
                  == NodeBadge::ExternalLink,
              "a resolved external reference is badged as a link");
        check(badgeForReference(ReferenceKind::External, ReferenceState::Missing)
                  == NodeBadge::MissingLink,
              "a MISSING external reference gets a distinct badge");
        check(badgeForReference(ReferenceKind::External, ReferenceState::Missing)
                  != badgeForReference(ReferenceKind::External, ReferenceState::Resolved),
              "so a broken link can never be mistaken for a healthy one");
        check(badgeForReference(ReferenceKind::External, ReferenceState::Stale)
                  == NodeBadge::Warning,
              "a stale reference warns rather than errors");
        check(badgeForReference(ReferenceKind::Embedded, ReferenceState::NotApplicable)
                  == NodeBadge::None,
              "an embedded project carries no link badge");
    }

    // ---- node types are objects, not a switch -------------------------
    {
        const NodeTypeInfo& sketch = nodeTypeInfo(NodeType::Sketch);
        check(sketch.isContainer(), "a sketch groups its canvas, so it is a container");
        check((sketch.defaultFlags() & NodeEditable) != 0u,
              "and declares its own capabilities");

        // An empty container is still a container. Deriving this from the
        // child count, as an earlier version did, made an empty Bodies
        // folder report itself as a leaf.
        BrowserNode empty;
        empty.type = NodeType::Folder;
        check(empty.children.empty() && empty.isContainer(),
              "an EMPTY folder is still a container");

        BrowserNode leaf;
        leaf.type = NodeType::Body;
        check(!leaf.isContainer(), "a body is not");
    }

    // ---- registration replaces a description --------------------------
    {
        // A plugin adding a node type must not require editing the header.
        class Custom : public NodeTypeInfo {
        public:
            NodeType type() const override { return NodeType::Decal; }
            const char* name() const override { return "StickerThing"; }
            unsigned defaultFlags() const override { return NodeExportable; }
        };
        static const Custom kCustom;
        registerNodeType(&kCustom);
        check(std::string(nodeTypeName(NodeType::Decal)) == "StickerThing",
              "a registered type overrides the built-in description");
        check((nodeTypeInfo(NodeType::Decal).defaultFlags() & NodeExportable) != 0u,
              "including its capabilities");
    }

    // ---- embedded project depth ---------------------------------------
    // Aaron set the limit at 16.
    {
        BrowserNode root; root.type = NodeType::Root;
        check(embeddedProjectDepth(root) == 0, "no embedded projects means depth 0");
        check(canEmbedProjectAt(root), "so one can be embedded");

        // Build a chain 16 deep.
        BrowserNode* cursor = &root;
        for (int i = 0; i < kMaxEmbeddedProjectDepth; ++i) {
            BrowserNode child;
            child.type = NodeType::ProjectEmbedded;
            child.name = "Nested";
            cursor->children.push_back(child);
            cursor = &cursor->children.back();
        }
        check(embeddedProjectDepth(root) == 16, "a 16-deep chain measures 16");
        check(!canEmbedProjectAt(root), "and refuses a seventeenth level");

        // Depth counts nesting, not siblings: twenty projects side by side
        // are depth 1 and must stay legal.
        BrowserNode wide; wide.type = NodeType::Root;
        for (int i = 0; i < 20; ++i) {
            BrowserNode child;
            child.type = NodeType::ProjectEmbedded;
            wide.children.push_back(child);
        }
        check(embeddedProjectDepth(wide) == 1, "siblings do not add depth");
        check(canEmbedProjectAt(wide), "so many siblings stay legal");
    }

    // ---- folders ------------------------------------------------------
    {
        // System folders carry no user affordances; a user folder does.
        check((nodeTypeInfo(NodeType::Folder).defaultFlags() & NodeRenameable) == 0u,
              "a SYSTEM folder cannot be renamed");
        check((nodeTypeInfo(NodeType::UserFolder).defaultFlags() & NodeRenameable) != 0u,
              "a USER folder can");
        check((nodeTypeInfo(NodeType::UserFolder).defaultFlags() & NodeDeletable) != 0u,
              "and can be deleted");
    }

    // ---- reference capabilities ---------------------------------------
    {
        const unsigned emb = nodeTypeInfo(NodeType::ProjectEmbedded).defaultFlags();
        const unsigned ext = nodeTypeInfo(NodeType::ProjectExternal).defaultFlags();
        check((emb & NodeExportable) != 0u,
              "an embedded project can be exported back out");
        check((ext & NodeRelinkable) != 0u,
              "an external one can be relinked");
        check((ext & NodeExportable) == 0u,
              "but not exported: its content is not ours to write");
    }

    // ---- the STRUCTURE decides placement ------------------------------
    // Aaron: "if a project wants to add a node, the structure knows which
    // folder to add it into, correct?" It does now: the type declares a
    // container key and placeNode() routes it. Nothing names a folder at
    // the call site.
    {
        BrowserNode root; root.type = NodeType::Root;

        check(placeNode(root, BrowserNode{NodeType::Body, 0, "Body1"})
                  == PlacementResult::Placed,
              "a body can be placed without naming a folder");
        check(root.children.size() == 1, "which created exactly one folder");
        check(root.children[0].folderKey == "bodies", "and it is the Bodies folder");
        check(root.children[0].children.size() == 1, "with the body inside it");

        // A second body joins the folder rather than making another.
        placeNode(root, BrowserNode{NodeType::Body, 1, "Body2"});
        check(root.children.size() == 1, "a second body reuses the folder");
        check(root.children[0].children.size() == 2, "and lands beside the first");

        // A different type creates its own folder, in declared order.
        placeNode(root, BrowserNode{NodeType::Sketch, 7, "Profile"});
        check(root.children.size() == 2, "a sketch creates the Sketches folder");
        check(root.children[0].folderKey == "bodies"
              && root.children[1].folderKey == "sketches",
              "and folders come out in their declared order");
    }

    // ---- a type with no container goes to the top level ---------------
    {
        BrowserNode root; root.type = NodeType::Root;
        check(placeNode(root, BrowserNode{NodeType::Parameters, -1, "Parameters"})
                  == PlacementResult::Placed,
              "a top-level type places successfully");
        check(root.children.size() == 1 && root.children[0].folderKey.empty(),
              "and is not wrapped in a folder");
    }

    // ---- a misdeclared container is refused, not silently dropped -----
    {
        class Rogue : public NodeTypeInfo {
        public:
            NodeType type() const override { return NodeType::Analysis; }
            const char* name() const override { return "Rogue"; }
            const char* containerKey() const override { return "no-such-folder"; }
        };
        static const Rogue kRogue;
        registerNodeType(&kRogue);

        BrowserNode root; root.type = NodeType::Root;
        check(placeNode(root, BrowserNode{NodeType::Analysis, 1, "Thing"})
                  == PlacementResult::Fallback,
              "a node naming an unknown folder reports Fallback");
        check(root.children.size() == 1, "it is NOT lost");
        check(root.children[0].folderKey == "misc", "it lands in Misc");
        check(root.children[0].badge == NodeBadge::Warning,
              "and the Misc folder itself is badged, so the bug is visible");
        check(root.children[0].children.size() == 1, "with the node inside");
        check(!root.children[0].children.empty()
                  && root.children[0].children[0].detail.find("no-such-folder")
                         != std::string::npos,
              "and the node records WHICH container could not be resolved");

        // Misc must sort last, so it never pushes real content down.
        BrowserNode mixed; mixed.type = NodeType::Root;
        placeNode(mixed, BrowserNode{NodeType::Analysis, 1, "Stray"});
        placeNode(mixed, BrowserNode{NodeType::Body, 0, "Body1"});
        check(mixed.children.size() == 2, "Misc and Bodies both exist");
        check(mixed.children[0].folderKey == "bodies"
                  && mixed.children[1].folderKey == "misc",
              "and Misc sorts last");
    }

    // ---- Misc appears ONLY when something is wrong --------------------
    {
        Project p;
        SketchData sd; sd.name = "Profile"; p.addSketch(sd);

        const BrowserNode tidy = buildBrowserTree(p);
        check(find(tidy, "Misc") == nullptr,
              "a healthy project has no Misc folder");

        // Even when empty folders are kept for callers holding pointers,
        // Misc stays absent; an empty Misc on every project would make
        // the warning meaningless.
        BrowserOptions keep; keep.hideEmptyFolders = false;
        const BrowserNode all = buildBrowserTree(p, keep);
        check(find(all, "Bodies") != nullptr, "empty folders ARE materialized");
        check(find(all, "Misc") == nullptr, "but Misc never is");

        // The inverse guarantee, which is the one that actually matters:
        // once Misc HAS content it must never be hidden, whatever the
        // options say. A suppressed bug indicator is worse than none.
        BrowserNode hidden; hidden.type = NodeType::Root;
        placeNode(hidden, BrowserNode{NodeType::Analysis, 1, "Stray"});
        check(find(hidden, "Misc") != nullptr,
              "a Misc folder with content is present...");

        BrowserOptions tight; tight.hideEmptyFolders = true;
        BrowserNode hidden2; hidden2.type = NodeType::Root;
        placeNode(hidden2, BrowserNode{NodeType::Analysis, 2, "Stray2"});
        const BrowserNode* m = find(hidden2, "Misc");
        check(m != nullptr && !m->children.empty(),
              "...and stays present with hideEmptyFolders on");
        check(m != nullptr && m->badge == NodeBadge::Warning,
              "still badged, so it cannot be mistaken for a normal folder");
    }

    // ---- a plugin can add its own folder ------------------------------
    {
        registerFolder({"fixtures", "Fixtures", 45, NodeType::Folder});
        class Fixture : public NodeTypeInfo {
        public:
            NodeType type() const override { return NodeType::Joint; }
            const char* name() const override { return "Fixture"; }
            const char* containerKey() const override { return "fixtures"; }
        };
        static const Fixture kFixture;
        registerNodeType(&kFixture);

        BrowserNode root; root.type = NodeType::Root;
        check(placeNode(root, BrowserNode{NodeType::Joint, 1, "Clamp"})
                  == PlacementResult::Placed,
              "a plugin type routes into a plugin folder");
        check(root.children.size() == 1 && root.children[0].name == "Fixtures",
              "which the registry created on demand");
    }

    // ---- version control seam: nodes name their files ------------------
    // A node does not perform git operations; it says which files it is
    // made of. The mapping is one-to-many and sometimes ZERO.
    {
        BrowserNode sketch{NodeType::Sketch, 7, "Profile"};
        const auto files = nodeTypeInfo(NodeType::Sketch).ownedFiles(sketch);
        check(files.size() == 1, "a sketch owns one file");
        check(!files.empty() && files[0] == "sketches/sketch_7.json",
              "named by its feature id, matching what the project writes");

        BrowserNode plane{NodeType::OriginPlane, 0, "XY Plane"};
        check(nodeTypeInfo(NodeType::OriginPlane).ownedFiles(plane).empty(),
              "an origin plane owns NO file, so it can carry no VC state");

        // Id 1, not 0: body ids start at 1 and 0 means unassigned, so a
        // test using 0 was asserting a path no real body can have.
        BrowserNode body{NodeType::Body, 1, "Body1"};
        const auto bfiles = nodeTypeInfo(NodeType::Body).ownedFiles(body);
        check(bfiles.size() == 1 && bfiles[0] == "geometry/body_1.brep",
              "a body owns its BREP file, named by id");

        BrowserNode folder; folder.type = NodeType::Folder;
        check(nodeTypeInfo(NodeType::Folder).ownedFiles(folder).empty(),
              "a folder owns nothing: it is a rendering, not content");
    }

    // ---- context menus come from the type -----------------------------
    {
        auto ids = [](const std::vector<NodeAction>& as) {
            std::string out;
            for (const NodeAction& a : as) { out += a.id; out += " "; }
            return out;
        };
        auto has = [](const std::vector<NodeAction>& as, const char* id) {
            for (const NodeAction& a : as) if (a.id == id) return true;
            return false;
        };
        auto labelOf = [](const std::vector<NodeAction>& as, const char* id) {
            for (const NodeAction& a : as) if (a.id == id) return a.label;
            return std::string();
        };

        // A system folder must offer nothing, or right-clicking the tree
        // pops an empty menu everywhere.
        BrowserNode sysFolder; sysFolder.type = NodeType::Folder;
        sysFolder.flags = nodeTypeInfo(NodeType::Folder).defaultFlags();
        check(nodeTypeInfo(NodeType::Folder).actions(sysFolder).empty(),
              "a system folder offers no menu at all");

        // An origin plane cannot be renamed or deleted, but you can sketch
        // on it. The menu must reflect exactly that.
        BrowserNode plane{NodeType::OriginPlane, 0, "XY Plane"};
        plane.flags = nodeTypeInfo(NodeType::OriginPlane).defaultFlags();
        const auto planeMenu = nodeTypeInfo(NodeType::OriginPlane).actions(plane);
        check(has(planeMenu, "sketch"), "an origin plane offers Create Sketch");
        check(!has(planeMenu, "delete"), "but cannot be deleted");
        check(!has(planeMenu, "rename"), "and cannot be renamed");

        // Delete must be last and separated, never adjacent to Edit.
        BrowserNode body{NodeType::Body, 0, "Body1"};
        body.flags = nodeTypeInfo(NodeType::Body).defaultFlags();
        const auto bodyMenu = nodeTypeInfo(NodeType::Body).actions(body);
        check(!bodyMenu.empty() && bodyMenu.back().id == "delete",
              "Delete is the last entry");
        check(!bodyMenu.empty() && bodyMenu.back().separatorBefore,
              "and is separated from what precedes it");
        check(!bodyMenu.empty() && bodyMenu.back().destructive,
              "and marked destructive");
        check(has(bodyMenu, "export"), "a body can be exported");

        // Visibility wording tracks the node's current state.
        BrowserNode shown{NodeType::Body, 0, "Body1"};
        shown.flags = nodeTypeInfo(NodeType::Body).defaultFlags();
        shown.visible = true;
        BrowserNode hiddenBody = shown;
        hiddenBody.visible = false;
        check(labelOf(nodeTypeInfo(NodeType::Body).actions(shown),
                      "toggle_visibility") == "Hide",
              "a visible node offers Hide");
        check(labelOf(nodeTypeInfo(NodeType::Body).actions(hiddenBody),
                      "toggle_visibility") == "Show",
              "a hidden one offers Show");

        // Per-type overrides: the label must be specific, not generic.
        BrowserNode sk{NodeType::Sketch, 3, "Profile"};
        sk.flags = nodeTypeInfo(NodeType::Sketch).defaultFlags();
        check(labelOf(nodeTypeInfo(NodeType::Sketch).actions(sk), "edit")
                  == "Edit Sketch",
              "a sketch says Edit Sketch, not Edit");

        BrowserNode canvas{NodeType::SketchCanvas, 3, "Canvas"};
        canvas.flags = nodeTypeInfo(NodeType::SketchCanvas).defaultFlags();
        check(labelOf(nodeTypeInfo(NodeType::SketchCanvas).actions(canvas), "edit")
                  == "Calibrate...",
              "a canvas is edited by calibrating it");

        // The two reference conversions, each offered only where it applies.
        BrowserNode ext{NodeType::ProjectExternal, 1, "Bracket"};
        ext.flags = nodeTypeInfo(NodeType::ProjectExternal).defaultFlags();
        const auto extMenu = nodeTypeInfo(NodeType::ProjectExternal).actions(ext);
        check(has(extMenu, "relink"), "an external reference can be relinked");
        check(has(extMenu, "embed"), "and converted to an embedded copy");
        check(!has(extMenu, "export"),
              "but not exported: its content is not ours to write");

        BrowserNode emb{NodeType::ProjectEmbedded, 1, "Bracket"};
        emb.flags = nodeTypeInfo(NodeType::ProjectEmbedded).defaultFlags();
        const auto embMenu = nodeTypeInfo(NodeType::ProjectEmbedded).actions(emb);
        check(has(embMenu, "externalize"),
              "an embedded project can be linked out to a file");
        check(has(embMenu, "export"), "and exported");
        check(!has(embMenu, "relink"),
              "but never relinked: it has no link to repair");

        // A brand-new type gets a working menu from its flags alone, with
        // no override and no change to the menu-building code.
        class Widget : public NodeTypeInfo {
        public:
            NodeType type() const override { return NodeType::Mesh; }
            const char* name() const override { return "Widget"; }
            unsigned defaultFlags() const override
            { return NodeRenameable | NodeDeletable; }
        };
        static const Widget kWidget;
        registerNodeType(&kWidget);
        BrowserNode w{NodeType::Mesh, 1, "Thing"};
        w.flags = nodeTypeInfo(NodeType::Mesh).defaultFlags();
        const auto wMenu = nodeTypeInfo(NodeType::Mesh).actions(w);
        check(ids(wMenu) == "rename delete ",
              "a new type gets exactly the menu its flags describe");
    }

    // ---- a folder dies with its last leaf ------------------------------
    // Aaron: "if leaves of that type disappear, the resulting folder
    // disappears too, correct?" It must: a folder is a rendering of what
    // the document contains, so an empty Bodies folder is a lie about it.
    {
        BrowserNode root; root.type = NodeType::Root;
        placeNode(root, BrowserNode{NodeType::Body, 0, "Body1"});
        placeNode(root, BrowserNode{NodeType::Body, 1, "Body2"});
        placeNode(root, BrowserNode{NodeType::Sketch, 7, "Profile"});
        check(root.children.size() == 2, "two folders to begin with");

        check(removeNode(root, NodeType::Body, 0), "the first body is removed");
        check(find(root, "Bodies") != nullptr,
              "Bodies survives while one body remains");

        check(removeNode(root, NodeType::Body, 1), "the last body is removed");
        check(find(root, "Bodies") == nullptr, "and Bodies goes with it");
        check(find(root, "Sketches") != nullptr,
              "while the UNRELATED folder is untouched");

        // Every system folder is NodeType::Folder with id -1, so a pruner
        // that erased "the folder with that type and id" would delete the
        // wrong one. This is the assertion that catches it.
        check(find(root, "Profile") != nullptr,
              "and the surviving folder still holds its own leaf");

        check(removeNode(root, NodeType::Sketch, 7), "the sketch is removed");
        check(root.children.empty(), "leaving no folders at all");
        check(!removeNode(root, NodeType::Sketch, 7),
              "and removing it again reports nothing was found");
    }

    // Misc is pruned by the same rule: once the misplaced node is gone the
    // warning must go too, or it outlives the bug it was reporting.
    {
        BrowserNode root; root.type = NodeType::Root;
        placeNode(root, BrowserNode{NodeType::Analysis, 5, "Stray"});
        check(find(root, "Misc") != nullptr, "Misc appears for a stray node");
        check(removeNode(root, NodeType::Analysis, 5), "the stray is removed");
        check(find(root, "Misc") == nullptr, "and Misc goes with it");
    }

    // A rebuild reaches the same place, which is how the application gets
    // there today: it rebuilds rather than mutating.
    {
        Project p;
        SketchData sd; sd.name = "Only"; p.addSketch(sd);
        check(find(buildBrowserTree(p), "Sketches") != nullptr,
              "a project with a sketch has a Sketches folder");
        p.removeSketch(0);
        check(find(buildBrowserTree(p), "Sketches") == nullptr,
              "and loses the folder when the last sketch is deleted");
    }

    // ---- who hides when empty, and who does not ------------------------
    {
        // A user folder was made deliberately. Emptying it must NOT delete
        // it; dragging the last item out would otherwise destroy the
        // organization the user built.
        BrowserNode root; root.type = NodeType::Root;
        BrowserNode mine;
        mine.type = NodeType::UserFolder;
        mine.name = "My Parts";
        mine.id = 42;
        BrowserNode body{NodeType::Body, 0, "Body1"};
        mine.children.push_back(body);
        root.children.push_back(mine);

        check(removeNode(root, NodeType::Body, 0), "the last item is removed");
        check(find(root, "My Parts") != nullptr,
              "a USER folder survives being emptied");

        check(pruneEmptyFolders(root) == 0,
              "and a full prune does not take it either");

        // A system folder in the same tree still goes.
        placeNode(root, BrowserNode{NodeType::Body, 1, "Body2"});
        check(find(root, "Bodies") != nullptr, "the system folder exists");
        check(removeNode(root, NodeType::Body, 1), "its only body is removed");
        check(find(root, "Bodies") == nullptr, "and the system folder goes");
        check(find(root, "My Parts") != nullptr,
              "while the user folder is still there");
    }

    // ---- the rule does NOT apply to project nodes ----------------------
    // A referenced project is content, not a rendering of content. For a
    // BROKEN external reference this is load-bearing: it has no children
    // precisely because it could not resolve, so hiding it would erase the
    // evidence and leave nothing to relink.
    {
        BrowserNode root; root.type = NodeType::Root;

        BrowserNode ext;
        ext.type = NodeType::ProjectExternal;
        ext.name = "Bracket";
        ext.id = 1;
        ext.refKind = ReferenceKind::External;
        ext.refState = ReferenceState::Missing;
        ext.refPath = "/gone/bracket";
        ext.badge = badgeForReference(ext.refKind, ext.refState);
        root.children.push_back(ext);

        BrowserNode emb;
        emb.type = NodeType::ProjectEmbedded;
        emb.name = "Empty Sub";
        emb.id = 2;
        root.children.push_back(emb);

        check(!hidesNow(root.children[0]),
              "a MISSING external reference does not hide itself");
        check(!hidesNow(root.children[1]),
              "and neither does an empty embedded project");

        check(pruneEmptyFolders(root) == 0, "a full prune removes neither");
        check(find(root, "Bracket") != nullptr,
              "the broken link is still there to be relinked");
        check(find(root, "Empty Sub") != nullptr,
              "and the empty project is still there to be filled");
        check(root.children[0].badge == NodeBadge::MissingLink,
              "still badged as a broken link");
    }

    // ---- content is never hidden, whatever shape it has ----------------
    // A Sketch is a container: it holds its Canvas. It is also CONTENT.
    // When hidesWhenEmpty() defaulted to isContainer(), every sketch
    // without a background image (most of them) reported itself as
    // hideable, and deleting a background image deleted the whole sketch.
    {
        BrowserNode bare{NodeType::Sketch, 1, "Profile"};
        check(!hidesNow(bare),
              "a sketch with no canvas does NOT hide itself");

        BrowserNode root; root.type = NodeType::Root;
        BrowserNode sk{NodeType::Sketch, 7, "Traced"};
        sk.children.push_back(BrowserNode{NodeType::SketchCanvas, 7, "Canvas"});
        root.children.push_back(sk);

        check(removeNode(root, NodeType::SketchCanvas, 7),
              "the background image is deleted");
        check(find(root, "Traced") != nullptr,
              "and the SKETCH survives losing it");
        check(pruneEmptyFolders(root) == 0,
              "a full prune does not take it either");
    }

    // ---- which FOLDER types vanish when empty --------------------------
    // Only the generic system folder. The rest are either always populated
    // (so an empty one means a bug, which must stay visible) or authored.
    {
        check(nodeTypeInfo(NodeType::Folder).hidesWhenEmpty(),
              "the system folder (Bodies, Sketches, Construction, Misc) hides");
        check(!nodeTypeInfo(NodeType::UserFolder).hidesWhenEmpty(),
              "a user folder does not");
        check(!nodeTypeInfo(NodeType::OriginFolder).hidesWhenEmpty(),
              "Origin does not: it is always populated, so empty means a bug");
        check(!nodeTypeInfo(NodeType::DocumentSettings).hidesWhenEmpty(),
              "nor does Document Settings, for the same reason");
        check(!nodeTypeInfo(NodeType::Root).hidesWhenEmpty(),
              "and the root never vanishes");

        // A design is content: someone authored it, and an empty one is a
        // design you are about to draw in. Hiding it would make "New
        // Design" look like it did nothing.
        check(!nodeTypeInfo(NodeType::Design).hidesWhenEmpty(),
              "a design does not hide when empty either");
        check(nodeTypeInfo(NodeType::Design).isContainer(),
              "even though it is a container");
        check(!hidesNow(BrowserNode{NodeType::Design, 1, "Empty Design"}),
              "so an empty design survives a prune");

        // The default is the safe one: a new container type added without
        // thinking about this cannot make itself disappear.
        class NewThing : public NodeTypeInfo {
        public:
            NodeType type() const override { return NodeType::Analysis; }
            const char* name() const override { return "NewThing"; }
            bool isContainer() const override { return true; }
        };
        static const NewThing kNewThing;
        registerNodeType(&kNewThing);
        check(!nodeTypeInfo(NodeType::Analysis).hidesWhenEmpty(),
              "a new container type defaults to NOT hiding");
    }

    // Emptiness is derived, not counted: a node built by hand with children
    // is visible without anyone having incremented anything.
    {
        BrowserNode f;
        f.type = NodeType::Folder;
        check(hidesNow(f), "an empty system folder hides");
        f.children.push_back(BrowserNode{NodeType::Body, 0, "Body1"});
        check(!hidesNow(f),
              "and stops hiding the moment it has a child, with no count kept");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
