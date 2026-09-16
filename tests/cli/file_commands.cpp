// =====================================================================
//  tests/cli/file_commands.cpp — new, open, save and convert act on the
//  real project
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  They used to be demo code: "new" and "save" made a test box, and "open"
//  read a BREP file and threw the shapes away, so no command could write or
//  read a project at all.
#include <filesystem>
#include "tmpdir.h"
#include <hobbycad/strutil.h>
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
    CliResult run(const std::string& line) { return engine.execute(line); }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
};

}  // namespace

int main() {
    std::printf("file commands\n");

    hobbycad::test::TempDir tmp;
    const std::string root = tmp.path();
    Fixture f;

    f.run("create sketch XY Base");
    f.run("rectangle from 0,0 to 10,10");
    f.run("finish");
    ck(f.run("extrude Base 5").exitCode == 0, "a sketch and a body to save");

    ck(f.run("new").exitCode != 0 && f.proj().sketches().size() == 1,
       "new refuses to drop unsaved work");

    ck(f.run(subst("save %1/widget", root)).exitCode == 0, "save to a directory");
    ck(std::filesystem::exists(root + "/widget/widget.hcad"), "the manifest is in widget/");
    ck(!f.proj().isModified(), "and the project reads as saved");

    ck(f.run("new").exitCode == 0 && f.proj().sketches().empty() && f.proj().bodies().empty(),
       "new starts an empty project, with no test box");
    // On the history itself: an undo that fails against the new project also
    // says "Nothing to undo", so the message passed with the history kept.
    ck(f.host.undoDepth() == 0 && f.host.redoDepth() == 0, "and an empty history");

    ck(f.run(subst("open %1/widget", root)).exitCode == 0, "open the project");
    ck(f.proj().sketches().size() == 1 && f.proj().bodies().size() == 1 && f.proj().name() == "widget",
       "its sketch and body are back");
    ck(f.host.hostSession()->timeline().size() == 2, "and so is its timeline");

    ck(f.run(subst("save %1/body.brep", root)).exitCode == 0
       && std::filesystem::exists(root + "/body.brep"), "save .brep writes the bodies");

    // A save in the middle of a sketch is a checkpoint: the sketch goes in.
    f.run("create sketch XZ Side");
    f.run("line from 0,0 to 5,5");
    ck(f.run("save").exitCode == 0, "save during a sketch");
    f.run("discard");
    {
        Project q;
        std::string err;
        ck(q.load((root + "/widget"), &err) && q.sketches().size() == 2,
           "the open sketch was written");
    }

    f.run("rename sketch Base Plate");
    ck(f.run("new").exitCode != 0, "new still refuses once edited");
    ck(f.run("new discard").exitCode == 0 && f.proj().sketches().empty(), "new discard drops the edit");

    ck(f.run(subst("open %1/body.brep", root)).exitCode == 0
       && f.proj().bodies().size() == 1 && f.proj().sketches().empty(), "open .brep brings in its bodies");

    ck(f.run(subst("convert %1/widget %1/conv.brep", root)).exitCode == 0
       && std::filesystem::exists(root + "/conv.brep"), "convert a project to BREP");
    ck(f.run(subst("convert %1/conv.brep %1/fromBrep.hcad", root)).exitCode == 0, "convert BREP to a project");
    {
        Project q;
        std::string err;
        ck(q.load((root + "/fromBrep"), &err) && q.bodies().size() == 1,
           "the converted project has the body");
    }

    std::printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
