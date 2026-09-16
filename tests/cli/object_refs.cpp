// tests/cli/object_refs.cpp — select, rename and delete resolve a document
// object the same way (hobbycad::resolveObjectRef): name first, then id,
// "id=<n>" explicit, ambiguity refused.
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
    std::printf("object refs\n");
    Fixture f;
    ck(f.run("create sketch XY First").exitCode == 0 && f.run("finish").exitCode == 0, "sketch First");
    ck(f.run("create sketch XY Second").exitCode == 0 && f.run("finish").exitCode == 0, "sketch Second");
    ck(f.proj().sketches().size() == 2, "two sketches");
    const int idSecond = f.proj().sketches()[1].id;

    CliResult byId = f.run(subst("select sketch id=%1", idSecond).c_str());
    ck(byId.exitCode == 0 && contains(byId.output, "'Second'"), "select sketch id=<n> resolves to Second");
    CliResult bare = f.run(subst("select sketch %1", idSecond).c_str());
    ck(bare.exitCode == 0 && contains(bare.output, "'Second'"), "a bare id selects too");
    ck(f.run("select sketch First").exitCode == 0, "by name");
    ck(f.run("select sketch Nope").exitCode != 0, "unknown name refused");
    ck(f.run("select sketch id=999").exitCode != 0, "unknown id refused");
    ck(f.run("select sketch id=abc").exitCode != 0, "non-numeric id refused");

    // A sketch NAMED like another sketch's id is ambiguous as a bare token.
    ck(f.run("deselect").exitCode == 0, "deselect");
    const std::string clash = numToString(f.proj().sketches()[0].id);
    ck(f.run(subst("rename sketch Second %1", clash).c_str()).exitCode == 0,
       "rename Second to the first sketch's id number");
    CliResult amb = f.run(subst("select sketch %1", clash).c_str());
    ck(amb.exitCode != 0 && contains(amb.error, "id="), "the bare token is refused as ambiguous");
    ck(f.run(subst("select sketch id=%1", clash).c_str()).exitCode == 0,
       "id=<n> still picks the one with that id");

    ck(f.run("rename sketch First Second2").exitCode == 0, "rename by name");
    ck(f.run("rename plane Nope X").exitCode != 0, "no planes: refused");
    if (fails==0) std::printf("cli object_refs: ALL PASS\n"); else std::printf("cli object_refs: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
