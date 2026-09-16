// tests/solver/validate_groups.cpp — validateSketch catches dangling group refs.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/sketch/queries.h>
#include <hobbycad/sketch/group.h>
#include <cstdio>
using namespace hobbycad;
using namespace hobbycad::sketch;
static int fails = 0;
static void ck(bool ok, const char* w){ std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok)++fails; }
static Entity line(int id){ Entity e; e.id=id; e.type=EntityType::Line; e.points={{0,0,0},{10,0,0}}; return e; }
int main() {
    std::printf("validateSketch group checks\n");
    std::vector<Entity> ents = { line(1), line(2) };
    std::vector<Constraint> cons;   // none

    // A clean group referencing existing entities: valid.
    { Group g; g.id=1; g.entityIds={1,2};
      auto r = validateSketch(ents, cons, {g});
      ck(r.valid, "clean group is valid"); }

    // A group referencing a non-existent entity: invalid.
    { Group g; g.id=1; g.entityIds={1,99};
      auto r = validateSketch(ents, cons, {g});
      ck(!r.valid, "group with a dangling entity ref is invalid");
      bool named=false; for (auto& e : r.errors) if (e.find("99")!=std::string::npos) named=true;
      ck(named, "the error names the missing entity 99"); }

    // A group with a non-existent child group: invalid.
    { Group g; g.id=1; g.entityIds={1}; g.childGroupIds={7};
      auto r = validateSketch(ents, cons, {g});
      ck(!r.valid, "group with a dangling child-group ref is invalid"); }

    // Passing no groups keeps the old behavior (valid).
    { auto r = validateSketch(ents, cons);
      ck(r.valid, "no groups -> unchanged behavior (valid)"); }

    // A legitimate nested pair with consistent both-ended links: valid.
    { Group p; p.id=1; p.entityIds={1}; p.childGroupIds={2};
      Group c; c.id=2; c.entityIds={2}; c.parentGroupId=1;
      auto r = validateSketch(ents, cons, {p, c});
      ck(r.valid, "a consistent nested parent/child pair is valid"); }

    // Self-parent is corruption.
    { Group g; g.id=1; g.parentGroupId=1;
      auto r = validateSketch(ents, cons, {g});
      ck(!r.valid, "a self-parent group is invalid");
      bool named=false; for (auto& e : r.errors) if (e.find("own parent")!=std::string::npos) named=true;
      ck(named, "the error says the group is its own parent"); }

    // Self-child is corruption.
    { Group g; g.id=1; g.childGroupIds={1};
      auto r = validateSketch(ents, cons, {g});
      ck(!r.valid, "a self-child group is invalid"); }

    // Parent/child links that disagree between the two ends: invalid.
    { Group p; p.id=1; p.childGroupIds={2};   // 1 claims 2 as a child
      Group c; c.id=2; c.parentGroupId=-1;    // but 2 says it has no parent
      auto r = validateSketch(ents, cons, {p, c});
      ck(!r.valid, "a one-sided parent/child link is invalid"); }

    // A parent-group cycle (1<->2, links consistent) is corruption.
    { Group a; a.id=1; a.parentGroupId=2; a.childGroupIds={2};
      Group b; b.id=2; b.parentGroupId=1; b.childGroupIds={1};
      auto r = validateSketch(ents, cons, {a, b});
      ck(!r.valid, "a parent-group cycle is invalid");
      bool named=false; for (auto& e : r.errors) if (e.find("cycle")!=std::string::npos) named=true;
      ck(named, "the error names the cycle"); }

        // ---- Group::locked chain -------------------------------------------
    {
        Group parent; parent.id=1; parent.locked=true;
        Group child;  child.id=2;  child.parentGroupId=1;  child.locked=false;
        Group loose;  loose.id=3;  loose.locked=false;
        std::vector<Group> gs = {parent, child, loose};
        ck(isGroupChainLocked(1, gs),  "a locked group reports locked");
        ck(isGroupChainLocked(2, gs),  "a child of a locked parent reports locked");
        ck(!isGroupChainLocked(3, gs), "an unlocked group is not locked");
        ck(!isGroupChainLocked(-1, gs),"no group (-1) is not locked");
        ck(!isGroupChainLocked(99, gs),"unknown group is not locked");
    }

    if (fails==0) std::printf("validate_groups: ALL PASS\n"); else std::printf("validate_groups: %d FAIL\n", fails);
    return fails ? 1 : 0;
}
