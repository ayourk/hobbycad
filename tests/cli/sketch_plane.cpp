// tests/cli/sketch_plane.cpp — "create sketch on <plane>" resolves the plane.
// SPDX-License-Identifier: GPL-3.0-only
//  A sketch created on a construction plane by name must carry that plane's
//  id (plane == Custom, constructionPlaneId set); an unknown name is refused
//  rather than silently making an XY sketch.
#include <cstdio>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"
#include <hobbycad/project.h>
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }
namespace {
struct Fixture {
    CliHistory history; HeadlessDocumentHost host; CliEngine engine{history};
    Fixture(){ engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* l){ return engine.execute(l); }
    const Project& proj() const { return *host.hostProject(); }
};
}
int main() {
    std::printf("sketch plane\n");
    Fixture f;
    ck(f.run("create plane at 0,0,10").exitCode == 0, "create plane (Plane 1)");
    const int planeId = f.proj().constructionPlanes().empty() ? -1 : f.proj().constructionPlanes()[0].id;
    ck(planeId >= 0, "plane has an id");

    // by name, both spellings of the grammar
    ck(f.run("create sketch on \"Plane 1\" S1").exitCode == 0, "create sketch on \"Plane 1\" S1");
    ck(f.run("finish").exitCode == 0, "finish S1");
    ck(!f.proj().sketches().empty()
       && f.proj().sketches().back().plane == SketchPlane::Custom
       && f.proj().sketches().back().constructionPlaneId == planeId,
       "S1 is on Plane 1 (Custom + constructionPlaneId)");

    ck(f.run("create sketch S2 on plane \"plane 1\"").exitCode == 0, "name first, case-insensitive plane");
    ck(f.run("finish").exitCode == 0, "finish S2");
    ck(f.proj().sketches().size() == 2
       && f.proj().sketches().back().constructionPlaneId == planeId,
       "S2 is on Plane 1 too");

    // origin planes still resolve, and keep constructionPlaneId at -1
    ck(f.run("create sketch XZ S3").exitCode == 0 && f.run("finish").exitCode == 0, "create sketch XZ S3");
    ck(f.proj().sketches().back().plane == SketchPlane::XZ
       && f.proj().sketches().back().constructionPlaneId == -1, "S3 is XZ with no construction plane");

    // unknown plane: refused, and no sketch was opened
    CliResult bad = f.run("create sketch on Nope S4");
    ck(bad.exitCode != 0, "unknown plane refused");
    ck(f.run("create sketch XY S4").exitCode == 0, "no sketch was left open by the refusal");
    ck(f.run("discard").exitCode == 0, "discard S4");

    if (fails==0) std::printf("cli sketch_plane: ALL PASS\n"); else std::printf("cli sketch_plane: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
