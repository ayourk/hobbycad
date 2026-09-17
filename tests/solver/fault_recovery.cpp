// =====================================================================
//  tests/solver/fault_recovery.cpp — a libslvs fault is survived, via the API
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The HobbyCAD libslvs hands a kernel assertion back to the host (fatal
//  handler, patches 0002/0003) instead of aborting. This proves the whole
//  chain from HobbyCAD's side: Solver::solve() returns InternalError with a
//  message, the caller's entities are untouched, and the NEXT solve through
//  the same Solver is clean (SK is a file-scope global in libslvs; a botched
//  teardown would poison it rather than merely leak).
//
//  The fault is provoked with setSolverFaultInjectionForTesting(): HobbyCAD's
//  operand checks now stop every known real input from reaching a kernel
//  assertion, which is good, and it is why the container test that leaned
//  on one (a circle-circle Tangent) went stale. The hook is inert without a
//  line entity, which the last case pins.
// =====================================================================
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
static Constraint horizontal(int id, int lineId)
{
    Constraint c; c.id = id; c.type = ConstraintType::Horizontal;
    c.entityIds = {lineId}; c.enabled = true; return c;
}
static Constraint radius(int id, int circleId, double r)
{
    Constraint c; c.id = id; c.type = ConstraintType::Radius;
    c.entityIds = {circleId}; c.value = r; c.isDriving = true; c.enabled = true; return c;
}

int main()
{
    installSolverFatalHandler();
    std::printf("solver fault recovery through the public API\n");
    std::printf("  handler available: %s, can recover: %s\n",
                solverFatalHandlerAvailable() ? "yes" : "no",
                solverCanRecoverFromFaults() ? "yes" : "no");
    if (!solverCanRecoverFromFaults()) {
        std::printf("    [note] linked libslvs cannot recover from faults: an injected fault would "
                    "abort, so this test is skipped by design\n");
        // A skip is not a pass: say which it was. Exit 0 keeps a build
        // against a stock libslvs (no recovery) green by design.
        std::printf("\nSKIPPED (nothing checked)\n");
        return 0;
    }
    check(!solverFaultInjectionForTesting(), "fault injection is OFF by default");

    Solver solver;

    // --- control: the same sketch solves with the hook off -------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}) };
        std::vector<Constraint> cs{ horizontal(1, 1) };
        SolveResult r = solver.solve(es, cs);
        check(r.success, "control: a line + Horizontal solves with injection off");
    }

    // --- the fault ------------------------------------------------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}) };
        std::vector<Constraint> cs{ horizontal(1, 1) };
        setSolverFaultInjectionForTesting(true);
        SolveResult r = solver.solve(es, cs);
        setSolverFaultInjectionForTesting(false);
        std::printf("  faulted solve: success=%d code=%d message=\"%s\"\n",
                    r.success ? 1 : 0, static_cast<int>(r.resultCode), r.errorMessage.c_str());
        check(!r.success, "the solve reported failure");
        check(r.resultCode == SolveResult::InternalError, "resultCode == InternalError");
        check(!r.errorMessage.empty(), "an error message was set");
        check(std::fabs(es[0].points[1].y - 3.0) < 1e-12
              && std::fabs(es[0].points[1].x - 10.0) < 1e-12,
              "the caller's entities were left untouched (line end still at (10,3))");
    }

    // --- the next solve, same Solver, must be clean ---------------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}) };
        std::vector<Constraint> cs{ horizontal(1, 1) };
        SolveResult r = solver.solve(es, cs);
        check(r.success, "the next solve through the same Solver succeeds");
        check(std::fabs(es[0].points[0].y - es[0].points[1].y) < 1e-9,
              "and its constraint was applied (line is horizontal): "
              "the kernel was torn down cleanly");
    }

    // --- the hook is inert when there is no line to hang the fault on ---
    {
        std::vector<Entity> es{ createCircle(1, {0, 0}, 5.0) };
        std::vector<Constraint> cs{ radius(1, 1, 7.0) };
        setSolverFaultInjectionForTesting(true);
        SolveResult r = solver.solve(es, cs);
        setSolverFaultInjectionForTesting(false);
        check(r.success && std::fabs(es[0].radius - 7.0) < 1e-9,
              "with no line in the sketch the hook injects nothing (circle radius solves to 7)");
    }

    // --- and the hole the old test relied on is closed -------------------
    {
        std::vector<Entity> es{ createLine(1, {0, 0}, {10, 3}) };
        std::vector<Constraint> cs{ radius(1, 1, 4.0) };   // a Radius on a LINE
        SolveResult r = solver.solve(es, cs);
        check(r.resultCode != SolveResult::InternalError,
              "a Radius on a line is refused by HobbyCAD, not faulted in libslvs");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
