// =====================================================================
//  tests/solver/redundant_scale.cpp
//  Scale-conditioning probe, motivated by SolveSpace issue #1769
//  ("unexpected size-dependent redundant constraints").
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  #1769: a legitimately non-redundant constraint (a diameter on a circle
//  held tangent to two lines) is FALSELY reported as redundant once the
//  geometry is large enough. The rank test that decides redundancy uses a
//  threshold tied to the largest Jacobian column norm; our equations mix
//  dimensionless/length/area magnitudes, so a genuine pivot can slip under
//  the threshold as the sketch grows. b87bfca (in our 20260904 fork) fixed
//  the Newton *solve* path this way but EXPLICITLY left the *redundancy rank*
//  determination untouched: the path #1769 exercises.
//
//  Two things here, using HobbyCAD's NON-STOCK diagnostics:
//   (1) a regression GUARD: over the practical size range a well-conditioned
//       tangent+diameter system must stay scale-invariant (same dof, same
//       redundancy verdict) AND the rank-based verdict (SketchState /
//       RedundantOkay / failedIds) must agree with findRedundantConstraints()
//       (our removal-probing oracle, which does not use the rank test). A
//       disagreement (rank says redundant, probe finds nothing) is the #1769
//       false positive.
//   (2) an informational conditioning PROBE that pushes scale and wedge angle
//       to locate where the solver first diverges, and CLASSIFIES the failure:
//       OverConstrained/RedundantOkay == the #1769 redundant symptom;
//       Inconsistent/DidntConverge == the b87bfca solve-path family. It never
//       fails the suite; it reports the ceiling.
//
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Circle held tangent to two lines forming a wedge of `angleDeg` at the origin,
// plus a diameter on the circle, whole system scaled by s. Lines pinned so
// the diameter is genuinely non-redundant in a determined system (dof 0).
static void buildSystem(double s, double angleDeg,
                        std::vector<Entity>& es, std::vector<Constraint>& cs)
{
    const double a = angleDeg * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
    es.clear();
    es.push_back(createLine(1, {0.0, 0.0}, {10.0 * s, 0.0}));
    es.push_back(createLine(2, {0.0, 0.0}, {10.0 * s * ca, 10.0 * s * sa}));
    // Seed the circle on the bisector, comfortably inside the wedge.
    const double bis = a / 2.0, r0 = 0.8 * s;
    const double d = r0 / std::sin(bis);
    es.push_back(createCircle(3, {d * std::cos(bis), d * std::sin(bis)}, r0));

    cs.clear();
    auto fixPt = [&](int id, int lineId, int ptIdx) {
        Constraint c; c.id = id; c.type = ConstraintType::FixedPoint;
        c.entityIds = {lineId}; c.pointIndices = {ptIdx}; c.enabled = true; return c;
    };
    cs.push_back(fixPt(1, 1, 0)); cs.push_back(fixPt(2, 1, 1));
    cs.push_back(fixPt(3, 2, 0)); cs.push_back(fixPt(4, 2, 1));
    Constraint t1; t1.id = 5; t1.type = ConstraintType::Tangent; t1.entityIds = {1, 3}; t1.enabled = true;
    Constraint t2; t2.id = 6; t2.type = ConstraintType::Tangent; t2.entityIds = {2, 3}; t2.enabled = true;
    Constraint dia; dia.id = 7; dia.type = ConstraintType::Diameter; dia.entityIds = {3};
    dia.value = 2.0 * r0; dia.enabled = true;
    cs.push_back(t1); cs.push_back(t2); cs.push_back(dia);
}

struct Probe { int dof; bool solved; SketchState state; int code; int nFailed; int nProbe; };
static Probe run1(double s, double angleDeg)
{
    std::vector<Entity> es; std::vector<Constraint> cs;
    buildSystem(s, angleDeg, es, cs);
    Solver solver;
    const int dof = solver.degreesOfFreedom(es, cs);
    std::vector<Entity> a = es; SolveResult r = solver.solve(a, cs);
    std::vector<Entity> b = es; auto probe = solver.findRedundantConstraints(b, cs);
    return { dof, r.success, r.state, (int)r.resultCode,
             (int)r.failedConstraintIds.size(), (int)probe.size() };
}
static bool rankRedundant(const Probe& p) {
    return p.state == SketchState::OverConstrained
        || p.code == (int)SolveResult::RedundantOkay || p.nFailed > 0;
}

int main()
{
    installSolverFatalHandler();
    std::printf("SolveSpace #1769 scale-conditioning test (fork 20260904+p1)\n");

    // (1) GUARD: practical size range, well-conditioned (60-degree wedge).
    std::printf("-- guard: practical range, 60-degree wedge --\n");
    const double scales[] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0};
    Probe base = run1(scales[0], 60.0);
    bool dofStable = true, verdictStable = true, agrees = true;
    for (double s : scales) {
        Probe p = run1(s, 60.0);
        std::printf("   scale %10.0f : dof=%d solved=%s state=%-17s code=%d failedIds=%d probe=%d\n",
                    s, p.dof, p.solved ? "y" : "n", sketchStateName(p.state), p.code, p.nFailed, p.nProbe);
        if (p.dof != base.dof) dofStable = false;
        if (rankRedundant(p) != rankRedundant(base)) verdictStable = false;
        if (rankRedundant(p) && p.nProbe == 0) agrees = false;   // #1769 false positive
    }
    check(dofStable, "practical range: dof is scale-invariant");
    check(verdictStable, "practical range: redundancy verdict is scale-invariant");
    check(agrees, "practical range: rank verdict agrees with the removal-probe oracle (no #1769)");

    // (2) PROBE (informational, never fails the suite): push scale x angle to
    //     find the conditioning ceiling and CLASSIFY the failure mode.
    std::printf("-- probe: conditioning ceiling (informational) --\n");
    const double pScales[] = {1e6, 1e7, 1e8, 1e9};
    const double pAngles[] = {60.0, 15.0, 3.0};
    for (double ang : pAngles) {
        for (double s : pScales) {
            Probe p = run1(s, ang);
            const bool bad = (p.dof != 0) || !p.solved;
            // The oracle decides: rank says redundant but the removal-probe
            // finds nothing truly removable == a CONFIRMED #1769 false positive.
            const char* mode;
            if (!bad)                                    mode = "ok";
            else if (p.solved && rankRedundant(p) && p.nProbe == 0)
                                                          mode = "FALSE-REDUNDANT: #1769 CONFIRMED (rank says redundant, oracle finds none)";
            else if (p.solved && rankRedundant(p))        mode = "genuinely redundant (oracle agrees)";
            else if (!p.solved)                           mode = "INCONSISTENT / solve-breakdown (b87bfca path)";
            else                                          mode = "dof drift";
            std::printf("   angle %5.1f deg  scale %10.0g : dof=%d solved=%s state=%-17s probe=%d -> %s\n",
                        ang, s, p.dof, p.solved ? "y" : "n", sketchStateName(p.state), p.nProbe, mode);
        }
    }

    if (failures == 0) {
        std::printf("redundant_scale: ALL PASS (practical range robust; see probe for the ceiling)\n");
        return 0;
    }
    std::printf("redundant_scale: %d FAILURE(S): scale-dependent redundancy in the practical range\n",
                failures);
    return 1;
}
