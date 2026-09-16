// createArc() must now produce a real solver arc, with endpoints that agree
// with its parametric fields.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
using namespace hobbycad; using namespace hobbycad::sketch;
static int failures=0;
static void check(bool ok,const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++failures; }
static bool nr(double a,double b,double e=1e-9){ return std::fabs(a-b)<e; }
int main(){
    Entity a = createArc(1, {0,0}, 5.0, 0.0, 90.0);
    std::printf("points=%zu  start=(%.6f,%.6f) end=(%.6f,%.6f)\n",
        a.points.size(), a.points[1].x, a.points[1].y, a.points[2].x, a.points[2].y);
    check(a.points.size()==3, "createArc() stores center + start + end");
    check(nr(a.points[1].x,5.0,1e-9) && nr(a.points[1].y,0.0,1e-9), "start point at 0 degrees is (5,0)");
    check(nr(a.points[2].x,0.0,1e-9) && nr(a.points[2].y,5.0,1e-9), "end point at 90 degrees is (0,5)");
    // endpoints must be exactly `radius` from the center
    for (int i=1;i<3;++i){
        double dx=a.points[i].x-a.points[0].x, dy=a.points[i].y-a.points[0].y;
        check(nr(std::sqrt(dx*dx+dy*dy), 5.0, 1e-9), i==1?"start is radius from center":"end is radius from center");
    }
    // now the solver must see a real ARC (5 DOF), not the circle fallback (3)
    { Solver s; std::vector<Entity> es{a}; std::vector<Constraint> cs;
      int d=s.degreesOfFreedom(es,cs);
      std::printf("createArc() solver DOF=%d\n", d);
      check(d==5, "createArc() now registers as a real arc (5 DOF, was 3)"); }
    // a negative sweep (clockwise) must still round-trip
    { Entity cw = createArc(2, {0,0}, 5.0, 90.0, -90.0);
      std::printf("cw start=(%.6f,%.6f) end=(%.6f,%.6f)\n",
          cw.points[1].x, cw.points[1].y, cw.points[2].x, cw.points[2].y);
      check(nr(cw.points[1].x,0.0,1e-9)&&nr(cw.points[1].y,5.0,1e-9), "clockwise start at 90 degrees");
      check(nr(cw.points[2].x,5.0,1e-9)&&nr(cw.points[2].y,0.0,1e-9), "clockwise end at 0 degrees");
      Solver s; std::vector<Entity> es{cw}; std::vector<Constraint> cs;
      SolveResult r=s.solve(es,cs);
      std::printf("cw solve: %s sweep=%.4f\n", r.success?"ok":r.errorMessage.c_str(), es[0].sweepAngle);
      check(r.success && es[0].sweepAngle < 0.0, "clockwise arc keeps a negative sweep through a solve"); }
    // endpoint constrainable to a fixed point, impossible before this change
    { Entity arc = createArc(1,{0,0},10.0,0.0,90.0);
      Entity pt  = createPoint(2,{0,20});
      std::vector<Entity> es{arc,pt};
      Constraint fx; fx.id=1; fx.type=ConstraintType::FixedPoint; fx.entityIds={2}; fx.enabled=true;
      Constraint co; co.id=2; co.type=ConstraintType::Coincident;
      co.entityIds={1,2}; co.pointIndices={2,0}; co.enabled=true;   // arc END to the point
      std::vector<Constraint> cs{fx,co};
      Solver s; SolveResult r=s.solve(es,cs);
      const Entity& A=es[0];
      double dx=A.points[2].x-20e-9, dy=A.points[2].y;
      (void)dx;(void)dy;
      double gap=std::hypot(A.points[2].x-0.0, A.points[2].y-20.0);
      std::printf("endpoint solve: %s  arc end=(%.4f,%.4f) gap=%.3e radius=%.4f\n",
                  r.success?"ok":r.errorMessage.c_str(), A.points[2].x, A.points[2].y, gap, A.radius);
      check(r.success && gap < 1e-6, "arc ENDPOINT is constrainable (was impossible: no endpoints)");
      double rs=std::hypot(A.points[1].x-A.points[0].x, A.points[1].y-A.points[0].y);
      double re=std::hypot(A.points[2].x-A.points[0].x, A.points[2].y-A.points[0].y);
      check(nr(rs,re,1e-6), "arc stayed circular (both endpoints equidistant from center)"); }
    std::printf("\n%s (%d failure(s))\n", failures?"FAILURES":"ALL PASS", failures);
    return failures?1:0;
}
