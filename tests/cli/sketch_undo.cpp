// =====================================================================
//  tests/cli/sketch_undo.cpp — undo and redo inside a sketch
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Inside a sketch, "undo" used to reach past it to the document's
//  history, so a mistyped circle could not be taken back without
//  discarding the whole sketch. Each command that edits the sketch is now
//  recorded in the sketch's own history (sketch/edit_session.h), the one
//  the canvas uses, and the document's history is left alone until the
//  sketch is finished.
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

    Fixture() {
        engine.setDocumentHost(&host);
        engine.setUndoHost(&host);
    }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
};

}  // namespace

int main() {
    // ---- geometry comes off and goes back ------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Parts");
        ck(contains(f.run("undo").output, "Nothing to undo"),
           "a new sketch has nothing to undo, and says so rather than touching the document");

        f.run("circle at 0,0 radius 5");
        f.run("line from 0,0 to 10,0");
        const CliResult undone = f.run("undo");
        ck(undone.exitCode == 0 && contains(undone.output, "line from 0,0 to 10,0"),
           "undo names the command it took back");
        ck(contains(undone.output, "circle at 0,0 radius 5"),
           "and the next one in line");

        const CliResult redone = f.run("redo");
        ck(redone.exitCode == 0 && contains(redone.output, "line from 0,0 to 10,0"),
           "redo puts it back");

        const CliResult many = f.run("undo 5");
        ck(contains(many.output, "only 2 of 5"), "undo with a count stops at the start");

        f.run("redo 2");
        f.run("finish");
        const auto& sk = f.proj().sketches().back();
        ck(sk.entities.size() == 2, "what was redone is what is saved");
    }

    // ---- constraints, properties and deletes ------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Edits");
        f.run("line from 0,0 to 10,3");   // 1
        f.run("constrain horizontal 1");
        f.run("set 1 length 20");
        f.run("select 1");
        f.run("delete 1");

        ck(contains(f.run("undo").output, "delete 1"), "a delete comes back");
        ck(contains(f.run("undo").output, "set 1 length 20"), "a property edit comes back");
        ck(contains(f.run("undo").output, "constrain horizontal 1"),
           "a constraint comes off");
        f.run("finish");
        const auto& sk = f.proj().sketches().back();
        ck(sk.constraints.empty() && sk.entities.size() == 1,
           "the saved sketch has the line and no constraint");
        ck(!sk.entities.empty() && sk.entities[0].points[1].x == 10.0,
           "with its original length");
    }

    // ---- a refused command records nothing -----------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Refused");
        f.run("point at 0,0");
        const CliResult bad = f.run("constrain horizontal 9");
        ck(bad.exitCode != 0, "a constraint on a missing entity is refused");
        ck(contains(f.run("undo").output, "point at 0,0"),
           "and undo skips it, taking back the point");
        ck(contains(f.run("undo").output, "Nothing to undo"), "then there is nothing left");
    }

    // ---- the undone selection is forgotten ------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Selected");
        f.run("point at 1,1");
        f.run("select 1");
        f.run("undo");
        const CliResult set = f.run("set point0 2,2");
        ck(set.exitCode != 0 && contains(set.error, "Nothing selected"),
           "undoing the selected entity clears the selection");
    }

    // ---- the document's history is its own ------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY First");
        f.run("point at 0,0");
        f.run("finish");
        ck(f.proj().sketches().size() == 1, "the first sketch is saved");

        f.run("create sketch XY Second");
        ck(contains(f.run("undo").output, "Nothing to undo"),
           "a second sketch does not inherit the first one's history");
        f.run("discard");

        const CliResult doc = f.run("undo");
        ck(doc.exitCode == 0 && f.proj().sketches().empty(),
           "outside a sketch, undo takes back the finished sketch as before");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
