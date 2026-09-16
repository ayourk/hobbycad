// =====================================================================
//  tests/cli/constraints.cpp — CLI constraint commands
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Two things here are easy to get subtly wrong and hard to notice.
//
//  Redundancy detection only works on an otherwise-DETERMINED system.
//  libslvs does not report a duplicate constraint on an under-constrained
//  sketch, so the fixture below is pinned down first, exactly as
//  tests/solver/redundancy_finder.cpp does. A test built on a bare line
//  would "pass" while proving nothing.
//
//  And the constraint had to carry its real id BEFORE the check ran:
//  libslvs treats handle 0 as invalid and silently drops such a
//  constraint, so an id-less one was tested against a system that did
//  not contain it, and every duplicate came back clean.
#include <hobbycad/strutil.h>
#include <cmath>
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

bool near(double a, double b) { return (a > b ? a - b : b - a) < 1e-9; }

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

    /// A line and a point, pinned so the system is determined. Only then
    /// does libslvs report a redundant constraint at all.
    void pinnedSketch() {
        run("create sketch XY Pinned");
        run("line from 0,0 to 10,0");   // entity 1
        run("point at 0,0");            // entity 2
        run("constrain fixedpoint 2");
        run("constrain coincident 1.0 2.0");
        run("constrain horizontal 1");
        run("constrain distance 1.1 2.0 10mm");
    }
};

}  // namespace

