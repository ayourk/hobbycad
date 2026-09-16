// Tangency between two CURVES (circle or arc, any mix).
//
// SolveSpace's CURVE_CURVE_TANGENT reads an arc's center->endpoint
// direction and asserts outright on a full circle, which has no endpoint.
// So circle-circle and arc-circle Tangent used to abort libslvs in
// GenerateEquations ("Unexpected entity types for CURVE_CURVE_TANGENT") and
// leave the sketch stuck. It is now built from the definition (one touch
// point that lies on both curves' circles and is collinear with the two
// centers), using only stock libslvs constraints, no library patch.
//
// This asserts the GEOMETRY, not merely that nothing crashed: after solving
// with the center distance fixed, two externally tangent curves satisfy
// distance(centers) == r1 + r2, and the tangent removes exactly one DOF.
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

static Constraint tangent(int id, int a, int b)
{
    Constraint c; c.id = id; c.type = ConstraintType::Tangent;
    c.entityIds = {a, b}; c.enabled = true; return c;
}
static Constraint centerDistance(int id, int a, int b, double v)
{
    Constraint c; c.id = id; c.type = ConstraintType::Distance;
    c.entityIds = {a, b}; c.pointIndices = {0, 0};
    c.value = v; c.enabled = true; c.isDriving = true; return c;
}

static void tangentPair(const char* name, Entity ca, Entity cb, double centerDist)
{
    installSolverFatalHandler();
    std::vector<Entity> es{ca, cb};
    Solver s0; std::vector<Constraint> none;
    const int before = s0.degreesOfFreedom(es, none);
    std::vector<Constraint> justTangent{ tangent(1, ca.id, cb.id) };
    const int afterTangent = s0.degreesOfFreedom(es, justTangent);
    std::printf("%s DOF: free=%d tangent=%d\n", name, before, afterTangent);
    check(afterTangent == before - 1, "tangent removes exactly one DOF");

    std::vector<Constraint> cs{ tangent(1, ca.id, cb.id),
                                centerDistance(2, ca.id, cb.id, centerDist) };
    Solver s; SolveResult r = s.solve(es, cs);
    check(r.success, "tangent + center distance solves (no abort)");
    const Point2D pa = es[0].points[0], pb = es[1].points[0];
    const double d = std::hypot(pb.x - pa.x, pb.y - pa.y);
    const double rsum = es[0].radius + es[1].radius;
    std::printf("  centers=%.4f  r1+r2=%.4f\n", d, rsum);
    check(std::fabs(d - rsum) < 1e-4, "externally tangent: distance(centers) == r1 + r2");
    check(std::fabs(d - centerDist) < 1e-4, "center distance honored");
}

int main()
{
    tangentPair("circle-circle", createCircle(1, {0, 0}, 30.0),
                                 createCircle(2, {80, 0}, 20.0), 55.0);
    tangentPair("arc-circle", createArc(1, {0, 0}, 30.0, 10.0, 160.0),
                              createCircle(2, {90, 0}, 20.0), 55.0);
    tangentPair("arc-arc", createArc(1, {0, 0}, 30.0, 10.0, 160.0),
                           createArc(2, {95, 0}, 20.0, 70.0, 190.0), 55.0);

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
