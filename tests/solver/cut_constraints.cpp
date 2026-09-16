// tests/solver/cut_constraints.cpp
//   computeCutConstraints: split join + trim/extend point-on-object.
//   The shared library rule behind the GUI (and future CLI) trim/split/extend,
//   so a cut never leaves geometry disconnected or a free endpoint.
// SPDX-License-Identifier: GPL-3.0-only
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <cstdio>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails;
}
static Entity line(int id, double x0, double y0, double x1, double y1) {
    Entity e; e.id = id; e.type = EntityType::Line;
    e.points = {{x0, y0, 0}, {x1, y1, 0}}; return e;
}
static int nid = 1000;
static int gen() { return nid++; }

int main() {
    std::printf("computeCutConstraints\n");

    // 1. Split: two pieces meeting at (5,0) -> one Coincident join.
    { nid = 1000;
      std::vector<Entity> pieces = { line(1,0,0,5,0), line(2,5,0,10,0) };
      auto cs = computeCutConstraints(pieces, {}, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::Coincident, "split -> one Coincident join");
      ck(cs.size()==1 && cs[0].entityIds.size()==2
         && cs[0].entityIds[0]==1 && cs[0].entityIds[1]==2, "join ties the two pieces");
      ck(cs.size()==1 && cs[0].pointIndices.size()==2
         && cs[0].pointIndices[0]==1 && cs[0].pointIndices[1]==0, "join uses the shared endpoints"); }

    // 2. Trim onto a line boundary -> PointOnLine.
    { nid = 1000;
      std::vector<Entity> pieces = { line(1,0,0,5,0) };
      std::vector<Entity> others = { line(2,5,-5,5,5) };   // vertical boundary through (5,0)
      auto cs = computeCutConstraints(pieces, others, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::PointOnLine, "trim onto line -> PointOnLine");
      ck(cs.size()==1 && cs[0].entityIds[0]==1 && cs[0].entityIds[1]==2, "poc ties piece to boundary"); }

    // 3. Trim onto a circle boundary -> PointOnCircle.
    { nid = 1000;
      std::vector<Entity> pieces = { line(1,0,0,5,0) };
      std::vector<Entity> others = { createCircle(2, Point2D(5,5), 5.0) };  // passes through (5,0)
      auto cs = computeCutConstraints(pieces, others, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::PointOnCircle, "trim onto circle -> PointOnCircle"); }

    // 4. A junction that no piece endpoint sits at -> nothing.
    { nid = 1000;
      std::vector<Entity> pieces = { line(1,0,0,5,0) };
      auto cs = computeCutConstraints(pieces, {}, {{99,99}}, gen);
      ck(cs.empty(), "junction off all pieces -> no constraints"); }

    // 5. Plain point-split (no boundary through the split) -> join only, no poc.
    { nid = 1000;
      std::vector<Entity> pieces = { line(1,0,0,5,0), line(2,5,0,10,0) };
      std::vector<Entity> others = { line(3,0,20,10,20) };   // far away
      auto cs = computeCutConstraints(pieces, others, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::Coincident, "no boundary at split -> join only"); }

    // 5b. A lone 360-degree arc (a circle opened at one point) meets itself at
    // that point, but its two endpoints must NOT be welded coincident, or it
    // could never be pulled open. computeCutConstraints never joins a piece to
    // itself.
    { nid = 1000;
      Entity a = createArc(1, Point2D(0,0), 5.0, 0.0, 360.0);  // start==end at (5,0)
      auto cs = computeCutConstraints({a}, {}, {{5,0}}, gen);
      ck(cs.empty(), "opened circle: the arc's own endpoints are not welded"); }

    // 5c. Opened circle (one 360-degree arc) crossing a line at the split point:
    // exactly ONE of its two coincident ends is pinned to the line, never both.
    { nid = 1000;
      Entity a = createArc(1, Point2D(0,0), 5.0, 0.0, 360.0);   // ends at (5,0)
      std::vector<Entity> others = { line(2, 5,-5, 5,5) };       // vertical thru (5,0)
      auto cs = computeCutConstraints({a}, others, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::PointOnLine,
         "opened circle at a line -> exactly one PointOnLine (one end tied)"); }

    // 5d. Opened circle with a point entity sitting at the split -> one Coincident.
    { nid = 1000;
      Entity a = createArc(1, Point2D(0,0), 5.0, 0.0, 360.0);
      std::vector<Entity> others = { createPoint(2, Point2D(5,0)) };
      auto cs = computeCutConstraints({a}, others, {{5,0}}, gen);
      ck(cs.size()==1 && cs[0].type==ConstraintType::Coincident,
         "opened circle at a point entity -> one Coincident"); }

    // ---- remapCutConstraints: carry an original's constraints onto pieces ----
    std::printf("remapCutConstraints\n");
    // Original line 1 from (0,0) to (10,0), split at (5,0) into pieces 2 and 3.
    auto orig = line(1, 0,0, 10,0);
    std::vector<Entity> two = { line(2,0,0,5,0), line(3,5,0,10,0) };

    auto mkC = [](ConstraintType t, std::vector<int> eids, std::vector<int> pis) {
        Constraint c; c.type = t; c.entityIds = std::move(eids);
        c.pointIndices = std::move(pis); return c;
    };

    // 6. Point-anchored Coincident follows the piece that keeps the point.
    { nid = 2000;
      Constraint c = mkC(ConstraintType::Coincident, {1, 99}, {1, 0});  // orig end (10,0)
      auto r = remapCutConstraints({c}, orig, two, gen);
      ck(r.size()==1 && r[0].type==ConstraintType::Coincident, "coincident carries");
      ck(r.size()==1 && r[0].entityIds[0]==3 && r[0].pointIndices[0]==1,
         "coincident re-anchored to the piece owning (10,0)");
      ck(r.size()==1 && r[0].entityIds[1]==99 && r[0].pointIndices[1]==0,
         "the other reference is left untouched"); }

    // 7. A point cut away (only the left piece survives) -> the anchor drops.
    { nid = 2000;
      std::vector<Entity> leftOnly = { line(2,0,0,5,0) };
      Constraint c = mkC(ConstraintType::Coincident, {1, 99}, {1, 0});  // orig end (10,0), gone
      auto r = remapCutConstraints({c}, orig, leftOnly, gen);
      ck(r.empty(), "anchor whose point was trimmed away is dropped"); }

    // 8. Horizontal replicates onto every line piece.
    { nid = 2000;
      Constraint c = mkC(ConstraintType::Horizontal, {1}, {});
      auto r = remapCutConstraints({c}, orig, two, gen);
      ck(r.size()==2, "horizontal replicates onto both pieces");
      ck(r.size()==2 && r[0].type==ConstraintType::Horizontal
         && r[0].entityIds[0]==2 && r[1].entityIds[0]==3, "each piece gets its own horizontal"); }

    // 9. Parallel to another entity replicates, keeping the other reference.
    { nid = 2000;
      Constraint c = mkC(ConstraintType::Parallel, {1, 50}, {});
      auto r = remapCutConstraints({c}, orig, two, gen);
      ck(r.size()==2 && r[0].entityIds[0]==2 && r[0].entityIds[1]==50
         && r[1].entityIds[0]==3 && r[1].entityIds[1]==50, "parallel replicates, keeps entity 50"); }

    // 10. A dimension does not survive the cut -> dropped.
    { nid = 2000;
      Constraint c = mkC(ConstraintType::Distance, {1}, {}); c.value = 10.0;
      auto r = remapCutConstraints({c}, orig, two, gen);
      ck(r.empty(), "length dimension is dropped"); }

    // 11. A constraint that never named the original is ignored entirely.
    { nid = 2000;
      Constraint c = mkC(ConstraintType::Horizontal, {77}, {});
      auto r = remapCutConstraints({c}, orig, two, gen);
      ck(r.empty(), "unrelated constraint is not carried"); }

    std::printf(fails ? "cut_constraints: %d FAILURE(S)\n" : "cut_constraints: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
