// =====================================================================
//  tests/cli/set_property.cpp — "set" and "help <command>"
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The properties panel could change an entity's radius, corner or text
//  and the command line could not, although both work on the same model.
//  "set" edits by the panel's names, through the same library rules
//  (sketch/property_schema.h, sketch/properties.h), so the two agree on
//  what can be changed and what is refused.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/strutil.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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

    /// The entity with `id` in the last saved sketch, or null.
    const sketch::Entity* saved(int id) const {
        const auto& sketches = host.hostProject()->sketches();
        if (sketches.empty()) return nullptr;
        return sketch::findEntityById(sketches.back().entities, id);
    }
};

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

/// The first id a creation command reports ("... [id 7]"), or -1; a
/// slot reports its own before its centerline's.
int reportedId(const CliResult& r) {
    const std::size_t at = r.output.find("[id ");
    return at == std::string::npos ? -1 : std::atoi(r.output.c_str() + at + 4);
}

/// True when the command worked; otherwise shows why, for the log.
bool worked(const CliResult& r) {
    if (r.exitCode != 0) std::printf("         %s\n", r.error.c_str());
    return r.exitCode == 0;
}

}  // namespace

int main() {
    // ---- numbers, points and text -------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Parts");
        f.run("circle at 0,0 radius 5");            // 1
        f.run("rectangle 0,0 to 10,5");             // 2
        const int textId = reportedId(f.run("text Hi at 0,0"));
        // A slot also makes its centerline, so its id is read, not assumed.
        const int slotId = reportedId(f.run("slot from 0,0 to 10,0 width 4"));
        ck(textId > 0 && slotId > 0, "the text and the slot report their ids");

        ck(worked(f.run("set 1 radius 12")), "a circle's radius sets");
        ck(worked(f.run("set 1 point0 3,4")), "and its center");
        ck(worked(f.run(subst("set %1 width 10", slotId).c_str())), "a slot's width sets");
        ck(worked(f.run(subst("set %1 text Hello World", textId).c_str())),
           "a text's words set");
        ck(worked(f.run("set 2 width (2*6)")), "a value may be an expression");

        f.run("select 1");
        const CliResult selected = f.run("set diameter 30");
        ck(selected.exitCode == 0 && contains(selected.output, "entity 1"),
           "without an id, the selected entity is set");

        const CliResult list = f.run("set 1");
        ck(list.exitCode == 0 && contains(list.output, "radius")
               && contains(list.output, "diameter"),
           "\"set <id>\" lists the properties");

        f.run("finish");
        const sketch::Entity* circle = f.saved(1);
        const sketch::Entity* rect = f.saved(2);
        const sketch::Entity* slot = f.saved(slotId);
        const sketch::Entity* text = f.saved(textId);
        ck(circle && near(circle->radius, 15.0) && near(circle->points[0].x, 3.0),
           "the circle keeps the last radius and the new center");
        ck(rect && near(std::fabs(rect->points[1].x - rect->points[0].x), 12.0),
           "the rectangle is 12 wide");
        ck(slot && near(slot->radius, 5.0), "the slot is 10 wide, not 20");
        ck(text && text->text == "Hello World", "the text reads as typed");
    }

    // ---- refusals -----------------------------------------------------------------
    {
        Fixture f;
        const CliResult outside = f.run("set 1 radius 3");
        ck(outside.exitCode != 0 && contains(outside.error, "no sketch is open"),
           "outside a sketch, set says where it works");

        f.run("create sketch XY Refusals");
        f.run("circle at 0,0 radius 5");            // 1
        f.run("rectangle 0,0 to 10,5");             // 2

        const CliResult none = f.run("set radius 3");
        ck(none.exitCode != 0 && contains(none.error, "Nothing selected"),
           "with nothing selected and no id, set asks for one");

        const CliResult unknown = f.run("set 1 sides 5");
        ck(unknown.exitCode != 0 && contains(unknown.error, "has no property 'sides'")
               && contains(unknown.error, "radius"),
           "an unknown property is refused with the ones that exist");

        const CliResult negative = f.run("set 1 radius -1");
        ck(negative.exitCode != 0 && contains(negative.error, "positive"),
           "a negative radius is refused");

        const CliResult corner = f.run("set 2 point0 1,1");
        ck(corner.exitCode != 0 && contains(corner.error, "read-only"),
           "a rectangle's corner is read-only, as in the panel");

        const CliResult missing = f.run("set 9 radius 1");
        ck(missing.exitCode != 0 && contains(missing.error, "No entity 9"),
           "an unknown id is named");

        const CliResult badPoint = f.run("set 1 point0 nowhere");
        ck(badPoint.exitCode != 0 && contains(badPoint.error, "Invalid point"),
           "a point that is not x,y is refused");
    }

    // ---- help for one command -----------------------------------------------------
    {
        Fixture f;
        const CliResult circle = f.run("help circle");
        ck(circle.exitCode == 0 && contains(circle.output, "Draw circle")
               && contains(circle.output, "sketch is open"),
           "help names what a command does, from the registry, and where it works");
        const CliResult pwd = f.run("help pwd");
        ck(pwd.exitCode == 0 && contains(pwd.output, "any prompt"),
           "a command with no registry entry still gets its scope");
        const CliResult typo = f.run("help cirlce");
        ck(typo.exitCode != 0 && contains(typo.error, "circle"),
           "a misspelled command gets a suggestion");
        ck(contains(f.run("help").output, "set [<id>] <property> <value>"),
           "the full help lists set");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
