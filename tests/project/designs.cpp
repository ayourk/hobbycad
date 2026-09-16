// =====================================================================
//  tests/project/designs.cpp — single-design project layout
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The property under test: a project with one design writes its files
//  exactly where a project without the design concept did, and a manifest
//  that lists no designs loads as one design. Together those mean no
//  saved project ever has to be converted.
#include <hobbycad/browser.h>
#include <hobbycad/document.h>
#include <hobbycad/project.h>
#include <TopoDS_Shape.hxx>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static std::set<std::string> filesUnder(const std::string& dir) {
    std::set<std::string> out;
    if (!fs::exists(dir)) return out;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.is_regular_file()) {
            out.insert(fs::relative(e.path(), dir).generic_string());
        }
    }
    return out;
}
static SketchData mk(const char* name) {
    SketchData s; s.name = name;
    SketchEntityData e; e.type = sketch::EntityType::Line;
    e.points = { {0.0, 0.0}, {10.0, 0.0} };
    s.entities.push_back(e);
    return s;
}
static std::string slurp(const std::string& p) {
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_designs";
    fs::remove_all(base);
    fs::create_directories(base);
    std::printf("single-design project layout\n");

    const std::string dirA = base + "/A";
    {
        Project p;
        p.addSketch(mk("First"));
        p.addSketch(mk("Second"));

        check(p.designs().size() == 1, "a project has exactly one design");
        check(p.designs()[0].id == 1, "with id 1");
        check(p.designs()[0].pathPrefix.empty(),
              "and an EMPTY path prefix; this is what keeps files in place");
        check(p.sketches()[0].designId == 1, "sketches belong to it");

        std::string err;
        check(p.save(dirA, &err), "save succeeds");
    }

    // The decisive assertion: no designs/ directory, paths unchanged.
    {
        const auto files = filesUnder(dirA);
        check(files.count("sketches/sketch_1.json")
                  && files.count("sketches/sketch_2.json"),
              "sketches are still at sketches/sketch_<id>.json");
        bool anyDesignDir = false;
        for (const auto& f : files) {
            if (f.rfind("designs/", 0) == 0) anyDesignDir = true;
        }
        check(!anyDesignDir, "and NOTHING was moved under designs/");

        const std::string manifest = slurp(dirA + "/A.hcad");
        check(manifest.find("\"designs\"") != std::string::npos,
              "yet the manifest records the design explicitly");
    }

    // A manifest with NO designs array must still load as one design.
    // Not a compatibility promise (HobbyCAD is unreleased and breaking
    // changes are fine), but a project with zero designs is meaningless,
    // so a hand-edited or truncated manifest should degrade to the only
    // sensible reading rather than producing a project nothing can be
    // added to.
    {
        const std::string dirB = base + "/B";
        fs::copy(dirA, dirB, fs::copy_options::recursive);
        fs::rename(dirB + "/A.hcad", dirB + "/B.hcad");

        std::string text = slurp(dirB + "/B.hcad");
        const auto at = text.find("\"designs\"");
        check(at != std::string::npos, "the copy has a designs array to strip");
        if (at != std::string::npos) {
            // Blank out the array, leaving valid JSON without the key.
            const auto close = text.find(']', at);
            text.replace(at, close - at + 1, "\"unused\": []");
            std::ofstream(dirB + "/B.hcad") << text;
        }

        Project p; std::string err;
        check(p.load(dirB, &err), "a manifest with no designs still loads");
        check(p.designs().size() == 1,
              "and yields exactly one design, never zero");
        check(p.designs()[0].pathPrefix.empty(),
              "with an empty prefix, so its files are found where they are");
        check(p.sketches().size() == 2, "both sketches load");
        check(p.sketches()[0].designId == 1,
              "with its sketches attributed to that design");
    }

    // A second design must not disturb the first.
    {
        Project p; std::string err;
        check(p.load(dirA, &err), "reload the one-design project");
        const int second = p.addDesign("Bracket");
        check(second == 2, "the second design gets id 2");
        check(p.designPathPrefix(1).empty(),
              "the FIRST design keeps its empty prefix");
        check(p.designPathPrefix(2) == "designs/2/",
              "and only the second gets a directory");

        p.setActiveDesignId(second);
        p.addSketch(mk("InSecond"));
        const std::string dirC = base + "/C";
        check(p.save(dirC, &err), "save with two designs");

        const auto files = filesUnder(dirC);
        check(files.count("sketches/sketch_1.json"),
              "design 1's files did NOT move");
        bool secondPlaced = false;
        for (const auto& f : files) {
            if (f.rfind("designs/2/sketches/", 0) == 0) secondPlaced = true;
        }
        check(secondPlaced, "design 2's sketch went under designs/2/");
    }

    // ---- bodies have identity -----------------------------------------
    // Bodies used to be a bare vector of TopoDS_Shape: no id to name their
    // file, no name to show, and nowhere to record a design. Same defect
    // the sketches had before feature ids.
    {
        const std::string dirD = base + "/D";
        Project p;
        const int b1 = p.addBody(TopoDS_Shape{});
        const int b2 = p.addBody(TopoDS_Shape{}, "Bracket");

        check(b1 == 1 && b2 == 2, "bodies get distinct ids starting at 1");
        check(p.bodies().size() == 2, "both are held");
        check(p.bodies()[0].name == "Body1", "an unnamed body gets a default name");
        check(p.bodies()[1].name == "Bracket", "a named one keeps its name");
        check(p.bodies()[0].designId == 1, "and both belong to design 1");

        std::string err;
        check(p.save(dirD, &err), "save with bodies");

        const auto files = filesUnder(dirD);
        check(files.count("geometry/body_1.brep")
                  && files.count("geometry/body_2.brep"),
              "geometry files are named by body id");
        check(!files.count("geometry/body_001.brep"),
              "not by zero-padded position");
    }

    // Deleting a body must not renumber the survivors' files.
    {
        const std::string dirE = base + "/E";
        Project p;
        p.addBody(TopoDS_Shape{});
        p.addBody(TopoDS_Shape{});
        p.addBody(TopoDS_Shape{});

        // Drop the FIRST body, the case positional naming got wrong.
        std::vector<TopoDS_Shape> remaining;
        for (size_t i = 1; i < p.bodies().size(); ++i) {
            remaining.push_back(p.bodies()[i].shape);
        }
        // setShapes carries metadata by position, so re-assert identity the
        // way the GUI would: rebuild from the surviving records.
        const auto keptB = p.bodies()[1];
        const auto keptC = p.bodies()[2];
        Project q;
        q.addBody(keptB.shape, keptB.name);
        q.addBody(keptC.shape, keptC.name);

        std::string err;
        check(q.save(dirE, &err), "save the pruned project");
        const auto files = filesUnder(dirE);
        check(files.size() >= 2, "its bodies are written");
        check(!remaining.empty(), "and the survivors were carried over");
    }

    // A body in a second design lands under that design's prefix.
    {
        const std::string dirF = base + "/F";
        Project p;
        p.addBody(TopoDS_Shape{});
        const int second = p.addDesign("Bracket");
        p.setActiveDesignId(second);
        p.addBody(TopoDS_Shape{});

        check(p.bodies()[0].designId == 1, "the first body stays in design 1");
        check(p.bodies()[1].designId == second,
              "the second is created in the active design");

        std::string err;
        check(p.save(dirF, &err), "save with two designs holding bodies");
        const auto files = filesUnder(dirF);
        check(files.count("geometry/body_1.brep"),
              "design 1's body did NOT move");
        bool prefixed = false;
        for (const auto& f : files) {
            if (f.rfind("designs/2/geometry/", 0) == 0) prefixed = true;
        }
        check(prefixed, "and design 2's body went under designs/2/");
    }

    // ---- the Project <-> Document seam ---------------------------------
    // The document used to hold bare shapes, so syncing back into the
    // project had to carry id, name and design across BY POSITION. That is
    // wrong the moment a body is deleted: every survivor inherits the
    // identity of whatever used to sit at its index.
    {
        Project p;
        p.addBody(TopoDS_Shape{}, "Frame");
        const int second = p.addDesign("Sub");
        p.setActiveDesignId(second);
        p.addBody(TopoDS_Shape{}, "Bracket");
        p.setActiveDesignId(1);
        p.addBody(TopoDS_Shape{}, "Plate");

        Document doc;
        for (const auto& b : p.bodies()) {
            doc.addBody(b);
        }
        check(doc.bodies().size() == 3, "the document takes all three bodies");
        check(doc.bodies()[1].id == p.bodies()[1].id,
              "keeping their ids rather than reassigning");
        check(doc.bodies()[1].name == "Bracket", "and their names");
        check(doc.bodies()[1].designId == second, "and their designs");

        // Delete the FIRST body, then sync back.
        std::vector<BodyData> remaining(doc.bodies().begin() + 1,
                                        doc.bodies().end());
        doc.setBodies(remaining);
        p.setBodies(doc.bodies());

        check(p.bodies().size() == 2, "two bodies survive");
        check(p.bodies()[0].name == "Bracket" && p.bodies()[1].name == "Plate",
              "the SURVIVORS keep their own names, not their predecessors'");
        check(p.bodies()[0].designId == second,
              "and stay in the design they were made in");
        check(p.bodies()[0].id != 1,
              "and keep their original ids rather than being renumbered");

        // A body created in the document gets a fresh id, not a clash.
        const int fresh = doc.addShape(TopoDS_Shape{});
        bool clash = false;
        for (const auto& b : p.bodies()) {
            if (b.id == fresh) clash = true;
        }
        check(!clash, "a body added to the document cannot collide with an existing id");
    }

    // ---- construction planes have identity too --------------------------
    // Sketches reference a plane by id (SketchData::constructionPlaneId),
    // so positional plane files were worse than positional sketch files:
    // deleting a plane silently repointed every sketch that referenced a
    // later one.
    {
        const std::string dirG = base + "/G";
        Project p;

        ConstructionPlaneData a; a.name = "Top";
        ConstructionPlaneData b; b.name = "Angled";
        const int idA = p.addConstructionPlane(a);
        const int idB = p.addConstructionPlane(b);

        check(idA > 0 && idB > 0 && idA != idB,
              "planes get distinct ids from the project");
        check(p.constructionPlanes()[0].designId == 1,
              "and belong to the active design");

        std::string err;
        check(p.save(dirG, &err), "save with construction planes");

        const auto files = filesUnder(dirG);
        check(files.count("construction/plane_" + std::to_string(idA) + ".json"),
              "plane files are named by id");
        check(!files.count("construction/plane_001.json"),
              "not by zero-padded position");
    }

    // Relative-to references may never loop: a plane cannot be defined in
    // terms of itself, directly or through other planes, by either kind of
    // reference (offset-from-plane base, relative center).
    {
        Project p;
        ConstructionPlaneData a; a.name = "A";
        ConstructionPlaneData b; b.name = "B";
        ConstructionPlaneData c; c.name = "C";
        ConstructionPlaneData d; d.name = "D";
        const int idA = p.addConstructionPlane(a);
        const int idB = p.addConstructionPlane(b);
        const int idC = p.addConstructionPlane(c);
        const int idD = p.addConstructionPlane(d);
        check(p.constructionPlaneDependsOn(idA, idA), "a plane relative to itself is a loop");
        check(!p.constructionPlaneDependsOn(idA, -1), "absolute (no reference) never loops");
        check(!p.constructionPlaneDependsOn(idA, idB), "B is free: A may reference it");
        // B's center relative to A
        ConstructionPlaneData b2 = *p.constructionPlaneById(idB);
        b2.centerRelative = true; b2.centerRefPlaneId = idA;
        p.setConstructionPlane(1, b2);
        check(p.constructionPlaneDependsOn(idA, idB), "now A relative to B would loop (via B's center)");
        // C offset-from-plane B: chain C -> B -> A
        ConstructionPlaneData c2 = *p.constructionPlaneById(idC);
        c2.type = ConstructionPlaneType::OffsetFromPlane; c2.basePlaneId = idB;
        p.setConstructionPlane(2, c2);
        check(p.constructionPlaneDependsOn(idA, idC), "A relative to C would loop through B's base chain");
        check(p.constructionPlaneDependsOn(idB, idC), "B relative to C would loop");
        check(!p.constructionPlaneDependsOn(idC, idA), "C relative to A is fine: A depends on nothing");
        check(!p.constructionPlaneDependsOn(idA, idD), "D is unrelated: allowed");
        check(!p.constructionPlaneDependsOn(idD, idC), "D relative to C is allowed");
    }

    // Deleting the first plane must not renumber the survivor's file, or a
    // sketch referencing it would resolve to different geometry.
    {
        const std::string dirH = base + "/H";
        Project p;
        ConstructionPlaneData a; a.name = "First";
        ConstructionPlaneData b; b.name = "Second";
        b.centerRelative = true; b.centerRefPlaneId = 7; b.originX = 3.5;   // relative center round-trips
        p.addConstructionPlane(a);
        const int keep = p.addConstructionPlane(b);

        p.removeConstructionPlane(0);
        std::string err;
        check(p.save(dirH, &err), "save after deleting the first plane");

        const auto files = filesUnder(dirH);
        check(files.count("construction/plane_" + std::to_string(keep) + ".json"),
              "the SURVIVOR keeps its original filename");
        int planeFiles = 0;
        for (const auto& f : files) {
            if (f.rfind("construction/plane_", 0) == 0) ++planeFiles;
        }
        check(planeFiles == 1, "and it is the only plane file left");

        Project q;
        check(q.load(dirH, &err), "the pruned project loads");
        check(q.constructionPlanes().size() == 1, "with one plane");
        check(q.constructionPlanes()[0].id == keep, "keeping its id");
        check(q.constructionPlanes()[0].name == "Second", "and its name");
        check(q.constructionPlanes()[0].centerRelative && q.constructionPlanes()[0].centerRefPlaneId == 7
              && q.constructionPlanes()[0].originX == 3.5, "and its relative center reference");
        check(q.constructionPlaneById(keep) != nullptr,
              "so a sketch referencing it still resolves");
    }

    // A plane in a second design lands under that design's prefix.
    {
        const std::string dirI = base + "/I";
        Project p;
        ConstructionPlaneData a; a.name = "InFirst";
        p.addConstructionPlane(a);

        const int second = p.addDesign("Sub");
        p.setActiveDesignId(second);
        ConstructionPlaneData b; b.name = "InSecond";
        const int idB = p.addConstructionPlane(b);

        check(p.constructionPlanes()[1].designId == second,
              "a plane is created in the active design");

        std::string err;
        check(p.save(dirI, &err), "save with planes in two designs");
        const auto files = filesUnder(dirI);
        bool firstFlat = false, secondPrefixed = false;
        for (const auto& f : files) {
            if (f.rfind("construction/plane_", 0) == 0) firstFlat = true;
            if (f == "designs/2/construction/plane_" + std::to_string(idB) + ".json")
                secondPrefixed = true;
        }
        check(firstFlat, "design 1's plane stays flat");
        check(secondPrefixed, "and design 2's plane goes under designs/2/");
    }

    // ---- parameters: name IS the identity -------------------------------
    // Parameters get no surrogate id and no file of their own: expressions
    // resolve them by name, so the name is the identity. What they needed
    // was the other half: a design, unique names within it, and a rename
    // that carries references along.
    {
        Project p;
        ParameterData w; w.name = "width";  w.expression = "50";
        ParameterData h; h.name = "height"; h.expression = "width * 2";
        check(p.addParameter(w), "a parameter is added");
        check(p.addParameter(h), "and one referencing it");
        check(p.parameters()[0].designId == 1, "both belong to the active design");

        // A duplicate name would silently shadow, since evaluation is by name.
        ParameterData dup; dup.name = "width"; dup.expression = "99";
        check(!p.addParameter(dup), "a duplicate name is REFUSED");
        check(p.parameters().size() == 2, "and nothing was added");
    }

    // The same name in a DIFFERENT design is not a duplicate.
    {
        Project p;
        ParameterData a; a.name = "width"; a.expression = "10";
        check(p.addParameter(a), "design 1 has a width");

        const int second = p.addDesign("Sub");
        p.setActiveDesignId(second);
        ParameterData b; b.name = "width"; b.expression = "20";
        check(p.addParameter(b), "design 2 may have its own width");
        check(p.parameterByName("width", 1) != nullptr
                  && p.parameterByName("width", second) != nullptr,
              "and both resolve independently");
        check(p.parameterByName("width", 1)->expression == "10",
              "without shadowing each other");
    }

    // Renaming must carry every reference with it.
    {
        Project p;
        ParameterData w;  w.name = "width";  w.expression = "50";
        ParameterData h;  h.name = "height"; h.expression = "width * 2";
        ParameterData d;  d.name = "depth";  d.expression = "width + height";
        p.addParameter(w); p.addParameter(h); p.addParameter(d);

        check(p.renameParameter("width", "outerWidth", 1), "the rename succeeds");
        check(p.parameterByName("outerWidth", 1) != nullptr, "under the new name");
        check(p.parameterByName("width", 1) == nullptr, "and not the old one");
        check(p.parameterByName("height", 1)->expression == "outerWidth * 2",
              "an expression referencing it was REWRITTEN");
        check(p.parameterByName("depth", 1)->expression == "outerWidth + height",
              "including one that references it alongside another");

        check(!p.renameParameter("nosuch", "x", 1),
              "renaming a parameter that does not exist fails");
        check(!p.renameParameter("height", "depth", 1),
              "and renaming onto a taken name is refused, not allowed to shadow");
    }

    // Whole-word only. This is where a naive replace goes wrong.
    {
        Project p;
        ParameterData w;   w.name = "w";       w.expression = "5";
        ParameterData wid; wid.name = "width"; wid.expression = "w * 2";
        ParameterData tot; tot.name = "total"; tot.expression = "width + w + w2";
        ParameterData w2;  w2.name = "w2";     w2.expression = "1";
        p.addParameter(w); p.addParameter(wid); p.addParameter(tot); p.addParameter(w2);

        check(p.renameParameter("w", "wide", 1), "rename the short name");
        check(p.parameterByName("width", 1)->expression == "wide * 2",
              "the standalone reference is rewritten");
        check(p.parameterByName("total", 1)->expression == "width + wide + w2",
              "but 'width' and 'w2' are left alone; substrings are not references");
    }

    // A rename in one design must not touch another design's expressions.
    {
        Project p;
        ParameterData a; a.name = "width";  a.expression = "10";
        ParameterData b; b.name = "double"; b.expression = "width * 2";
        p.addParameter(a); p.addParameter(b);

        const int second = p.addDesign("Sub");
        p.setActiveDesignId(second);
        ParameterData c; c.name = "width"; c.expression = "99";
        ParameterData e; e.name = "triple"; e.expression = "width * 3";
        p.addParameter(c); p.addParameter(e);

        check(p.renameParameter("width", "w1", 1), "rename design 1's width");
        check(p.parameterByName("double", 1)->expression == "w1 * 2",
              "design 1's expression follows");
        check(p.parameterByName("width", second) != nullptr,
              "design 2 still has its own width");
        check(p.parameterByName("triple", second)->expression == "width * 3",
              "and its expression is untouched");
    }

    // ---- ownedFiles must match what save() actually writes --------------
    // These are two independent statements of the same path: the node model
    // tells the version-control layer which files a node owns, and save()
    // decides where they go. They drifted once already: ownedFiles kept
    // the old zero-padded names after the writers moved to ids, so every
    // lookup would have missed.
    {
        const std::string dirJ = base + "/J";
        Project p;
        p.addSketch(mk("Profile"));
        p.addBody(TopoDS_Shape{}, "Frame");
        ConstructionPlaneData cp; cp.name = "Angled";
        p.addConstructionPlane(cp);

        std::string err;
        check(p.save(dirJ, &err), "save one of each");
        const auto onDisk = filesUnder(dirJ);

        struct Claim { NodeType type; int id; };
        const Claim claims[] = {
            {NodeType::Sketch,            p.sketches()[0].id},
            {NodeType::Body,              p.bodies()[0].id},
            {NodeType::ConstructionPlane, p.constructionPlanes()[0].id},
        };

        for (const Claim& c : claims) {
            BrowserNode node;
            node.type = c.type;
            node.id = c.id;
            const auto owned = nodeTypeInfo(c.type).ownedFiles(node);
            check(owned.size() == 1,
                  "the node claims exactly one file");
            check(!owned.empty() && onDisk.count(owned[0]) == 1,
                  "and that file is the one save() actually wrote");
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
