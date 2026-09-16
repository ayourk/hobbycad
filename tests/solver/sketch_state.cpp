// The SketchState classification, end to end.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace hobbycad; using namespace hobbycad::sketch;
static int failures=0;
static Constraint C(int id, ConstraintType t, std::vector<int> e, std::vector<int> p={}, double v=0){
    Constraint c; c.id=id;c.type=t;c.entityIds=e;c.pointIndices=p;c.value=v;c.enabled=true;return c; }
static void expect(const char* label, std::vector<Entity> es, std::vector<Constraint> cs,
                   SketchState want, int wantDof /* -1 = don't care */){
    Solver s; SolveResult r = s.solve(es, cs);
    bool ok = (r.state == want) && (wantDof < 0 || r.dof == wantDof);
    if(!ok) ++failures;
    std::printf("  [%s] %-40s state=%-18s dof=%-3d dofKnown=%s\n",
        ok?"PASS":"FAIL", label, sketchStateName(r.state), r.dof,
        r.dofIsKnown()?"yes":"no");
}
int main(){
    auto line = []{ return std::vector<Entity>{createLine(1,{0,0},{10,0})}; };
    auto pinned = []{ return std::vector<Entity>{createLine(1,{0,0},{10,0}), createPoint(2,{0,0})}; };
    auto pin = []{ return std::vector<Constraint>{
        C(1,ConstraintType::FixedPoint,{2}), C(2,ConstraintType::Coincident,{1,2},{0,0}),
        C(3,ConstraintType::Horizontal,{1}), C(4,ConstraintType::Distance,{1,2},{1,0},10.0)}; };

    std::printf("SketchState classification\n");
    expect("empty sketch", {}, {}, SketchState::Empty, 0);
    // THE FIX: geometry, zero constraints -> a real dof, not the -1 sentinel
    expect("line, NO constraints (was -1)", line(), {}, SketchState::UnderConstrained, 4);
    expect("line, partially constrained", pinned(),
           {C(1,ConstraintType::FixedPoint,{2}), C(2,ConstraintType::Coincident,{1,2},{0,0})},
           SketchState::UnderConstrained, 2);
    expect("line, fully pinned", pinned(), pin(), SketchState::FullyConstrained, 0);
    { auto cs=pin(); cs.push_back(C(5,ConstraintType::Horizontal,{1}));
      expect("+ redundant horizontal", pinned(), cs, SketchState::OverConstrained, 0); }
    expect("contradiction (10 and 25)",
           {createPoint(1,{0,0}), createPoint(2,{10,0})},
           {C(1,ConstraintType::Distance,{1,2},{0,0},10.0),
            C(2,ConstraintType::Distance,{1,2},{0,0},25.0)},
           SketchState::Inconsistent, -1);
    // THE OTHER FIX: a driven dimension must not affect the state at all
    { auto cs=pin(); Constraint d=C(9,ConstraintType::Distance,{1,2},{1,0},25.0);
      d.isDriving=false; cs.push_back(d);
      expect("+ DRIVEN dim disagreeing (was Inconsistent)", pinned(), cs,
             SketchState::FullyConstrained, 0); }
    std::printf("\n%s (%d failure(s))\n", failures?"FAILURES":"ALL PASS", failures);
    return failures?1:0;
}
