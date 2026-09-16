// =====================================================================
//  tests/cli/roundtrip.cpp — export a sketch, replay it, compare
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  "export" claims to emit a replayable script. The only honest test of
//  that claim is to replay it.
//
//  This is not hypothetical. The emitter wrote arcs as "arc <pt> <pt>
//  <pt>", a form the parser rejects, AND guarded it on the entity having
//  three points when a CLI arc has one, so every arc was silently
//  dropped from the script, and would have failed to parse had it not
//  been. Both bugs are invisible unless the output is fed back in.
#include <fstream>
#include <iterator>
#include <filesystem>
#include "tmpdir.h"
#include <hobbycad/strutil.h>
#include <cstdio>
#include <cmath>


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

/// Everything the CLI can draw, in one sketch.
const char* kScript[] = {
    "create sketch XY every",
    "point at 1,2",
    "line from 0,0 to 10,0 construction",
    "rectangle from 0,0 to 20,10",
    "circle at 5,5 radius 3",
    "arc at 5,5 radius 3 angle 0 to 90",
    "polygon at 0,0 radius 25 sides 6",
    "ellipse at 10,10 major 40 minor 20",
    "slot from 0,0 to 50,0 width 10",
    "spline through 0,0 25,40 50,0",
    "text \"Part A\" at 0,60 size 8 rotation 45",
    // Bezier splines (the handle-spline, distinct from the Catmull-Rom above):
    // they must export as `bezier` and come back with their handles intact.
    "bezier 10,10 out 45 1  13,13 in 225 1",
    "bezier 13,13 out 20 1  16,10 in 135 1",
    // Constraints too: they reference entities by id, so replay only works
    // if the ids come out the same on the far side.
    "constrain horizontal 2",
    "constrain radius 5 3",
    "constrain coincident 2.0 1.0",
    "constrain diameter 5 6 reference",
    "constrain curvature 12.1 13.0",   // G2 between the two beziers
    // Groups too. These were silently dropped by export: the construction
    // lines and constraints came back, but the records tying them together
    // did not. The first is a sweep-angle rig by kind ("sweep"); its name
    // is a label, though the "Sweep Angle" prefix still marks older scripts.
    "group \"Sweep Angle 1\" entities 2,3 constraints 1 locked sweep",
    "group Outer entities 1 groups 1 pivot 3,4",
    "finish",
};

