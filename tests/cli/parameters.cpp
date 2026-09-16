// =====================================================================
//  tests/cli/parameters.cpp — ParameterEngine is the evaluation authority
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  These check the behaviors the engine adds and the old ad-hoc path lacked,
//  so each one fails meaningfully if evaluation stops going through the engine:
//    * a change PROPAGATES to dependent parameters,
//    * a CIRCULAR definition is refused (and nothing is written),
//    * an INVALID name is refused.
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
struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); engine.setUndoHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
    const ParameterData* param(const char* name) const {
        for (const auto& p : proj().parameters())
            if (p.name == name) return &p;
        return nullptr;
    }
    int count() const { return static_cast<int>(proj().parameters().size()); }
};
}  // namespace

int main() {
    // ---- A change propagates to dependent parameters --------------------
    {
        Fixture f;
        ck(f.run("parameters a 10").exitCode == 0, "a = 10 is added");
        ck(f.run("parameters b a*2").exitCode == 0, "b = a*2 is added");
        ck(f.param("b") && f.param("b")->value == 20.0, "b evaluates to 20");

        ck(f.run("parameters a 30").exitCode == 0, "a is changed to 30");
        ck(f.param("a") && f.param("a")->value == 30.0, "a is now 30");
        ck(f.param("b") && f.param("b")->value == 60.0,
           "b PROPAGATED to 60 (the whole point of the engine)");
    }

    // ---- A self-referential definition is refused -----------------------
    {
        Fixture f;
        const CliResult r = f.run("parameters c c+1");
        ck(r.exitCode != 0, "c = c+1 is rejected");
        ck(contains(r.error, "circular"), "with a circular-definition message");
        ck(f.param("c") == nullptr, "and nothing was written");
    }

    // ---- A mutual cycle is refused, leaving the earlier value intact ----
    {
        Fixture f;
        f.run("parameters a 5");
        ck(f.run("parameters x a").exitCode == 0, "x = a is fine");
        const CliResult r = f.run("parameters a x");   // would make a<->x
        ck(r.exitCode != 0, "a = x is rejected (would be circular)");
        ck(f.param("a") && f.param("a")->value == 5.0,
           "and a keeps its previous value, not a broken one");
    }

    // ---- An invalid identifier is refused (parity with the GUI dialog) --
    {
        Fixture f;
        const CliResult r = f.run("parameters 2bad 5");
        ck(r.exitCode != 0, "a name starting with a digit is rejected");
        ck(f.count() == 0, "and no parameter is created");
    }

    // ---- A plain add/edit still reports the evaluated value -------------
    {
        Fixture f;
        ck(contains(f.run("parameters width 50").output, "50"),
           "adding a plain parameter reports its value");
        // A self-reference is circular (width depends on width) and refused.
        ck(f.run("parameters width width+25").exitCode != 0,
           "a self-referential edit is rejected as circular");
        ck(f.param("width") && f.param("width")->value == 50.0,
           "and width keeps its previous value (50)");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
