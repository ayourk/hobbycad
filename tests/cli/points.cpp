// tests/cli/points.cpp — the `points` command lists an entity's point indices.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/strutil.h>
#include <cstdio>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"
using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }
namespace {
struct Fixture {
    CliHistory history; HeadlessDocumentHost host; CliEngine engine{history};
    Fixture(){ engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* l){ return engine.execute(l); }
};
}
int main() {
    Fixture f;
    ck(f.run("create sketch XY S").exitCode == 0, "create sketch");
    ck(f.run("line from 0,0 to 10,0").exitCode == 0, "line (entity 1)");
    CliResult r = f.run("points 1");
    ck(r.exitCode == 0, "points 1 succeeds");
    ck(contains(r.output, "1.0") && contains(r.output, "1.1"),
       "lists 1.0 and 1.1 point references");
    ck(contains(r.output, "2 point"), "reports two points");
    ck(f.run("points 99").exitCode != 0, "unknown entity refused");
    ck(f.run("points abc").exitCode != 0, "non-numeric id refused");
    ck(f.run("points").exitCode != 0, "missing arg refused");
    if (fails==0) std::printf("cli points: ALL PASS\n"); else std::printf("cli points: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
