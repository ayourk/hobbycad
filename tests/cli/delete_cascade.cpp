// tests/cli/delete_cascade.cpp — deleting an entity cascades the same way
// in the CLI as in the canvas (sketch::deleteEntities).
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/strutil.h>
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
    std::printf("delete cascade\n");
    Fixture f;
    ck(f.run("create sketch XY S").exitCode == 0, "create sketch");
    ck(f.run("line from 0,0 to 10,0").exitCode == 0, "line 1");
    ck(f.run("line from 10,0 to 10,10").exitCode == 0, "line 2");
    ck(f.run("line from 20,0 to 30,0").exitCode == 0, "line 3");
    ck(f.run("constrain perpendicular 1 2").exitCode == 0, "constraint 1 names 1 and 2");
    ck(f.run("constrain horizontal 3").exitCode == 0, "constraint 2 names 3");
    ck(f.run("group Corner entities 1,2 constraints 1").exitCode == 0, "group Corner");
    ck(f.run("group Lone entities 3 constraints 2").exitCode == 0, "group Lone");

    // Delete line 2: constraint 1 goes, and its id leaves group Corner's list.
    CliResult d = f.run("delete 2");
    ck(d.exitCode == 0, "delete 2");
    ck(contains(d.output, "1 constraint(s)"), "reports the dropped constraint");

    // Delete line 3: group Lone is emptied and removed.
    CliResult d3 = f.run("delete 3");
    ck(d3.exitCode == 0, "delete 3");
    ck(contains(d3.output, "group(s) left empty"), "reports the removed group");
    ck(f.run("delete 99").exitCode != 0, "unknown id refused");

    // What was saved is what the cascade left.
    ck(f.run("finish").exitCode == 0 && !f.proj().sketches().empty(), "finish");
    const SketchData& sk = f.proj().sketches().back();
    ck(sk.entities.size() == 1, "one entity left");
    ck(sk.constraints.empty(), "no constraints left");
    bool cornerOk = false, loneGone = true;
    for (const auto& g : sk.groups) {
        if (g.name == "Corner") cornerOk = (g.entityIds.size() == 1 && g.constraintIds.empty());
        if (g.name == "Lone") loneGone = false;
    }
    ck(cornerOk, "group Corner keeps line 1 and no dangling constraint id");
    ck(loneGone, "group Lone is gone");
    if (fails==0) std::printf("cli delete_cascade: ALL PASS\n"); else std::printf("cli delete_cascade: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
