// =====================================================================
//  tests/cli/coord_spaces.cpp
//  Space-tolerant + formula/parameter coordinates are UNIFORM across CLI
//  commands: the tokenizer merges comma-split coordinate fragments, and the
//  shared coord parser accepts bare formulas: no per-command code.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
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
};
}

int main() {
    std::printf("uniform space-tolerant / formula coords\n");
    Fixture f;
    f.run("parameters w 10");
    f.run("create sketch");
    f.run("point at 3, 4");             // spaced coord
    f.run("line from 1, 2 to 5, 6");    // spaced coords, both ends
    f.run("point at w, w/2");           // bare formula + parameter + spaced
    f.run("finish");

    if (f.proj().sketches().empty()) {
        check(false, "sketch committed");
    } else {
        const auto& e = f.proj().sketches()[0].entities;
        check(e.size() == 3, "three entities created via spaced/formula coords");
        if (e.size() == 3) {
            check(near(e[0].points[0].x, 3) && near(e[0].points[0].y, 4),
                  "point at 3, 4 (spaced)");
            check(e[1].type == sketch::EntityType::Line
                  && near(e[1].points[0].x, 1) && near(e[1].points[0].y, 2)
                  && near(e[1].points[1].x, 5) && near(e[1].points[1].y, 6),
                  "line from 1, 2 to 5, 6 (spaced both ends)");
            check(near(e[2].points[0].x, 10) && near(e[2].points[0].y, 5),
                  "point at w, w/2 (bare formula + parameter + spaced)");
        }
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
