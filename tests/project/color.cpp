// =====================================================================
//  tests/project/color.cpp — per-entity color survives save/load
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  A per-entity RGB (0xRRGGBB; -1 = default/by-layer) must survive a project
//  round trip in both serializer backends, or a colored sketch loses its
//  colors. Mirrors centerline.cpp.
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
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_color";
    fs::remove_all(base);
    fs::create_directories(base);
    std::printf("per-entity color round trip\n");

    const std::string dir = base + "/proj";
    {
        Project p;
        SketchData s; s.name = "colored";

        SketchEntityData plain; plain.type = sketch::EntityType::Line;
        plain.points = { {0.0, 0.0}, {10.0, 0.0} };          // default color (-1)
        s.entities.push_back(plain);

        SketchEntityData red; red.type = sketch::EntityType::Line;
        red.points = { {0.0, 5.0}, {10.0, 5.0} };
        red.color = 0xFF0000;
        s.entities.push_back(red);

        SketchEntityData teal; teal.type = sketch::EntityType::Circle;
        teal.points = { {0.0, 0.0} }; teal.radius = 3.0;
        teal.color = 0x008080;
        s.entities.push_back(teal);

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
            check(es.size() == 3, "all three entities load");
            if (es.size() == 3) {
                check(es[0].color == -1, "a default-color entity stays -1");
                check(es[1].color == 0xFF0000, "an RGB color survives the round trip");
                check(es[2].color == 0x008080, "a second RGB color survives too");
            }
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
