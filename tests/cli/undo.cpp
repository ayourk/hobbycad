// =====================================================================
//  tests/cli/undo.cpp — CLI mutations are undoable
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Two gaps met here and made each other worse:
//
//    * DocumentUndoHost had no way to RECORD anything. The CLI could undo
//      what the GUI had done, but nothing the CLI did was ever pushed.
//    * Nothing outside MainWindow implemented DocumentUndoHost, so a
//      --no-gui session answered "no document" to every undo.
//
//  Together that meant "delete sketch Foo" destroyed work with no way
//  back. These tests hold both halves shut.
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
    int sketches() const { return static_cast<int>(proj().sketches().size()); }
};

/// The same engine with NO undo host, as a front end without history.
struct NoUndoFixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};

    NoUndoFixture() { engine.setDocumentHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
};

}  // namespace

int main() {
    // ---- Deleting a sketch is reversible, contents and all --------------
    {
        Fixture f;
        f.run("create sketch XY Keeper");
        f.run("circle at 3,4 radius 7");
        f.run("finish");
        const int idBefore = f.proj().sketches()[0].id;

        ck(f.run("delete sketch Keeper").exitCode == 0, "the sketch deletes");
        ck(f.sketches() == 0, "and is gone");

        ck(f.run("undo").exitCode == 0, "undo succeeds");
        ck(f.sketches() == 1, "the sketch is back");

        // Restoring a NAME is not enough. The id names the file the sketch
        // saves to, and the geometry is the actual work.
        const auto& sk = f.proj().sketches()[0];
        ck(sk.name == "Keeper", "with its name");
        ck(sk.id == idBefore, "its id, which names its file on disk");
        ck(sk.entities.size() == 1, "and its geometry");
        ck(sk.entities.size() == 1 && sk.entities[0].radius == 7.0,
           "down to the values that were typed");
    }

    // ---- redo puts it back again ----------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Gone");
        f.run("finish");
        f.run("delete sketch Gone");
        f.run("undo");
        ck(f.sketches() == 1, "undone");
        ck(f.run("redo").exitCode == 0, "redo succeeds");
        ck(f.sketches() == 0, "and the delete is re-applied");
    }

    // ---- rename is undoable ---------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Original");
        f.run("finish");
        f.run("rename sketch Original Changed");
        ck(f.proj().sketches()[0].name == "Changed", "renamed");

        f.run("undo");
        ck(f.proj().sketches()[0].name == "Original", "and the rename reverses");
    }

    // ---- Creating a sketch is undoable ----------------------------------
    {
        Fixture f;
        f.run("create sketch XY Fresh");
        f.run("circle at 0,0 radius 1");
        f.run("finish");
        ck(f.sketches() == 1, "a finished sketch is in the document");

        f.run("undo");
        ck(f.sketches() == 0, "and undo takes it back out");

        f.run("redo");
        ck(f.sketches() == 1, "redo restores it");
    }

    // ---- Several steps unwind in order ----------------------------------
    {
        Fixture f;
        f.run("create sketch XY One");   f.run("finish");
        f.run("create sketch XY Two");   f.run("finish");
        f.run("rename sketch Two Second");
        f.run("delete sketch One");

        ck(f.sketches() == 1 && f.proj().sketches()[0].name == "Second",
           "after four operations, one sketch named Second");

        f.run("undo");   // un-delete One
        ck(f.sketches() == 2, "undo 1: One is back");
        f.run("undo");   // un-rename
        ck(f.proj().sketches()[1].name == "Two", "undo 2: the rename reverses");
        f.run("undo");   // un-create Two
        ck(f.sketches() == 1, "undo 3: Two is gone");
        f.run("undo");   // un-create One
        ck(f.sketches() == 0, "undo 4: the document is empty again");

        // An empty stack is not an ERROR: it is the same as pressing
        // Ctrl+Z with nothing to undo, so it reports rather than fails.
        const CliResult spent = f.run("undo");
        ck(contains(spent.output, "Nothing to undo"),
           "and a fifth undo says there is nothing left");
        ck(f.sketches() == 0, "without changing anything");
    }

    // ---- The description names what was undone --------------------------
    {
        Fixture f;
        f.run("create sketch XY Named");
        f.run("finish");
        f.run("delete sketch Named");

        const CliResult u = f.run("undo");
        ck(contains(u.output, "Named"),
           "undo says which object it acted on");
        ck(contains(u.output, "Delete"),
           "and what the operation was");
    }

    // ---- A front end with no history says so, and does not pretend ------
    {
        NoUndoFixture f;
        f.run("create sketch XY Doomed");
        f.run("finish");

        const CliResult d = f.run("delete sketch Doomed");
        ck(d.exitCode == 0, "the delete still happens");
        ck(contains(d.output, "CANNOT be undone"),
           "but says plainly that it cannot be undone");

        // The recorders are no-ops without a host, and must not crash or
        // silently claim a history exists.
        // No undo host at all is a different thing from an empty stack,
        // and unlike an empty stack it IS a failure: the caller asked for
        // something the front end cannot provide.
        const CliResult u = f.run("undo");
        ck(u.exitCode != 0, "and undo fails rather than reporting success");
        ck(!u.error.empty(), "with a message saying why");
    }

    // ---- Parameters are undoable too (list-command pattern) -------------
    {
        Fixture f;
        ck(f.run("parameters width 50").exitCode == 0, "a parameter is added");
        ck(f.proj().parameters().size() == 1, "and is in the document");

        f.run("undo");
        ck(f.proj().parameters().empty(), "undo removes the added parameter");
        f.run("redo");
        ck(f.proj().parameters().size() == 1, "redo brings it back");

        // Editing an existing parameter is undoable to its old value.
        f.run("parameters width 80");
        ck(f.proj().parameters().size() == 1 && f.proj().parameters()[0].value == 80.0,
           "the parameter is edited to 80");
        f.run("undo");
        ck(f.proj().parameters().size() == 1 && f.proj().parameters()[0].value == 50.0,
           "undo restores the previous value, not the whole parameter");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
