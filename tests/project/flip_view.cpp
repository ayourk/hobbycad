// =====================================================================
//  tests/project/flip_view.cpp — SketchData::flipView survives save/load
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Heads/tails (draw from the far side of the plane) is a persisted per-
//  sketch property: a flipped sketch must reopen flipped. It is written only
//  when true (files stay clean) and reads back false by default. Both
//  serialization paths (QJson writer/reader and the nlohmann reader) carry
//  it; this pins the round trip through Project::save/load.
#include <hobbycad/project.h>
#include <cstdio>
#include <string>
using namespace hobbycad;

static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w);
    if (!ok) ++fails;
}

static SketchData mk(const char* name, SketchPlane plane, bool flip) {
    SketchData s; s.name = name; s.plane = plane; s.flipView = flip;
    return s;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1 ? std::string(argv[1]) : "/tmp/hobbycad_flip") + "_proj";
    std::printf("flipView round trip\n");

    {
        Project p;
        p.addSketch(mk("Tails", SketchPlane::XY, true));   // flipped
        p.addSketch(mk("Heads", SketchPlane::XZ, false));  // default
        std::string err;
        ck(p.save(dir, &err), "save succeeds");
    }
    {
        Project p; std::string err;
        ck(p.load(dir, &err), "reload the saved project");
        const auto& sk = p.sketches();
        ck(sk.size() == 2, "two sketches reloaded");
        if (sk.size() == 2) {
            // Match by name (order is preserved, but be explicit).
            const SketchData* tails = nullptr;
            const SketchData* heads = nullptr;
            for (const auto& s : sk) {
                if (s.name == "Tails") tails = &s;
                if (s.name == "Heads") heads = &s;
            }
            ck(tails && tails->flipView, "flipped sketch reopens flipped (true survived)");
            ck(heads && !heads->flipView, "default sketch stays unflipped (false)");
        }
    }

    if (fails == 0) std::printf("flip_view: ALL PASS\n");
    else            std::printf("flip_view: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
