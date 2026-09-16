// =====================================================================
//  tests/cli/editing.cpp — select, delete, rename, and error wording
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  delete and rename change or destroy work, and the CLI records no undo
//  entry for either, so "it refused correctly" matters as much as "it
//  worked". Most of the checks here are about refusal.
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

    Fixture() { engine.setDocumentHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }

    /// A saved sketch called `name` with one circle in it.
    void makeSketch(const char* name) {
        run(subst("create sketch XY %1", name).c_str());
        run("circle at 0,0 radius 5");
        run("finish");
    }
};

int sketchCount(const Fixture& f) {
    return static_cast<int>(f.proj().sketches().size());
}

}  // namespace

int main() {
    // ---- Unrecognized commands are classified, not lumped together ------
    {
        Fixture f;

        const CliResult ctx = f.run("circle at 0,0 radius 5");
        ck(ctx.exitCode != 0, "a sketch command outside a sketch fails");
        ck(!contains(ctx.error, "Unknown command"),
           "and is NOT called unknown: it is spelled correctly");
        ck(contains(ctx.error, "no sketch is open"),
           "the message names the real problem: where you are standing");

        const CliResult typo = f.run("cirlce");
        ck(contains(typo.error, "Unknown command"),
           "a typo IS unknown");
        ck(contains(typo.error, "circle"),
           "and the near match is offered");

        const CliResult nonsense = f.run("xyzzy");
        ck(contains(nonsense.error, "Unknown command"),
           "something with no near match is just unknown");
        ck(!contains(nonsense.error, "Did you mean"),
           "with no invented suggestion");
    }

    // ---- select knows which types it can actually resolve ---------------
    {
        Fixture f;
        f.makeSketch("First");

        ck(f.run("select sketch First").exitCode == 0, "a sketch selects");

        // "plane" was absent from select's valid-type list while the lookup
        // below it handled planes, so the branch was unreachable and every
        // "select plane" was refused as an unknown type.
        const CliResult pl = f.run("select plane Nope");
        ck(!contains(pl.error, "Unknown type"),
           "'plane' is a known type, not an unknown one");

        const CliResult face = f.run("select face f1");
        ck(face.exitCode != 0 && contains(face.error, "not supported yet"),
           "a planned type says so, rather than reading as a typo");

        const CliResult junk = f.run("select wombat x");
        ck(contains(junk.error, "Unknown type"),
           "and a genuine non-type is still unknown");
    }

    // ---- Entity selection and deletion inside a sketch ------------------
    {
        Fixture f;
        f.run("create sketch XY Work");
        f.run("circle at 0,0 radius 5");     // id 1
        f.run("line from 0,0 to 9,9");       // id 2
        f.run("point at 3,3");               // id 3

        ck(f.run("select 2").exitCode == 0, "an entity selects by id");
        ck(contains(f.engine.buildPrompt(), "entity 2"),
           "and the prompt says which one");

        ck(f.run("select 99").exitCode != 0, "a missing id is refused");
        ck(f.run("delete 1").exitCode == 0, "an entity deletes by id");

        // Deleting must not renumber what is left. If it did, "delete 2"
        // would afterwards hit a different entity than the one printed.
        f.run("finish");
        const auto& es = f.proj().sketches()[0].entities;
        ck(es.size() == 2, "two entities remain");
        ck(es.size() == 2 && es[0].id == 2 && es[1].id == 3,
           "and the survivors KEEP their ids");
    }

    // ---- Deleting the selected entity clears the selection --------------
    {
        Fixture f;
        f.run("create sketch XY Sel");
        f.run("circle at 0,0 radius 5");
        f.run("select 1");
        f.run("delete 1");
        ck(!contains(f.engine.buildPrompt(), "entity"),
           "the prompt stops naming an entity that no longer exists");
    }

    // ---- Document-level delete ------------------------------------------
    {
        Fixture f;
        f.makeSketch("Keep");
        f.makeSketch("Toss");
        ck(sketchCount(f) == 2, "two sketches");

        ck(f.run("delete sketch Nothing").exitCode != 0,
           "deleting something absent is refused");
        ck(sketchCount(f) == 2, "and nothing is removed");

        ck(f.run("delete sketch Toss").exitCode == 0, "a named sketch deletes");
        ck(sketchCount(f) == 1 &&
           f.proj().sketches()[0].name == "Keep",
           "and the OTHER one survives, by name");

        ck(f.run("delete wombat x").exitCode != 0, "an unknown kind is refused");
    }

    // ---- delete by explicit id ------------------------------------------
    {
        Fixture f;
        f.makeSketch("A");
        f.makeSketch("B");
        const int idOfB = f.proj().sketches()[1].id;

        const CliResult d =
            f.run(subst("delete sketch id=%1", idOfB).c_str());
        ck(d.exitCode == 0, "id= deletes the one with that id");
        ck(sketchCount(f) == 1 && f.proj().sketches()[0].name == "A",
           "and leaves the other");
    }

    // ---- A numeric NAME must not be confused with an id -----------------
    {
        Fixture f;
        f.makeSketch("A");        // gets id 1
        f.makeSketch("B");        // gets id 2
        f.run("rename sketch B 1");   // now a sketch NAMED "1", with id 2

        // "1" is both the name of one sketch and the id of another. Guessing
        // would delete the wrong drawing, and there is no undo.
        const CliResult amb = f.run("delete sketch 1");
        ck(amb.exitCode != 0, "an ambiguous token is refused");
        ck(contains(amb.error, "id="),
           "and the message says how to say which was meant");
        ck(sketchCount(f) == 2, "with nothing deleted in the meantime");
    }

    // ---- rename ----------------------------------------------------------
    {
        Fixture f;
        f.makeSketch("Old");
        f.makeSketch("Other");

        ck(f.run("rename sketch Old New").exitCode == 0, "a sketch renames");
        ck(f.proj().sketches()[0].name == "New", "and the model has the new name");

        ck(f.run("rename sketch New Other").exitCode != 0,
           "renaming onto another sketch's name is refused");
        ck(f.proj().sketches()[0].name == "New", "leaving the name unchanged");

        // '*' marks a sketch as being edited in the prompt, so it cannot be
        // the first character of a name.
        ck(f.run("rename sketch New *Bad").exitCode != 0,
           "an invalid name is refused");

        ck(f.run("rename sketch Nothing X").exitCode != 0,
           "renaming something absent is refused");
    }

    // ---- Renaming the selected sketch keeps the prompt honest -----------
    {
        Fixture f;
        f.makeSketch("Before");
        f.run("select sketch Before");
        f.run("rename sketch Before After");
        ck(contains(f.engine.buildPrompt(), "After"),
           "the prompt follows the rename");
        ck(!contains(f.engine.buildPrompt(), "Before"),
           "and stops showing a name nothing answers to");
    }

    // ---- Deleting the selected sketch clears the prompt -----------------
    {
        Fixture f;
        f.makeSketch("Doomed");
        f.run("select sketch Doomed");
        f.run("delete sketch Doomed");
        ck(!contains(f.engine.buildPrompt(), "Doomed"),
           "the prompt stops naming a deleted sketch");
    }

    // ---- print follows the context --------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Live");
        f.run("circle at 0,0 radius 5");
        f.run("line from 0,0 to 9,9");
        f.run("delete 1");

        const CliResult p = f.run("print");
        // It used to print the document summary here, reporting "sketches 0"
        // while a sketch with geometry in it was open.
        ck(contains(p.output, "Live"),
           "print inside a sketch shows THAT sketch");
        ck(!contains(p.output, "sketches  0"),
           "not a document summary that does not contain it yet");
        // The number print shows must be the one delete and select take.
        ck(contains(p.output, "  2  "),
           "and lists the surviving entity by its id, not its position");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
