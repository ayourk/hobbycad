// =====================================================================
//  tests/cli/named_coords.cpp — named coordinate triplets
//  A named point (registry like parameters, but 3-vectors) can be used
//  wherever a coordinate is expected; "origin" is built-in.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/strutil.h>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"
#include <hobbycad/project.h>
#include <hobbycad/sketch/entity.h>
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
    CliHistory history; HeadlessDocumentHost host; CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* l) { return engine.execute(l); }
    const Project& proj() const { return *host.hostProject(); }
    const std::vector<ConstructionPlaneData>& planes() const { return proj().constructionPlanes(); }
};
}

int main() {
    std::printf("named coordinates\n");
    Fixture f;

    // define a named coordinate, then use it as a whole plane origin
    CliResult d = f.run("coords corner 10,20,5");
    check(d.exitCode == 0 && contains(d.output, "Named coordinate"), "coords corner 10,20,5 defines it");
    f.run("create plane at corner");
    check(!f.planes().empty() && near(f.planes().back().originX, 10)
          && near(f.planes().back().originY, 20) && near(f.planes().back().originZ, 5),
          "create plane at corner -> (10,20,5)");

    // built-in origin
    f.run("create plane at origin");
    check(near(f.planes().back().originX, 0) && near(f.planes().back().originY, 0)
          && near(f.planes().back().originZ, 0), "create plane at origin -> (0,0,0) built-in");

    // a named coord whose components are formulas/parameters
    f.run("parameters w 10");
    f.run("coords c2 w, w/2, 0");
    f.run("create plane at c2");
    check(near(f.planes().back().originX, 10) && near(f.planes().back().originY, 5),
          "coords c2 = w, w/2, 0 -> plane at (10,5,0)");

    // a named 3D coord used in a 2D sketch point (x,y only)
    f.run("create sketch");
    f.run("point at corner");
    f.run("finish");
    if (!f.proj().sketches().empty() && !f.proj().sketches()[0].entities.empty()) {
        const auto& p0 = f.proj().sketches()[0].entities[0].points[0];
        check(near(p0.x, 10) && near(p0.y, 20), "point at corner -> (10,20) in 2D (drops z)");
    } else check(false, "sketch point created");

    // redefining origin is refused
    check(f.run("coords origin 1,1,1").exitCode != 0, "origin cannot be redefined");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
