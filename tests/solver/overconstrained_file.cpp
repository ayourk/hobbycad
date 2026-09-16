// =====================================================================
//  tests/solver/overconstrained_file.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  An over-constrained sketch must SURVIVE a save/load round trip and still
//  be diagnosable afterwards.
//
//  This is the scenario the redundancy finder actually exists for. Every
//  interactive path now refuses to create a redundancy (the Dimension tool
//  offers a driven dimension instead, and suggestConstraints() omits
//  relations that already hold), so a sketch can only ARRIVE
//  over-constrained: loaded from a file written by an older build, produced
//  by decomposition or locked dimension fields (both of which skip the
//  insert-time check), or imported. If redundancy did not round-trip, the
//  finder would be unreachable in the one case that matters.
// =====================================================================
#include <hobbycad/project.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static ConstraintData C(int id, ConstraintType t, std::vector<int> ents,
                        std::vector<int> pts = {}, double v = 0.0) {
    ConstraintData c;
    c.id = id; c.type = t; c.entityIds = std::move(ents);
    c.pointIndices = std::move(pts); c.value = v; c.enabled = true;
    return c;
}

/// The sketch under test: a line pinned to a point, made horizontal, and
/// given a length, then a SECOND Horizontal that says nothing new.
static SketchData makeOverConstrainedSketch() {
    SketchData s;
    s.name = "OverConstrained";
    s.entities.push_back(createLine(1, {0, 0}, {10, 0}));
    s.entities.push_back(createPoint(2, {0, 0}));
    s.constraints.push_back(C(1, ConstraintType::FixedPoint, {2}));
    s.constraints.push_back(C(2, ConstraintType::Coincident, {1, 2}, {0, 0}));
    s.constraints.push_back(C(3, ConstraintType::Horizontal, {1}));
    s.constraints.push_back(C(4, ConstraintType::Distance, {1, 2}, {1, 0}, 10.0));
    s.constraints.push_back(C(5, ConstraintType::Horizontal, {1}));   // redundant
    return s;
}

static void solveAndReport(const SketchData& s, const char* label,
                           SketchState* stateOut, std::vector<int>* candOut) {
    std::vector<Entity> es(s.entities.begin(), s.entities.end());
    std::vector<Constraint> cs;
    for (const ConstraintData& c : s.constraints) {
        Constraint lc;
        lc.id = c.id; lc.type = c.type; lc.entityIds = c.entityIds;
        lc.pointIndices = c.pointIndices; lc.value = c.value;
        lc.isDriving = c.isDriving; lc.enabled = c.enabled;
        cs.push_back(lc);
    }
    Solver solver;
    std::vector<Entity> probe = es;
    const SolveResult r = solver.solve(probe, cs);
    *stateOut = r.state;
    *candOut = solver.findRedundantConstraints(es, cs);
    std::printf("  %-18s state=%-18s dof=%d  candidates=%zu\n",
                label, sketchStateName(r.state), r.dof, candOut->size());
}

int main() {
    if (!Solver::isAvailable()) {
        std::printf("libslvs unavailable; cannot test\n");
        return 2;
    }
    std::printf("over-constrained sketch, save/load round trip\n");

    const SketchData original = makeOverConstrainedSketch();
    SketchState st0; std::vector<int> cand0;
    solveAndReport(original, "before save", &st0, &cand0);
    check(st0 == SketchState::OverConstrained, "sketch starts over-constrained");
    check(cand0.size() == 2, "two candidates before saving");

    // --- round trip through a real project on disk ---
    std::string dir = "/tmp/hobbycad_ocfile_test";
    std::string rm = "rm -rf " + dir;
    if (std::system(rm.c_str()) != 0) { /* first run: nothing to remove */ }

    Project out;
    out.addSketch(original);
    std::string err;
    const bool saved = out.save(dir, &err);
    check(saved, saved ? "project saved" : ("project save FAILED: " + err).c_str());
    if (!saved) { std::printf("\nFAILURES (%d)\n", ++failures); return 1; }

    Project in;
    const bool loaded = in.load(dir, &err);
    check(loaded, loaded ? "project loaded" : ("project load FAILED: " + err).c_str());
    if (!loaded) { std::printf("\nFAILURES (%d)\n", ++failures); return 1; }

    check(in.sketches().size() == 1, "one sketch came back");
    if (in.sketches().empty()) { std::printf("\nFAILURES (%d)\n", ++failures); return 1; }
    const SketchData& back = in.sketches().front();

    check(back.entities.size() == original.entities.size(), "entity count round-tripped");
    check(back.constraints.size() == original.constraints.size(),
          "constraint count round-tripped, INCLUDING the redundant one");

    SketchState st1; std::vector<int> cand1;
    solveAndReport(back, "after load", &st1, &cand1);

    check(st1 == SketchState::OverConstrained,
          "STILL over-constrained after load, so the finder is reachable");
    check(cand1.size() == cand0.size(), "same number of candidates as before saving");

    std::sort(cand0.begin(), cand0.end());
    std::sort(cand1.begin(), cand1.end());
    check(cand0 == cand1, "the SAME constraints are identified after reload");

    const bool bothHoriz = std::find(cand1.begin(), cand1.end(), 3) != cand1.end()
                        && std::find(cand1.begin(), cand1.end(), 5) != cand1.end();
    check(bothHoriz, "both Horizontals flagged; either may be removed");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