/// Compare two entity lists field by field, for the fields each type uses.
bool sameEntities(const std::vector<SketchEntityData>& a,
                  const std::vector<SketchEntityData>& b,
                  std::string* why) {
    if (a.size() != b.size()) {
        *why = subst("entity count %1 vs %2", a.size(), b.size());
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.type != y.type) {
            *why = subst("entity %1: type differs", i);
            return false;
        }
        if (x.points.size() != y.points.size()) {
            *why = subst("entity %1: point count differs", i);
            return false;
        }
        for (size_t j = 0; j < x.points.size(); ++j) {
            if (!near(x.points[j].x, y.points[j].x) ||
                !near(x.points[j].y, y.points[j].y)) {
                *why = subst("entity %1: point %2 moved", i, j);
                return false;
            }
        }
        if (!near(x.radius, y.radius) ||
            !near(x.majorRadius, y.majorRadius) ||
            !near(x.minorRadius, y.minorRadius) ||
            !near(x.startAngle, y.startAngle) ||
            !near(x.sweepAngle, y.sweepAngle) ||
            !near(x.fontSize, y.fontSize) ||
            !near(x.textRotation, y.textRotation) ||
            x.sides != y.sides ||
            x.text != y.text ||
            x.splineBezier != y.splineBezier ||
            x.isConstruction != y.isConstruction) {
            *why = subst("entity %1: a field differs", i);
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    hobbycad::test::TempDir tmp;
    if (!tmp.isValid()) {
        std::printf("  [FAIL] no temp dir\n");
        return 1;
    }
    const std::string scriptPath = tmp.path() + "/replay.hcs";

    // ---- Build the original, then export it -----------------------------
    std::vector<SketchEntityData> original;
    std::vector<ConstraintData> originalConstraints;
    std::vector<sketch::Group> originalGroups;
    {
        CliHistory history;
        HeadlessDocumentHost host;
        CliEngine engine{history};
        engine.setDocumentHost(&host);

        for (const char* line : kScript) engine.execute(line);

        const auto& sketches = host.hostProject()->sketches();
        ck(sketches.size() == 1, "the original sketch exists");
        if (sketches.empty()) return 1;
        original = sketches[0].entities;
        ck(original.size() == 13,
           "all ten entity kinds, the slot centerline, and two bezier splines");
        originalConstraints = sketches[0].constraints;
        ck(originalConstraints.size() == 5, "and five constraints (incl. curvature)");
        originalGroups = sketches[0].groups;
        ck(originalGroups.size() == 3,
           "two named groups plus the one the slot made for its centerline");

        const CliResult exp = engine.execute(
            subst("export all file=%1", scriptPath));
        ck(exp.exitCode == 0, "export succeeds");
        ck(std::filesystem::exists(scriptPath), "and writes the file");
    }

    // ---- The script must not admit to dropping anything -----------------
    {
        std::ifstream in(scriptPath);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        ck(!contains(text, "no CLI command yet"),
           "and skips nothing: every entity drawn is in the script");
    }

    // ---- Replay it into a fresh document --------------------------------
    {
        CliHistory history;
        HeadlessDocumentHost host;
        CliEngine engine{history};
        engine.setDocumentHost(&host);

        const CliResult rep = engine.execute(
            subst("script %1", scriptPath));
        ck(rep.exitCode == 0, "the exported script replays without error");

        const auto& sketches = host.hostProject()->sketches();
        ck(sketches.size() == 1, "producing one sketch");
        if (!sketches.empty()) {
            std::string why;
            const bool same = sameEntities(original, sketches[0].entities, &why);
            if (!same) std::printf("      (%s)\n", why.c_str());
            ck(same, "and geometry identical to the original");

            const auto& cs = sketches[0].constraints;
            ck(cs.size() == originalConstraints.size(),
               "with every constraint replayed");
            bool constraintsMatch = cs.size() == originalConstraints.size();
            for (size_t i = 0; constraintsMatch && i < cs.size(); ++i) {
                const auto& a = originalConstraints[i];
                const auto& b = cs[i];
                if (a.type != b.type || a.entityIds != b.entityIds ||
                    a.pointIndices != b.pointIndices ||
                    a.isDriving != b.isDriving ||
                    !near(a.value, b.value)) {
                    constraintsMatch = false;
                    std::printf("      (constraint %zu differs)\n", i);
                }
            }
            // Entity ids are what constraints refer to, so this also proves
            // the ids survived the round trip.
            ck(constraintsMatch,
               "identical in type, targets, points, value and driving flag");

            const auto& gs = sketches[0].groups;
            ck(gs.size() == originalGroups.size(), "every group replayed");
            bool groupsMatch = gs.size() == originalGroups.size();
            for (size_t i = 0; groupsMatch && i < gs.size(); ++i) {
                const auto& a = originalGroups[i];
                const auto& b = gs[i];
                if (a.id != b.id || a.name != b.name ||
                    a.entityIds != b.entityIds ||
                    a.constraintIds != b.constraintIds ||
                    a.childGroupIds != b.childGroupIds ||
                    a.locked != b.locked || a.hasPivot != b.hasPivot ||
                    (a.hasPivot && (std::fabs(a.pivot.x - b.pivot.x) > 1e-9 || std::fabs(a.pivot.y - b.pivot.y) > 1e-9))) {
                    groupsMatch = false;
                    std::printf("      (group %zu differs)\n", i);
                }
            }
            // The id matters as much as the members: a child group records
            // its parent by id, and the canvas looks groups up by name.
            ck(groupsMatch,
               "with the same ids, names, members, nesting, lock state and pivot");
        }
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
