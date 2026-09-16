// Regression: the four entity types that already had solver support.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
using namespace hobbycad; using namespace hobbycad::sketch;
static int failures = 0;
static void check(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) ++failures; }
static bool nr(double a,double b,double e=1e-3){ return std::fabs(a-b)<e; }
int main(){
    { Solver s; std::vector<Entity> es{createPoint(1,{3,4})}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs); std::printf("point DOF=%d\n",d); check(d==2,"point = 2 DOF"); }
    { Solver s; std::vector<Entity> es{createLine(1,{0,0},{10,0})}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs); std::printf("line DOF=%d\n",d); check(d==4,"line = 4 DOF"); }
    { Solver s; std::vector<Entity> es{createCircle(1,{0,0},5)}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs); std::printf("circle DOF=%d\n",d); check(d==3,"circle = 3 DOF"); }
    // INVARIANT: every arc, however it was built, reaches the solver as a
    // real arc. createArc() used to store only a center, which silently took
    // the degenerate-arc fallback and registered a CIRCLE (3 DOF) whose
    // endpoints could not be constrained; it now stores center+start+end like
    // the GUI's three-point path. Both routes must agree.
    { Solver s; std::vector<Entity> es{createArc(1,{0,0},5,0,90)}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs); std::printf("createArc() DOF=%d\n",d);
      check(d==5,"parametric createArc() is a real arc (5 DOF)"); }
    { Solver s; Entity a=createArcFromThreePoints(1,{5,0},{3.5355,3.5355},{0,5});
      std::vector<Entity> es{a}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs); std::printf("3-point arc DOF=%d\n",d);
      check(d==5,"three-point arc = 5 DOF (same as the parametric route)"); }
    // horizontal constraint still works on a line
    { Solver s; std::vector<Entity> es{createLine(1,{0,0},{10,3})};
      Constraint h; h.id=1; h.type=ConstraintType::Horizontal; h.entityIds={1}; h.enabled=true;
      std::vector<Constraint> cs{h};
      SolveResult r=s.solve(es,cs);
      std::printf("horizontal solve: %s  y0=%.4f y1=%.4f\n", r.success?"ok":r.errorMessage.c_str(),
                  es[0].points[0].y, es[0].points[1].y);
      check(r.success && nr(es[0].points[0].y, es[0].points[1].y), "Horizontal still solves a line flat"); }
    std::printf("\n%s (%d failure(s))\n", failures?"FAILURES":"ALL PASS", failures);
    return failures?1:0;
}
