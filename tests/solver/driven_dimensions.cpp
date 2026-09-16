// Does a DRIVEN (reference) dimension wrongly constrain the geometry?
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad; using namespace hobbycad::sketch;
static const char* nm(SolveResult::ResultCode c){
    switch(c){case SolveResult::Okay:return "Okay";case SolveResult::Inconsistent:return "Inconsistent";
    case SolveResult::DidntConverge:return "DidntConverge";case SolveResult::TooManyUnknowns:return "TooManyUnknowns";
    case SolveResult::RedundantOkay:return "RedundantOkay";default:return "InternalError";}}
static Constraint C(int id, ConstraintType t, std::vector<int> e, std::vector<int> p={}, double v=0){
    Constraint c; c.id=id;c.type=t;c.entityIds=e;c.pointIndices=p;c.value=v;c.enabled=true;return c; }
int main(){
    // Two points 10 apart, pinned by a DRIVING distance of 10.
    auto mk=[]{ return std::vector<Entity>{createPoint(1,{0,0}), createPoint(2,{10,0})}; };
    auto len=[](const std::vector<Entity>& es){
        return std::hypot(es[1].points[0].x-es[0].points[0].x,
                          es[1].points[0].y-es[0].points[0].y); };

    { auto es=mk(); std::vector<Constraint> cs{
        C(1,ConstraintType::FixedPoint,{1}),
        C(2,ConstraintType::Distance,{1,2},{0,0},10.0) };
      Solver s; auto r=s.solve(es,cs);
      std::printf("driving distance 10 only          : code=%-13s len=%.4f\n", nm(r.resultCode), len(es)); }

    // Now add a DRIVEN (reference) distance claiming 25; it must NOT move
    // anything and must NOT conflict. It only reports what it measures.
    { auto es=mk(); std::vector<Constraint> cs{
        C(1,ConstraintType::FixedPoint,{1}),
        C(2,ConstraintType::Distance,{1,2},{0,0},10.0) };
      Constraint d = C(3,ConstraintType::Distance,{1,2},{0,0},25.0);
      d.isDriving = false;                       // <-- reference only
      cs.push_back(d);
      Solver s; auto r=s.solve(es,cs);
      std::printf("+ DRIVEN distance claiming 25     : code=%-13s len=%.4f  success=%s\n",
                  nm(r.resultCode), len(es), r.success?"yes":"NO");
      bool ok = r.success && std::fabs(len(es)-10.0) < 1e-6;
      std::printf("\n  %s a driven dimension must not drive geometry\n", ok?"[PASS]":"[FAIL]");
      return ok?0:1; }
}
