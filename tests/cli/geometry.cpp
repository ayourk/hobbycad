// =====================================================================
//  tests/cli/geometry.cpp — CLI sketch geometry commands
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  These commands have a specific history worth guarding against.
//
//  They once parsed their arguments, printed "Created circle at (50, 50)
//  with radius 25", and added NOTHING to the sketch, reporting success
//  for work they discarded. So every assertion here checks the SKETCH,
//  not the message: what matters is that the entity exists, is of the
//  right type, and carries the values that were typed.
#include <hobbycad/strutil.h>
#include <cstdio>

#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

namespace {

/// One engine over one headless document, reset per test group.
struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};

    Fixture() { engine.setDocumentHost(&host); }

    CliResult run(const char* line) { return engine.execute(line); }

    /// The entities of the sketch committed by "finish".
    const std::vector<SketchEntityData>& entities(int sketchIndex = 0) const {
        static const std::vector<SketchEntityData> empty;
        const auto& sketches = host.hostProject()->sketches();
        if (sketchIndex >= static_cast<int>(sketches.size())) return empty;
        return sketches[static_cast<size_t>(sketchIndex)].entities;
    }
};

bool near(double a, double b) { return (a > b ? a - b : b - a) < 1e-9; }

}  // namespace

int main() {
    // ---- Every entity type reaches the sketch ---------------------------
    {
        Fixture f;
        f.run("create sketch XY shapes");
        f.run("polygon at 0,0 radius 25 sides 6");
        f.run("ellipse at 10,10 major 40 minor 20");
        f.run("slot from 0,0 to 50,0 width 10");
        f.run("spline through 0,0 25,40 50,0");
        f.run("text \"Part A\" at 0,60 size 8");
        const CliResult fin = f.run("finish");

        ck(fin.exitCode == 0, "the sketch commits");

        const auto& es = f.entities();
        ck(es.size() == 6,
           "all five entities reached the sketch: six records, since the\n"
           "           slot brings its centerline with it");
        if (es.size() == 6) {
            ck(es[0].type == sketch::EntityType::Polygon &&
               es[0].sides == 6 && near(es[0].radius, 25.0),
               "polygon keeps its radius and side count");
            ck(es[1].type == sketch::EntityType::Ellipse &&
               near(es[1].majorRadius, 40.0) && near(es[1].minorRadius, 20.0),
               "ellipse keeps both semi-axes");
            ck(es[3].type == sketch::EntityType::Slot &&
               es[3].points.size() == 2 && near(es[3].radius, 5.0),
               "slot keeps two centers and a radius");
            ck(es[4].type == sketch::EntityType::Spline &&
               es[4].points.size() == 3,
               "spline keeps every control point");
            ck(es[5].type == sketch::EntityType::Text &&
               es[5].text == "Part A" && near(es[5].fontSize, 8.0),
               "text keeps its string and size");
        }
    }

    // ---- Ids are assigned, unique, and 1-based --------------------------
    {
        Fixture f;
        f.run("create sketch XY ids");
        f.run("point at 0,0");
        f.run("point at 1,1");
        f.run("circle at 2,2 radius 1");
        f.run("finish");

        const auto& es = f.entities();
        ck(es.size() == 3, "three entities");
        if (es.size() == 3) {
            // Every entity used to be left at the default id 0, which
            // collides the moment anything (a constraint, a delete)
            // refers to one.
            ck(es[0].id == 1 && es[1].id == 2 && es[2].id == 3,
               "ids are 1-based and distinct, not all zero");
        }
    }

    // ---- The construction modifier works on every command ---------------
    {
        Fixture f;
        f.run("create sketch XY constr");
        f.run("line from 0,0 to 10,0 construction");
        f.run("circle at 0,0 radius 5 construction");
        f.run("polygon at 0,0 radius 5 sides 3 construction");
        f.run("rectangle from 0,0 to 5,5");
        f.run("finish");

        const auto& es = f.entities();
        ck(es.size() == 4, "four entities");
        if (es.size() == 4) {
            ck(es[0].isConstruction && es[1].isConstruction &&
               es[2].isConstruction,
               "the trailing keyword marks construction geometry");
            ck(!es[3].isConstruction,
               "and its absence leaves the entity as real geometry");
        }
    }

    // ---- Bad input is refused, and refuses ALL of the entity ------------
    {
        Fixture f;
        f.run("create sketch XY bad");

        ck(f.run("polygon at 0,0 radius 25 sides 2").exitCode != 0,
           "a 2-sided polygon is refused");
        ck(f.run("polygon at 0,0 radius 25 sides 5.5").exitCode != 0,
           "a fractional side count is refused, not truncated");
        ck(f.run("polygon at 0,0 radius 0 sides 6").exitCode != 0,
           "a zero radius is refused");
        ck(f.run("slot from 7,7 to 7,7 width 10").exitCode != 0,
           "a slot with coincident ends is refused");
        ck(f.run("spline through 0,0 bogus 5,5").exitCode != 0,
           "a malformed control point is refused");
        ck(f.run("text \"x\" at 0,0 size -3").exitCode != 0,
           "a negative text size is refused");
        ck(f.run("ellipse at 0,0 major 10").exitCode != 0,
           "an ellipse missing its minor axis is refused");

        f.run("finish");
        // The real hazard is a command that reports failure but leaves a
        // half-built entity behind.
        ck(f.entities().empty(),
           "and not one of them left anything in the sketch");
    }

    // ---- Mislabeled ellipse axes are corrected, and said so -------------
    {
        Fixture f;
        f.run("create sketch XY ell");
        const CliResult r = f.run("ellipse at 0,0 major 10 minor 30");
        f.run("finish");

        ck(r.exitCode == 0, "minor > major is accepted");
        ck(contains(r.output, "swapped"),
           "with the swap stated rather than done silently");
        const auto& es = f.entities();
        ck(es.size() == 1 && near(es[0].majorRadius, 30.0) &&
           near(es[0].minorRadius, 10.0),
           "and the larger value ends up as the major axis");
    }

    // ---- Geometry outside a sketch says where you are, not "unknown" ----
    {
        Fixture f;
        const CliResult r = f.run("circle at 0,0 radius 5");
        ck(r.exitCode != 0, "geometry outside a sketch fails");
        ck(!contains(r.error, "Unknown command"),
           "without calling a correctly spelled command unknown");
        ck(contains(r.error, "create sketch"),
           "and says what to do instead");
    }

    // ---- discard really discards ----------------------------------------
    {
        Fixture f;
        f.run("create sketch XY gone");
        f.run("circle at 0,0 radius 5");
        f.run("discard");
        ck(f.host.hostProject()->sketches().empty(),
           "a discarded sketch leaves nothing behind");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
