// =====================================================================
//  tests/cli/create_plane.cpp — `create plane` command
//  Grammar: create plane at X,Y,Z | offset from X,Y,Z | relative to <ref>
//           [offset N] [with rotation X Y Z]   (rotation is space-separated)
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/strutil.h>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"
#include <hobbycad/project.h>
#include <cmath>
#include <cstdio>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

namespace {
struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
    const std::vector<ConstructionPlaneData>& planes() const { return proj().constructionPlanes(); }
};
}

int main() {
    std::printf("create plane\n");
    Fixture f;

    // at X,Y,Z with rotation (space-separated)
    CliResult r1 = f.run("create plane at 0,0,10 with rotation 45 0 0");
    check(r1.exitCode == 0 && contains(r1.output, "Created plane"), "at 0,0,10 with rotation 45 0 0");
    check(f.planes().size() == 1, "one plane exists");
    if (f.planes().size() == 1) {
        const auto& p = f.planes()[0];
        check(near(p.originZ, 10.0), "origin Z = 10");
        check(near(p.primaryAngle, 45.0), "rotation X -> primaryAngle 45");
    }

    // offset from X,Y,Z with rotation on Y
    f.run("create plane offset from 5,0,0 with rotation 0 30 0");
    check(f.planes().size() == 2, "two planes exist");
    if (f.planes().size() == 2) {
        const auto& p = f.planes()[1];
        check(near(p.originX, 5.0), "origin X = 5");
        check(near(p.secondaryAngle, 30.0), "rotation Y -> secondaryAngle 30");
    }

    // relative to a named plane, with an offset distance
    CliResult r3 = f.run("create plane relative to \"Plane 1\" offset 20");
    check(r3.exitCode == 0, "relative to Plane 1 offset 20");
    check(f.planes().size() == 3, "three planes exist");
    if (f.planes().size() == 3) {
        const auto& p = f.planes()[2];
        check(p.centerRelative, "relative plane is centerRelative");
        check(p.centerRefPlaneId == f.planes()[0].id, "references Plane 1's id");
        check(near(p.offset, 20.0), "offset = 20");
    }

    // space-tolerant coordinates: "3, 4, 5" and "1 , 2 , 3"
    f.run("create plane at 3, 4, 5");
    check(f.planes().size() == 4, "spaced coord '3, 4, 5' creates a plane");
    if (f.planes().size() == 4) {
        const auto& p = f.planes()[3];
        check(near(p.originX,3) && near(p.originY,4) && near(p.originZ,5), "'3, 4, 5' -> (3,4,5)");
    }
    f.run("create plane at 1 , 2 , 3 with rotation 10 0 0");
    check(f.planes().size() == 5, "spaced coord '1 , 2 , 3' creates a plane");
    if (f.planes().size() == 5) {
        const auto& p = f.planes()[4];
        check(near(p.originX,1) && near(p.originY,2) && near(p.originZ,3), "'1 , 2 , 3' -> (1,2,3)");
        check(near(p.primaryAngle,10), "rotation still parses after a spaced coord");
    }

    // coords as formulas / user parameters (the parametric machinery)
    f.run("parameters w 10");
    f.run("create plane at w, 0, w/2 with rotation w+5 0 0");
    check(f.planes().size() == 6, "formula/parameter coords create a plane");
    if (f.planes().size() == 6) {
        const auto& p = f.planes()[5];
        check(near(p.originX,10) && near(p.originZ,5), "at w,0,w/2 -> (10,0,5)");
        check(near(p.primaryAngle,15), "rotation w+5 -> 15");
    }

    // all three rotation slots can be formulas/parameters (w = 10)
    f.run("create plane at 0,0,0 with rotation w, w/2, w-3");
    if (!f.planes().empty()) {
        const auto& p = f.planes().back();
        check(near(p.primaryAngle,10) && near(p.secondaryAngle,5) && near(p.rollAngle,7),
              "rotation w, w/2, w-3 -> 10, 5, 7 (all three are formulas)");
    }

    // unknown reference is refused
    CliResult r4 = f.run("create plane relative to \"Nope\"");
    check(r4.exitCode != 0, "unknown reference plane is refused");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
