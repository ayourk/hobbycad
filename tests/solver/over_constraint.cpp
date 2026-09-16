// checkOverConstrain() must catch BOTH kinds of over-constraint.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <vector>
using namespace hobbycad; using namespace hobbycad::sketch;
static int failures=0;
static Constraint C(int id, ConstraintType t, std::vector<int> e, std::vector<int> p={}, double v=0){
    Constraint c; c.id=id;c.type=t;c.entityIds=e;c.pointIndices=p;c.value=v;c.enabled=true;return c; }
static void expect(const char* label, std::vector<Entity> es, std::vector<Constraint> cs,
                   Constraint add, bool wantFlag, bool wantRedundant){
    Solver s; OverConstraintInfo i = s.checkOverConstrain(es, cs, add);
    bool ok = (i.wouldOverConstrain==wantFlag) && (i.isRedundant==wantRedundant);
    if(!ok) ++failures;
    std::printf("  [%s] %-44s flag=%-3s redundant=%-3s reason=%s\n", ok?"PASS":"FAIL", label,
        i.wouldOverConstrain?"yes":"no", i.isRedundant?"yes":"no",
        i.reason.empty()?"(none)":i.reason.c_str());
}
int main(){
    std::vector<Entity> es{createLine(1,{0,0},{10,0}), createPoint(2,{0,0})};
    std::vector<Constraint> pin{
        C(1,ConstraintType::FixedPoint,{2}), C(2,ConstraintType::Coincident,{1,2},{0,0}),
        C(3,ConstraintType::Horizontal,{1}), C(4,ConstraintType::Distance,{1,2},{1,0},10.0)};

    std::printf("checkOverConstrain()\n");
    // adding something genuinely new to an under-constrained sketch: fine
    expect("new constraint on under-constrained sketch", es,
           {C(1,ConstraintType::FixedPoint,{2})},
           C(9,ConstraintType::Horizontal,{1}), false, false);
    // REDUNDANT: a second horizontal on an already-horizontal, pinned line
    expect("duplicate Horizontal (redundant)", es, pin,
           C(9,ConstraintType::Horizontal,{1}), true, true);
    // CONTRADICTORY: a different distance
    expect("conflicting Distance (contradictory)", es, pin,
           C(9,ConstraintType::Distance,{1,2},{1,0},25.0), true, false);
    std::printf("\n%s (%d failure(s))\n", failures?"FAILURES":"ALL PASS", failures);
    return failures?1:0;
}
