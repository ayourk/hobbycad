// =====================================================================
//  tests/project/centerline.cpp — centerline linetype survives save/load
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The centerline flag is a new per-entity linetype (a dash-dot reference
//  axis). Like isConstruction, it has to survive a project round trip in
//  both serializer backends, or a saved sketch loses its reference axes.
#include <hobbycad/project.h>
#include <cstdio>
#include <filesystem>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_centerline";
    fs::remove_all(base);
    fs::create_directories(base);
    std::printf("centerline linetype round trip\n");

    const std::string dir = base + "/proj";
    {
        Project p;
        SketchData s; s.name = "axes";

        SketchEntityData plain; plain.type = sketch::EntityType::Line;
        plain.points = { {0.0, 0.0}, {10.0, 0.0} };
        s.entities.push_back(plain);

        SketchEntityData center; center.type = sketch::EntityType::Line;
        center.points = { {0.0, 5.0}, {10.0, 5.0} };
        center.isCenterline = true;
        s.entities.push_back(center);

        SketchEntityData constr; constr.type = sketch::EntityType::Line;
        constr.points = { {0.0, 8.0}, {10.0, 8.0} };
        constr.isConstruction = true;
        s.entities.push_back(constr);

        SketchEntityData off; off.type = sketch::EntityType::Line;
        off.points = { {0.0, 2.0}, {10.0, 2.0} };
        off.offsetParentId = 1;      // an associative offset of entity 1
        off.offsetDistance = 2.0;
        off.offsetSide = -1;
        s.entities.push_back(off);

        SketchEntityData slot; slot.type = sketch::EntityType::Slot;
        slot.points = { {0.0, -5.0}, {10.0, -5.0} };
        slot.radius = 1.0;
        slot.pathEntityIds = { 1, 2 };   // unified single/multi path-reference link
        s.entities.push_back(slot);

        p.addSketch(s);
        std::string err;
        check(p.save(dir, &err), "save succeeds");
    }

    {
        Project p; std::string err;
        check(p.load(dir, &err), "load succeeds");
        check(p.sketches().size() == 1, "the sketch loads");
        if (!p.sketches().empty()) {
            const auto& es = p.sketches()[0].entities;
            check(es.size() == 5, "all five entities load");
            if (es.size() == 5) {
                check(!es[0].isCenterline && !es[0].isConstruction,
                      "the plain line stays plain");
                check(es[1].isCenterline && !es[1].isConstruction,
                      "the centerline flag survives the round trip");
                check(es[2].isConstruction && !es[2].isCenterline,
                      "construction and centerline stay distinct");
                check(es[3].offsetParentId == 1 && es[3].offsetSide == -1
                          && es[3].offsetDistance == 2.0,
                      "the associative offset link survives the round trip");
                check(es[0].offsetParentId == -1,
                      "a non-offset entity keeps no offset link");
                check(es[4].type == sketch::EntityType::Slot
                          && es[4].pathEntityIds == std::vector<int>{1, 2},
                      "the slot's unified pathEntityIds link survives the round trip");
            }
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