int main() {
    // ---- Constraints reach the sketch, with ids and values --------------
    {
        Fixture f;
        f.pinnedSketch();
        f.run("finish");

        const auto& sk = f.proj().sketches()[0];
        ck(sk.constraints.size() == 4, "all four constraints are stored");
        if (sk.constraints.size() == 4) {
            ck(sk.constraints[0].id == 1 && sk.constraints[3].id == 4,
               "with ids of their own, counting from 1");
            ck(sk.constraints[2].type == sketch::ConstraintType::Horizontal,
               "the types survive");
            ck(sk.constraints[3].value == 10.0,
               "and a dimensional value is stored in mm");
        }
    }

    // ---- Entity and constraint ids are SEPARATE sequences ---------------
    {
        Fixture f;
        f.pinnedSketch();
        f.run("finish");
        const auto& sk = f.proj().sketches()[0];
        // Entity 1 and constraint 1 both exist and are different things.
        ck(sk.entities.size() == 2 && sk.constraints.size() == 4,
           "two entities and four constraints coexist");
        ck(sk.entities[0].id == 1 && sk.constraints[0].id == 1,
           "and both sequences start at 1, so an id needs its kind");
    }

    // ---- Point indices are carried ---------------------------------------
    {
        Fixture f;
        f.pinnedSketch();
        f.run("finish");
        const auto& cs = f.proj().sketches()[0].constraints;
        // "coincident 1.0 2.0": without point indices the solver cannot
        // know WHICH end of the line is meant.
        ck(cs.size() > 1 && cs[1].pointIndices.size() == 2,
           "an <id>.<point> reference stores its point indices");
        ck(cs.size() > 3 && cs[3].pointIndices.size() == 2 &&
           cs[3].pointIndices[0] == 1,
           "and the index is the one that was typed");
    }

    // ---- A redundant constraint is refused -------------------------------
    {
        Fixture f;
        f.pinnedSketch();

        const CliResult dup = f.run("constrain horizontal 1");
        ck(dup.exitCode != 0, "a duplicate on a determined sketch is refused");
        ck(contains(dup.error, "add nothing"),
           "as redundant rather than as a conflict");
        ck(contains(dup.error, "reference"),
           "and the message offers the way to record it anyway");

        f.run("finish");
        ck(f.proj().sketches()[0].constraints.size() == 4,
           "and it did not land in the sketch");
    }

    // ---- ...but a reference constraint is allowed ------------------------
    {
        Fixture f;
        f.pinnedSketch();
        ck(f.run("constrain horizontal 1 reference").exitCode == 0,
           "the same constraint as a reference is accepted");
        f.run("finish");
        const auto& cs = f.proj().sketches()[0].constraints;
        ck(cs.size() == 5 && !cs[4].isDriving,
           "and is stored as non-driving");
    }

    // ---- Bad input is refused --------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Bad");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");

        ck(f.run("constrain wombat 1").exitCode != 0,
           "an unknown constraint type is refused");
        ck(f.run("constrain parallel 1").exitCode != 0,
           "too few entities is refused");
        ck(f.run("constrain parallel 1 99").exitCode != 0,
           "a nonexistent entity is refused");
        ck(f.run("constrain parallel 1 1").exitCode != 0,
           "the same entity twice is refused");
        ck(f.run("constrain radius 1").exitCode != 0,
           "a dimensional constraint with no value is refused");
        ck(f.run("constrain radius 1 0").exitCode != 0,
           "a zero radius is refused");
        ck(f.run("constrain horizontal 1 5").exitCode != 0,
           "a value on a geometric constraint is refused");

        f.run("finish");
        ck(f.proj().sketches()[0].constraints.empty(),
           "and not one of them was stored");
    }

    // ---- Different POINTS of one entity are a legitimate pair ------------
    {
        Fixture f;
        f.run("create sketch XY Self");
        f.run("line from 0,0 to 10,0");
        ck(f.run("constrain coincident 1.0 1.1").exitCode == 0,
           "naming two different points of one entity is allowed");
    }

    // ---- Type names are forgiving ----------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Names");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        ck(f.run("constrain PERPENDICULAR 1 2").exitCode == 0, "case is ignored");
        f.run("delete constraint 1");
        ck(f.run("constrain perp 1 2").exitCode == 0, "a short alias works");
        f.run("delete constraint 2");
        ck(f.run("constrain point_on_line 1 2").exitCode == 0,
           "and underscores are ignored");
    }

    // ---- Deleting an entity takes its constraints with it ----------------
    {
        Fixture f;
        f.run("create sketch XY Cascade");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        f.run("constrain perpendicular 1 2");
        f.run("constrain horizontal 1");

        const CliResult d = f.run("delete 1");
        ck(d.exitCode == 0, "the entity deletes");
        ck(contains(d.output, "constraint"),
           "and says its constraints went too");

        f.run("finish");
        // A constraint naming a deleted entity makes the sketch unsolvable
        // with no visible cause.
        ck(f.proj().sketches()[0].constraints.empty(),
           "leaving no constraint pointing at something that is gone");
    }

    // ---- delete constraint removes only the constraint -------------------
    {
        Fixture f;
        f.run("create sketch XY DelC");
        f.run("line from 0,0 to 10,0");
        f.run("constrain horizontal 1");
        ck(f.run("delete constraint 1").exitCode == 0, "a constraint deletes");
        f.run("finish");
        const auto& sk = f.proj().sketches()[0];
        ck(sk.constraints.empty() && sk.entities.size() == 1,
           "and the entity it referred to is untouched");
    }

    // ---- solve reports the state -----------------------------------------
    {
        Fixture f;
        f.pinnedSketch();
        const CliResult s = f.run("solve");
        ck(s.exitCode == 0, "solve runs");
        ck(contains(s.output, "fully constrained"),
           "and reports a pinned sketch as fully constrained");
        ck(contains(s.output, "dof     0"),
           "with zero degrees of freedom");
    }
    {
        Fixture f;
        f.run("create sketch XY Loose");
        f.run("line from 0,0 to 10,0");
        const CliResult s = f.run("solve");
        ck(contains(s.output, "under-constrained"),
           "and an unpinned sketch as under-constrained");
    }

    // ---- constraints lists them ------------------------------------------
    {
        Fixture f;
        f.pinnedSketch();
        const CliResult c = f.run("constraints");
        ck(contains(c.output, "Horizontal"), "constraints lists them");
        ck(contains(c.output, "10 mm"),
           "with dimensional values in canonical form");

        f.run("finish");
        f.run("select sketch Pinned");
        ck(contains(f.run("constraints").output, "Horizontal"),
           "and works on a selected sketch too, not just an open one");
    }

    // ---- Angles are in degrees, and units are CHECKED --------------------
    //
    // An angle and a length are both "a number with a unit", so nothing
    // else stops "angle 1 2 45mm" storing 45 degrees or "radius 1 25deg"
    // storing 25 millimeters. Both are wrong drawings rather than errors.
    {
        Fixture f;
        f.run("create sketch XY Deg");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");

        ck(f.run("constrain angle 1 2 45").exitCode == 0,
           "a bare number on an angle is degrees");
        f.run("finish");
        const auto& cs = f.proj().sketches()[0].constraints;
        ck(cs.size() == 1 && near(cs[0].value, 45.0),
           "and is stored as 45");
    }
    {
        Fixture f;
        f.run("create sketch XY Suffix");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        ck(f.run("constrain angle 1 2 45deg").exitCode == 0,
           "an explicit degree suffix is accepted");
        f.run("finish");
        const auto& cs = f.proj().sketches()[0].constraints;
        ck(cs.size() == 1 && near(cs[0].value, 45.0), "storing 45 degrees");
    }
    {
        Fixture f;
        f.run("create sketch XY Rad");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        ck(f.run("constrain angle 1 2 0.7853981634rad").exitCode == 0,
           "radians are accepted");
        f.run("finish");
        const auto& cs = f.proj().sketches()[0].constraints;
        // Degrees are the storage convention for angles, the way mm are
        // for lengths, so radians must be CONVERTED, not stored as typed.
        ck(cs.size() == 1 && cs[0].value > 44.9 && cs[0].value < 45.1,
           "and converted to degrees on the way in");
    }
    {
        Fixture f;
        f.run("create sketch XY Mixed");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        f.run("circle at 5,5 radius 2");

        const CliResult a1 = f.run("constrain angle 1 2 45mm");
        ck(a1.exitCode != 0, "a LENGTH unit on an angle is refused");
        ck(contains(a1.error, "degrees"),
           "and the message says what to use instead");

        ck(f.run("constrain radius 3 25deg").exitCode != 0,
           "and an ANGLE unit on a length is refused too");

        f.run("finish");
        ck(f.proj().sketches()[0].constraints.empty(),
           "with neither stored");
    }

    // ---- FixedAngle is angular too ---------------------------------------
    //
    // It was formatted in millimeters, because the display keyed on
    // ConstraintType::Angle rather than on whether the value IS an angle.
    {
        Fixture f;
        f.run("create sketch XY Fixed");
        // NOT a horizontal line. FixedAngle builds a horizontal reference
        // line internally, and constraining an already-horizontal line
        // against it is degenerate: libslvs answers "inconsistent". That
        // is a solver property, not a CLI one; using a horizontal line here
        // would test the failure path while claiming to test formatting.
        f.run("line from 0,0 to 8,6");
        const CliResult applied = f.run("constrain fixedangle 1 30");
        ck(applied.exitCode == 0, "a fixed angle applies to a sloped line");
        const CliResult c = f.run("constraints");
        ck(!contains(c.output, "mm"),
           "a fixed angle is not reported in millimeters");
        ck(contains(c.output, "\xc2\xb0"),
           "but with a degree sign");
    }

    // ---- The degree sign survives being printed --------------------------
    {
        Fixture f;
        f.run("create sketch XY Utf");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        const CliResult a2 = f.run("constrain angle 1 2 45");
        // constraintUnit() returns UTF-8. Reading it as Latin-1 turned the
        // degree sign into two characters.
        ck(!contains(a2.output, "\xc3\x82"),
           "the degree sign is not mangled into mojibake");
    }

    // ---- An angle between two lines actually SOLVES to that angle --------
    //
    // Storing the constraint is not the same as satisfying it. This
    // measures the geometry afterwards rather than trusting the message.
    {
        Fixture f;
        f.run("create sketch XY Ang");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 10,10");   // 45 degrees to start with
        ck(f.run("constrain angle 1 2 30").exitCode == 0,
           "an angle between two lines is accepted");

        const CliResult s = f.run("solve");
        ck(contains(s.output, "Geometry updated"),
           "and the solve succeeds");

        f.run("finish");
        const auto& es = f.proj().sketches()[0].entities;
        ck(es.size() == 2 && es[0].points.size() == 2 && es[1].points.size() == 2,
           "two lines with two endpoints each");
        if (es.size() == 2 && es[0].points.size() == 2 && es[1].points.size() == 2) {
            auto heading = [](const SketchEntityData& e) {
                return std::atan2(e.points[1].y - e.points[0].y,
                                  e.points[1].x - e.points[0].x) * 180.0 / M_PI;
            };
            double between = std::fabs(heading(es[1]) - heading(es[0]));
            while (between > 180.0) between = 360.0 - between;
            ck(between > 29.999 && between < 30.001,
               "and the lines really are 30 degrees apart afterwards");
        }
    }

    // ---- print shows what DEFINES an entity, not just its first point ----
    {
        Fixture f;
        f.run("create sketch XY Show");
        f.run("line from 1,2 to 30,40");
        const CliResult p = f.run("print");
        // A line has two ends. Showing only the first says nothing about
        // where it goes, which is exactly what you need after a solve.
        ck(contains(p.output, "30") &&
           contains(p.output, "40"),
           "a line's far end is shown, not only its start");
    }

    // ---- Midpoint is a two-entity constraint: a point and a line (B7) ----
    //
    // It was listed as needing three entities, so the CLI demanded a third
    // id the solver ignored, and the redundancy check then fired on the
    // malformed constraint, refusing a genuine Midpoint as "already
    // implied" even where the point was nowhere near the middle.
    {
        Fixture f;
        f.run("create sketch XY Mid");
        f.run("line from 0,0 to 100,0");     // entity 1: the line
        f.run("line from 20,20 to 200,200"); // entity 2: carries the point
        const CliResult m = f.run("constrain midpoint 2.0 1");
        ck(m.exitCode == 0, "midpoint takes a point and a line, two ids");
        ck(contains(m.output, "Midpoint"),
           "and is accepted, not refused as already implied");

        // Two free lines are 8 dof; AT_MIDPOINT removes 2.
        const CliResult s = f.run("solve");
        ck(contains(s.output, "dof     6"),
           "and it removes two degrees of freedom");

        f.run("finish");
        const auto& es = f.proj().sketches()[0].entities;
        // Point 2.0 must sit at the midpoint of line 1 after the solve.
        if (es.size() == 2 && es[0].points.size() == 2 && es[1].points.size() == 2) {
            const auto mid = (es[0].points[0] + es[0].points[1]) / 2.0;
            ck(near(es[1].points[0].x, mid.x) && near(es[1].points[0].y, mid.y),
               "the point really lands at the middle of the line");
        }
    }
    {
        // The second entity must be a line: a circle is refused up front,
        // before libslvs can assert on the pairing.
        Fixture f;
        f.run("create sketch XY MidBad");
        f.run("line from 0,0 to 100,0");
        f.run("circle at 50,50 radius 10");
        const CliResult m = f.run("constrain midpoint 1.0 2");
        ck(m.exitCode != 0, "midpoint onto a circle is refused");
        ck(contains(m.error, "must be a line"),
           "with a message naming the reason");
    }

    // ---- A floating rectangle is under-constrained, not "dof 0" (B12) ----
    //
    // The CLI stores a rectangle as one compound entity the solver does not
    // model, which made "solve" report a floating rectangle as fully
    // constrained, the opposite of the truth. The read-out now counts the
    // decomposition (four edges, held square) the GUI stores, while the
    // stored entity stays the single compound (storage parity is separate).
    {
        Fixture f;
        f.run("create sketch XY Rect");
        f.run("rectangle from 0,0 to 100,50");
        const CliResult s = f.run("solve");
        ck(contains(s.output, "under-constrained"),
           "a floating rectangle is under-constrained");
        ck(contains(s.output, "dof     4"),
           "with the four degrees of freedom of position, width and height");
        ck(!contains(s.output, "fully constrained"),
           "and never reads as fully constrained");

        f.run("finish");
        const auto& es = f.proj().sketches()[0].entities;
        ck(es.size() == 1 &&
           es[0].type == sketch::EntityType::Rectangle,
           "the stored entity is still the single compound rectangle");
    }

    // ---- FixedAngle carries weight: it removes a degree of freedom (B8) --
    //
    // The GUI's locked line-angle field now creates exactly this constraint;
    // proving it is load-bearing here proves what the GUI emits.
    {
        Fixture f;
        f.run("create sketch XY Fx");
        f.run("line from 0,0 to 100,50");   // one free line: 4 dof
        ck(contains(f.run("solve").output, "dof     4"),
           "a free line has four degrees of freedom");
        ck(f.run("constrain fixedangle 1 30").exitCode == 0,
           "a fixed angle applies to a sloped line");
        ck(contains(f.run("solve").output, "dof     3"),
           "and fixing its angle removes one");
    }

    // ---- A dimension that references a parameter follows it -------------
    //
    // The expression is stored on the constraint and re-evaluated at solve
    // time, so changing the parameter (or undoing the change) flows into the
    // geometry on the next solve.
    {
        Fixture f;
        f.run("parameters width 60");
        f.run("create sketch XY P");
        f.run("line from 0,0 to 40,0");             // entity 1
        f.run("constrain distance 1.0 1.1 width");  // length = width (60)
        f.run("solve");
        // Change the parameter while the sketch is still open; the next solve
        // re-evaluates the expression-backed dimension.
        f.run("parameters width 100");
        const CliResult s = f.run("solve");
        ck(contains(s.output, "Geometry updated"), "re-solve succeeds");
        f.run("finish");

        const auto& sk = f.proj().sketches()[0];
        ck(!sk.constraints.empty() && sk.constraints[0].expression == "width",
           "the dimension stores its parameter expression");
        ck(!sk.constraints.empty() && sk.constraints[0].value == 100.0,
           "and its value re-evaluated to the changed parameter (100)");
        if (sk.entities.size() == 1 && sk.entities[0].points.size() == 2) {
            const double len = std::hypot(
                sk.entities[0].points[1].x - sk.entities[0].points[0].x,
                sk.entities[0].points[1].y - sk.entities[0].points[0].y);
            ck(std::fabs(len - 100.0) < 1e-6,
               "the line follows the changed parameter (length 100)");
        }
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
