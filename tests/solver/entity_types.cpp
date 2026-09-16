// Exercise solver support for the entity types added 2026-08-26.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
    std::printf("solver available: %s\n", Solver::isAvailable() ? "yes" : "NO");
    if (!Solver::isAvailable()) { std::printf("libslvs missing; cannot test\n"); return 2; }

    // ---- 1. baseline: does each new type change DOF at all? ----------
    {
        Solver s;
        std::vector<Entity> es;
        std::vector<Constraint> cs;
        Entity sp = createSpline(1, {{0,0},{10,10},{20,0}});
        es.push_back(sp);
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("spline (3 control points) DOF = %d\n", dof);
        check(dof == 6, "spline contributes 6 DOF (3 free control points)");
    }
    {
        Solver s;
        std::vector<Entity> es;
        std::vector<Constraint> cs;
        es.push_back(createPolygon(1, {5,5}, 10.0, 6));
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("regular polygon DOF = %d\n", dof);
        check(dof == 2, "regular polygon contributes 2 DOF (center only, NOT deformable)");
    }
    {
        Solver s;
        std::vector<Entity> es;
        std::vector<Constraint> cs;
        es.push_back(createEllipse(1, {3,4}, 20.0, 10.0));
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("ellipse DOF = %d\n", dof);
        check(dof == 2, "ellipse contributes 2 DOF (center only)");
    }
    {
        Solver s;
        std::vector<Entity> es;
        std::vector<Constraint> cs;
        es.push_back(createSlot(1, {0,0}, {30,0}, 5.0));
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("linear slot DOF = %d\n", dof);
        check(dof == 4, "linear slot contributes 4 DOF (both centerline ends free)");
    }

    // ---- 2. a regular polygon must TRANSLATE RIGIDLY -----------------
    {
        Solver s;
        std::vector<Entity> es;
        // Regular polygon with explicit vertices, as the GUI stores it.
        Entity poly = createPolygon(1, {0,0}, 10.0, 4);
        for (int i = 0; i < 4; ++i) {
            double a = i * M_PI / 2.0;
            poly.points.push_back({10.0 * std::cos(a), 10.0 * std::sin(a)});
        }
        Entity anchor = createPoint(2, {50, 25});
        es.push_back(poly);
        es.push_back(anchor);

        // Pin the anchor, then make the polygon center coincident with it.
        std::vector<Constraint> cs;
        Constraint fixX; fixX.id = 1; fixX.type = ConstraintType::FixedPoint;
        fixX.entityIds = {2}; fixX.enabled = true;
        cs.push_back(fixX);
        Constraint co; co.id = 2; co.type = ConstraintType::Coincident;
        co.entityIds = {1, 2}; co.pointIndices = {0, 0}; co.enabled = true;
        cs.push_back(co);

        SolveResult r = s.solve(es, cs);
        std::printf("solve result = %s\n", (r.success ? "success" : r.errorMessage.c_str()));

        const Entity& p = es[0];
        std::printf("  polygon center now (%.3f, %.3f)\n", p.points[0].x, p.points[0].y);
        check(near(p.points[0].x, 50.0, 1e-3) && near(p.points[0].y, 25.0, 1e-3),
              "polygon center moved to the anchor");

        // Rigidity: every vertex must still be `radius` from the center.
        bool rigid = true;
        for (std::size_t i = 1; i < p.points.size(); ++i) {
            double dx = p.points[i].x - p.points[0].x;
            double dy = p.points[i].y - p.points[0].y;
            double d = std::sqrt(dx*dx + dy*dy);
            if (!near(d, 10.0, 1e-3)) { rigid = false;
                std::printf("  vertex %zu is %.4f from center, expected 10\n", i, d); }
        }
        check(rigid, "polygon stayed REGULAR after solving (vertices rode along)");
    }

    // ---- 3. a freeform polygon MAY deform (that is its semantics) ----
    {
        Solver s;
        std::vector<Entity> es;
        Entity ff;
        ff.id = 1; ff.type = EntityType::Polygon; ff.radius = 0.0; ff.sides = 3;
        ff.points = {{0,0},{10,0},{5,8}};
        es.push_back(ff);
        std::vector<Constraint> cs;
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("freeform polygon (3 vertices) DOF = %d\n", dof);
        check(dof == 6, "freeform polygon contributes 6 DOF (all vertices free)");
        check(!isRegularPolygon(ff), "isRegularPolygon() says freeform");
        Entity reg = createPolygon(2, {0,0}, 10.0, 6);
        check(isRegularPolygon(reg), "isRegularPolygon() says regular");
    }

    // ---- 4. arc slot must stay rigid ---------------------------------
    {
        Solver s;
        std::vector<Entity> es;
        Entity as = createArcSlot(1, {0,0}, {10,0}, {0,10}, 3.0);
        es.push_back(as);
        std::vector<Constraint> cs;
        int dof = s.degreesOfFreedom(es, cs);
        std::printf("arc slot DOF = %d\n", dof);
        check(dof == 2, "arc slot contributes 2 DOF (center only, endpoints rigid)");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
