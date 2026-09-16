// =====================================================================
//  tests/cli/model_ops.cpp — 3D from the command line, through the
//  session every front end shares
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Aaron, 2026-09-15: "In reduced mode, The GUI CLI window should be able to
//  manipulate the 3D Stuff that reduced mode can't." A headless host has no
//  viewport either, so what works here works in Reduced mode's terminal.
#include <cstdio>

#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"
#include <hobbycad/project_session.h>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

namespace {

struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};

    Fixture() {
        engine.setDocumentHost(&host);
        engine.setUndoHost(&host);
    }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
};

}  // namespace

int main() {
    std::printf("model operations\n");

    Fixture f;
    ProjectSession& s = *f.host.hostSession();

    f.run("create sketch XY Plate");
    f.run("rectangle from 0,0 to 20,10");
    ck(f.run("finish").exitCode == 0, "finish");
    ck(f.proj().features().size() == 1 && f.proj().features()[0].type == FeatureType::Sketch
       && f.proj().features()[0].id == f.proj().sketches()[0].id,
       "a finished sketch has its feature record");
    ck(s.timeline().size() == 1, "and is on the timeline once");

    ck(f.run("extrude Plate 5").exitCode == 0, "extrude");
    ck(f.proj().bodies().size() == 1, "makes a body with no viewport");
    ck(s.timeline().size() == 2 && s.timeline()[1].type == FeatureType::Extrude, "and an extrude feature");
    ck(f.run("undo").exitCode == 0 && f.proj().bodies().empty() && s.timeline().size() == 1,
       "undo takes both back");
    ck(f.run("redo").exitCode == 0 && f.proj().bodies().size() == 1 && s.timeline().size() == 2,
       "redo restores them");

    ck(f.run("revolve Plate angle 90 about x join").exitCode == 0 && f.proj().bodies().size() == 1,
       "revolve joins into the body");
    // Negative, not zero: OCCT refuses a zero-length extrusion by itself, so
    // a zero check passed with the session's own guard removed.
    ck(f.run("extrude Plate -5").exitCode != 0 && f.proj().bodies().size() == 1,
       "a negative distance is refused");
    ck(f.run("extrude Nope 5").exitCode != 0, "an unknown sketch is refused");
    ck(f.run("extrude Plate 5 sideways").exitCode != 0, "an unknown option is refused");

    ck(f.run("rename sketch Plate Base").exitCode == 0 && s.timeline()[0].name == "Base"
       && f.proj().features()[0].name == "Base", "rename renames the record too");
    ck(f.run("delete sketch Base").exitCode == 0 && f.proj().sketches().empty()
       && f.proj().features().size() == 2, "delete takes the sketch and its record");
    ck(f.run("undo").exitCode == 0 && f.proj().sketches().size() == 1 && f.proj().features().size() == 3
       && s.timeline()[0].name == "Base", "and undo puts both back in place");

    std::printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
