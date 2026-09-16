// =====================================================================
//  tests/project/sketch_ids.cpp — sketch files are named by feature id
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The defect this guards: sketches used to be saved positionally as
//  sketch_001.json, so deleting the first sketch shifted every later
//  sketch's contents into a different filename. Under version control
//  one deletion made the whole directory look rewritten, and per-file
//  decoration in the feature tree pointed at the wrong sketch.
#include <hobbycad/project.h>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static std::set<std::string> sketchFiles(const std::string& dir) {
    std::set<std::string> out;
    const fs::path p = fs::path(dir) / "sketches";
    if (!fs::exists(p)) return out;
    for (const auto& e : fs::directory_iterator(p)) out.insert(e.path().filename().string());
    return out;
}

static SketchData mk(const char* name) {
    SketchData s; s.name = name;
    SketchEntityData e; e.type = sketch::EntityType::Line;
    e.points = { {0.0, 0.0}, {10.0, 0.0} };
    s.entities.push_back(e);
    return s;
}

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_sketch_ids";
    fs::remove_all(base);
    fs::create_directories(base);
    std::printf("sketch file naming by feature id\n");

    const std::string dirA = base + "/A";
    {
        Project p;
        p.addSketch(mk("First"));
        p.addSketch(mk("Second"));
        p.addSketch(mk("Third"));

        const auto sk = p.sketches();
        check(sk.size() == 3, "three sketches added");
        check(sk[0].id > 0 && sk[1].id > 0 && sk[2].id > 0,
              "addSketch assigns a feature id to each");
        check(sk[0].id != sk[1].id && sk[1].id != sk[2].id && sk[0].id != sk[2].id,
              "the assigned ids are distinct");

        std::string err;
        check(p.save(dirA, &err), "save succeeds");
    }

    const auto before = sketchFiles(dirA);
    check(before.count("sketch_1.json") && before.count("sketch_2.json")
              && before.count("sketch_3.json"),
          "files are named by id, not zero-padded position");
    check(!before.count("sketch_001.json"), "the old positional name is gone");

    // ---- the actual defect: delete the FIRST sketch and re-save ----------
    const std::string dirB = base + "/B";
    {
        Project p;
        std::string err;
        check(p.load(dirA, &err), "reload the saved project");
        const auto sk = p.sketches();
        check(sk.size() == 3, "three sketches survive a round trip");
        check(sk[0].id == 1 && sk[1].id == 2 && sk[2].id == 3,
              "feature ids survive the round trip");
        check(sk[1].name == "Second", "and they are still matched to their names");

        p.removeSketch(0);                       // drop "First"
        check(p.save(dirB, &err), "save after deleting the first sketch");
    }

    const auto after = sketchFiles(dirB);
    check(after.size() == 2, "two sketch files remain");
    check(after.count("sketch_2.json") && after.count("sketch_3.json"),
          "the SURVIVORS keep their original filenames");
    check(!after.count("sketch_1.json"),
          "and nothing was renumbered into the deleted sketch's name");

    {
        Project p; std::string err;
        check(p.load(dirB, &err), "the pruned project still loads");
        const auto sk = p.sketches();
        check(sk.size() == 2 && sk[0].name == "Second" && sk[1].name == "Third",
              "with the right sketches, in order");
        check(sk[0].id == 2 && sk[1].id == 3, "and their ids are unchanged");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
