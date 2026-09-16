// tests/cli/constrain_edit.cpp — edit an existing constraint from the CLI.
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
    ck(f.run("constrain distance 1.0 1.1 10").exitCode == 0, "add distance constraint (id 1)");
    // edit its value
    CliResult e = f.run("constrain edit 1 value 20");
    ck(e.exitCode == 0, "constrain edit 1 value 20 succeeds");
    ck(contains(f.run("constraints").output, "20"), "the new value shows in the list");
    // formula value
    ck(f.run("parameters w 7").exitCode == 0, "add a parameter");
    ck(f.run("constrain edit 1 value w*2").exitCode == 0, "value accepts a formula");
    ck(contains(f.run("constraints").output, "14"), "w*2 evaluated to 14");
    // a zero distance collapses geometry: refused by the shared rule
    // (isValidConstraintValue), the value left as it was
    ck(f.run("constrain edit 1 value 0").exitCode != 0, "value 0 refused for a distance");
    ck(contains(f.run("constraints").output, "14"), "value unchanged after the refusal");
    // toggle reference / driving
    ck(f.run("constrain edit 1 reference").exitCode == 0, "toggle to reference");
    ck(contains(f.run("constraints").output, "reference"), "shows (reference)");
    ck(f.run("constrain edit 1 driving").exitCode == 0, "toggle back to driving");
    // errors
    ck(f.run("constrain edit 99 value 5").exitCode != 0, "unknown constraint refused");
    ck(f.run("constrain edit 1 value").exitCode != 0, "missing expr refused");
    ck(f.run("constrain edit 1 bogus").exitCode != 0, "unknown field refused");
    if (fails==0) std::printf("cli constrain_edit: ALL PASS\n"); else std::printf("cli constrain_edit: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
