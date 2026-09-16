// findRedundantConstraints(): removal-probing, the Dune 3D technique.
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <algorithm>
#include <vector>
using namespace hobbycad; using namespace hobbycad::sketch;
static int failures=0;
static void check(bool ok,const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++failures; }
static Constraint C(int id, ConstraintType t, std::vector<int> e, std::vector<int> p={}, double v=0){
    Constraint c; c.id=id;c.type=t;c.entityIds=e;c.pointIndices=p;c.value=v;c.enabled=true;return c; }
static std::string list(const std::vector<int>& v){
    std::string s; for(int i:v){ s+=std::to_string(i); s+=" "; } return s.empty()?"(none)":s; }

int main(){
    std::vector<Entity> es{createLine(1,{0,0},{10,0}), createPoint(2,{0,0})};
    std::vector<Constraint> pin{
        C(1,ConstraintType::FixedPoint,{2}), C(2,ConstraintType::Coincident,{1,2},{0,0}),
        C(3,ConstraintType::Horizontal,{1}), C(4,ConstraintType::Distance,{1,2},{1,0},10.0)};

    // 1. clean sketch -> nothing to find, and it must cost only ONE solve
    { Solver s; auto r=s.findRedundantConstraints(es,pin);
      std::printf("clean sketch      -> %s\n", list(r).c_str());
      check(r.empty(), "no candidates on a cleanly constrained sketch"); }

    // 2. one duplicate Horizontal -> BOTH horizontals are valid answers
    { auto cs=pin; cs.push_back(C(5,ConstraintType::Horizontal,{1}));
      Solver s; auto r=s.findRedundantConstraints(es,cs);
      std::printf("dup Horizontal    -> %s\n", list(r).c_str());
      bool has3=std::find(r.begin(),r.end(),3)!=r.end();
      bool has5=std::find(r.begin(),r.end(),5)!=r.end();
      check(!r.empty(), "finds candidates when redundant");
      check(has3 && has5, "flags BOTH horizontals; either may be removed"); }

    // 3. a DRIVEN duplicate is never offered as the thing to delete
    { auto cs=pin; Constraint d=C(6,ConstraintType::Horizontal,{1}); d.isDriving=false;
      cs.push_back(d);
      Solver s; auto r=s.findRedundantConstraints(es,cs);
      std::printf("driven duplicate  -> %s\n", list(r).c_str());
      check(std::find(r.begin(),r.end(),6)==r.end(),
            "a driven constraint is never a candidate"); }

    // 4. the probe must not move the caller's geometry
    { auto cs=pin; cs.push_back(C(5,ConstraintType::Horizontal,{1}));
      auto before=es; Solver s; s.findRedundantConstraints(es,cs);
      bool same=true;
      for(size_t i=0;i<es.size();++i)
        for(size_t k=0;k<es[i].points.size();++k)
          if(es[i].points[k].x!=before[i].points[k].x || es[i].points[k].y!=before[i].points[k].y) same=false;
      check(same, "caller's entities are left untouched"); }

    std::printf("\n%s (%d failure(s))\n", failures?"FAILURES":"ALL PASS", failures);
    return failures?1:0;
}
