// The rectangle acceptance matrix for group transforms, at library level:
// translate is rigid; rotate/scale/mirror are passive with Onshape-style
// repair; an outside point coincident with a corner follows; the solver
// never silently undoes a transform.
#include <hobbycad/sketch/transform.h>
#include <hobbycad/sketch/solver.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }
static bool nearP(const Point2D& a, const Point2D& b, double tol = 1e-6) { return near(a.x, b.x, tol) && near(a.y, b.y, tol); }

struct Rect {
    std::vector<Entity> es; std::vector<Constraint> cs; std::vector<int> members;
    int nextE = 1, nextC = 1;
    int line(Point2D a, Point2D b, int gid) { Entity e; e.id = nextE++; e.type = EntityType::Line; e.points = {a, b}; e.groupId = gid; es.push_back(e); return e.id; }
    int con(ConstraintType t, std::vector<int> eids, std::vector<int> pis = {}, double v = 0) {
        Constraint c; c.id = nextC++; c.type = t; c.entityIds = eids; c.pointIndices = pis; c.value = v; cs.push_back(c); return c.id; }
    const Entity& e(int id) const { for (auto& x : es) if (x.id == id) return x; static Entity none; return none; }
    const Constraint& c(int id) const { for (auto& x : cs) if (x.id == id) return x; static Constraint none; return none; }
};

// Rectangle 0,0 -> 100,50 decomposed the way decomposition.cpp does it
// (coincidents, perpendicular + parallels or H/V, FixedPoint at corner 0),
// plus a Distance 100 on the bottom edge and an outside point P coincident
// with the top-right corner.
static Rect makeRect(bool axisAlignedConstraints) {
    Rect r; const int gid = 1;
    int l0 = r.line({0, 0}, {100, 0}, gid), l1 = r.line({100, 0}, {100, 50}, gid),
        l2 = r.line({100, 50}, {0, 50}, gid), l3 = r.line({0, 50}, {0, 0}, gid);
    r.members = {l0, l1, l2, l3};
    int ls[4] = {l0, l1, l2, l3};
    for (int i = 0; i < 4; ++i) r.con(ConstraintType::Coincident, {ls[i], ls[(i + 1) % 4]}, {1, 0});
    if (axisAlignedConstraints) {
        r.con(ConstraintType::Horizontal, {l0}); r.con(ConstraintType::Vertical, {l1});
        r.con(ConstraintType::Horizontal, {l2}); r.con(ConstraintType::Vertical, {l3});
    } else {
        r.con(ConstraintType::Perpendicular, {l0, l1});
        r.con(ConstraintType::Parallel, {l0, l2}); r.con(ConstraintType::Parallel, {l1, l3});
    }
    r.con(ConstraintType::FixedPoint, {l0}, {0});
    r.con(ConstraintType::Distance, {l0, l0}, {0, 1}, 100.0);   // solver form: two entity refs + point indices
    Entity p; p.id = r.nextE++; p.type = EntityType::Point; p.points = {{100, 50}}; r.es.push_back(p);
    r.con(ConstraintType::Coincident, {p.id, l1}, {0, 1});   // P sits on the top-right corner
    return r;
}

static SolveResult solve(Rect& r) { Solver s; return s.solve(r.es, r.cs); }
static double width(const Rect& r) { const auto& l0 = r.e(1); return std::hypot(l0.points[1].x - l0.points[0].x, l0.points[1].y - l0.points[0].y); }
static double height(const Rect& r) { const auto& l1 = r.e(2); return std::hypot(l1.points[1].x - l1.points[0].x, l1.points[1].y - l1.points[0].y); }
static bool isRectangle(const Rect& r) {
    // corners coincide and adjacent edges are perpendicular
    for (int i = 0; i < 4; ++i) if (!nearP(r.e(1 + i).points[1], r.e(1 + (i + 1) % 4).points[0], 1e-4)) return false;
    auto d = [&](int id) { const auto& l = r.e(id); return Point2D{l.points[1].x - l.points[0].x, l.points[1].y - l.points[0].y}; };
    Point2D a = d(1), b = d(2); return near(a.x * b.x + a.y * b.y, 0.0, 1e-4);
}

int main() {
    std::printf("=== translate: rigid, P follows, undo-able geometry ===\n");
    {
        Rect r = makeRect(false); const int pId = 5;
        // The FixedPoint pins corner 0: a rigid move must carry the pin too,
        // which is why the drag path re-seeds it; here we move the pin's
        // point along with everything else and expect the solver to agree.
        GroupTransformParams p; p.kind = GroupTransformKind::Translate; p.delta = {10, 20};
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(res.applied && res.changedEntityIds.size() == 4, "four members moved, nothing refused");
        ck(res.notes.empty(), "no repair was needed for a translation");
        ck(nearP(r.e(1).points[0], {10, 20}), "corner 0 is at 10,20 before the solve");
        auto s = solve(r);
        ck(s.success, "the sketch still solves");
        ck(isRectangle(r) && near(width(r), 100) && near(height(r), 50), "still a 100x50 rectangle");
        ck(nearP(r.e(pId).points[0], r.e(2).points[1], 1e-4), "outside point P followed the corner");
    }
    std::printf("=== rotate 90 with H/V: they swap ===\n");
    {
        Rect r = makeRect(true);
        GroupTransformParams p; p.kind = GroupTransformKind::Rotate; p.angleDeg = 90; p.centerGiven = true; p.center = {0, 0};
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(res.applied, "applied");
        ck(r.c(5).type == ConstraintType::Vertical && r.c(6).type == ConstraintType::Horizontal, "Horizontal became Vertical and Vertical became Horizontal");
        ck(res.addedEntities.empty(), "no construction line for a right angle");
        auto s = solve(r);
        ck(s.success && isRectangle(r) && near(width(r), 100) && near(height(r), 50), "solves as the same rectangle, turned");
        ck(nearP(r.e(5).points[0], r.e(2).points[1], 1e-4), "P followed");
    }
    std::printf("=== rotate 37 with H/V: converted to a rotated reference ===\n");
    {
        Rect r = makeRect(true);
        const int dofBefore = solve(r).dof;
        GroupTransformParams p; p.kind = GroupTransformKind::Rotate; p.angleDeg = 37; p.centerGiven = true; p.center = {0, 0};
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(res.applied && res.addedEntities.size() == 1 && res.addedEntities[0].isConstruction, "one construction reference line added");
        ck(res.addedConstraints.size() == 2 && res.addedConstraints[0].type == ConstraintType::FixedPoint, "pinned by two FixedPoints");
        ck(solve(r).dof == dofBefore, "degrees of freedom unchanged by the rotation (Onshape's promise)");
        ck(r.c(5).type == ConstraintType::Parallel && r.c(6).type == ConstraintType::Perpendicular, "H -> Parallel to ref, V -> Perpendicular to ref");
        ck(r.c(5).entityIds.size() == 2 && r.c(5).entityIds[1] == res.addedEntities[0].id, "the converted constraint names the reference line");
        std::vector<int> withRef = r.members; withRef.push_back(res.addedEntities[0].id);
        auto s = solve(r);
        ck(s.success && isRectangle(r) && near(width(r), 100, 1e-3) && near(height(r), 50, 1e-3), "solves as the same rectangle at 37 degrees");
        const auto& l0 = r.e(1); double ang = std::atan2(l0.points[1].y - l0.points[0].y, l0.points[1].x - l0.points[0].x) * 180.0 / 3.14159265358979323846;
        ck(near(ang, 37, 1e-3), "and the bottom edge sits at 37 degrees, not snapped back");
    }
    std::printf("=== scale 2: dimension follows, never a silent snap back ===\n");
    {
        Rect r = makeRect(false);
        GroupTransformParams p; p.kind = GroupTransformKind::Scale; p.factor = 2; p.centerGiven = true; p.center = {0, 0};
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(res.applied, "applied");
        ck(near(r.c(9).value, 200.0), "the Distance value scaled from 100 to 200");
        auto s = solve(r);
        ck(s.success && isRectangle(r) && near(width(r), 200, 1e-3) && near(height(r), 100, 1e-3), "solves as 200x100");
        ck(nearP(r.e(5).points[0], r.e(2).points[1], 1e-4), "P followed");
    }
    std::printf("=== the dimension is live: geometry scaled without its value snaps back ===\n");
    {
        Rect r = makeRect(false);
        for (auto& e : r.es) if (e.groupId == 1) for (auto& pt : e.points) pt = pt * 2.0;   // no value change
        auto s = solve(r);
        ck(s.success && near(width(r), 100, 1e-3), "solver restored width 100: the Distance is enforced, so the scale test above could have failed");
    }
    std::printf("=== mirror across the horizontal: rectangle survives, H/V untouched ===\n");
    {
        Rect r = makeRect(true);
        GroupTransformParams p; p.kind = GroupTransformKind::Mirror; p.mirrorAcrossHorizontal = true;
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(res.applied, "applied");
        ck(r.c(5).type == ConstraintType::Horizontal && r.c(6).type == ConstraintType::Vertical, "H and V unchanged by an axis-aligned mirror");
        auto s = solve(r);
        ck(s.success && isRectangle(r), "solves as a rectangle");
    }
    std::printf("=== mirror with an outside symmetric axis is refused, untouched ===\n");
    {
        Rect r = makeRect(false);
        int axis = r.line({-50, -50}, {-50, 200}, -1);
        r.con(ConstraintType::Symmetric, {1, 3, axis}, {0, 0});
        std::vector<Point2D> before; for (auto& e : r.es) for (auto& pt : e.points) before.push_back(pt);
        GroupTransformParams p; p.kind = GroupTransformKind::Mirror; p.mirrorAcrossHorizontal = false;
        auto res = transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC);
        ck(!res.applied && !res.refusal.empty(), "refused with a reason");
        std::vector<Point2D> after; for (auto& e : r.es) for (auto& pt : e.points) after.push_back(pt);
        bool same = before.size() == after.size(); for (size_t i = 0; same && i < before.size(); ++i) same = nearP(before[i], after[i]);
        ck(same, "and nothing moved");
    }
    std::printf("=== default pivot is the geometric center, not the bounding box ===\n");
    {
        Rect r;   // an L: 100 long along x, 20 up along y
        int a = r.line({0, 0}, {100, 0}, -1), b = r.line({100, 0}, {100, 20}, -1);
        std::vector<int> set = {a, b};
        Point2D c = memberCenter(r.es, set);
        // length-weighted: (50,0)*100 + (100,10)*20 over 120 = (58.33, 1.67); bbox center would be (50,10)
        ck(near(c.x, 58.333333, 1e-4) && near(c.y, 1.666667, 1e-4), "L-shape center is the length-weighted centroid");
        Rect q = makeRect(false);
        Point2D qc = memberCenter(q.es, q.members);
        ck(nearP(qc, {50, 25}, 1e-9), "rectangle center is its geometric center");
    }
    std::printf("=== transformPoint: a stored pivot moves like a member point ===\n");
    {
        GroupTransformParams p; p.kind = GroupTransformKind::Rotate; p.angleDeg = 90; p.centerGiven = true; p.center = {10, 5};
        ck(nearP(transformPoint({10, 5}, p, {10, 5}), {10, 5}), "rotating about the pivot leaves the pivot in place");
        ck(nearP(transformPoint({20, 5}, p, {10, 5}), {10, 15}), "a point off the pivot turns about it");
        GroupTransformParams t; t.kind = GroupTransformKind::Translate; t.delta = {5, 0};
        ck(nearP(transformPoint({10, 5}, t, {}), {15, 5}), "translate carries the pivot");
        GroupTransformParams s; s.kind = GroupTransformKind::Scale; s.factor = 2; s.centerGiven = true; s.center = {0, 0};
        ck(nearP(transformPoint({10, 5}, s, {0, 0}), {20, 10}), "scale about the center moves an off-center pivot");
        GroupTransformParams m; m.kind = GroupTransformKind::Mirror; m.mirrorLineGiven = true; m.mirrorA = {0, 0}; m.mirrorB = {1, 1};
        ck(nearP(transformPoint({10, 5}, m, {}), {5, 10}), "mirror across the 45-degree line swaps x and y");
    }
    std::printf("=== cloneSet: a copy carries its internal constraints and nothing else ===\n");
    {
        Rect r = makeRect(false);
        auto cs = cloneSet(r.es, r.cs, r.members, r.nextE, r.nextC);
        ck(cs.entities.size() == 4, "four clones");
        ck(cs.entities[0].id >= r.nextE && cs.entities[0].groupId == -1, "fresh ids, no group membership");
        // inside: 4 coincident + perpendicular + 2 parallel + FixedPoint + Distance = 9; the outside coincident to P is not copied
        ck(cs.constraints.size() == 9, "exactly the nine internal constraints copied, the outside coincident not");
        bool remapped = true;
        for (const auto& c : cs.constraints) for (int eid : c.entityIds) if (eid < r.nextE) remapped = false;
        ck(remapped, "every cloned constraint names clone ids");
        ck(cs.constraints[0].pointIndices == r.cs[0].pointIndices, "point indices unchanged");
        // clone-then-transform: originals untouched, clones translated, everything solves
        std::vector<Entity> es = r.es; es.insert(es.end(), cs.entities.begin(), cs.entities.end());
        std::vector<Constraint> ks = r.cs; ks.insert(ks.end(), cs.constraints.begin(), cs.constraints.end());
        std::vector<int> cloneIds; for (auto& e : cs.entities) cloneIds.push_back(e.id);
        GroupTransformParams p; p.kind = GroupTransformKind::Translate; p.delta = {200, 0};
        auto res = transformEntities(es, ks, cloneIds, p, cs.nextEntityId, cs.nextConstraintId);
        ck(res.applied && nearP(es[0].points[0], {0, 0}) && nearP(es[cs.entityIdMap[0].second - 1].points[0], {200, 0}), "originals stay, clones moved");
        Solver sv; auto s = sv.solve(es, ks);
        ck(s.success, "originals plus transformed clones solve together");
    }
    std::printf("=== point to point is a translate; free move is a turn plus a drag ===\n");
    {
        Rect a = makeRect(false), b = makeRect(false);
        GroupTransformParams p2p; p2p.kind = GroupTransformKind::Translate; p2p.delta = Point2D{30, 40} - Point2D{0, 0};
        transformEntities(a.es, a.cs, a.members, p2p, a.nextE, a.nextC);
        ck(nearP(a.e(1).points[0], {30, 40}), "delta = to - from lands the from-point on the to-point");
        GroupTransformParams fm; fm.kind = GroupTransformKind::Rotate; fm.angleDeg = 90; fm.centerGiven = true; fm.center = {0, 0}; fm.delta = {10, 0};
        transformEntities(b.es, b.cs, b.members, fm, b.nextE, b.nextC);
        Rect c = makeRect(false);
        GroupTransformParams rot; rot.kind = GroupTransformKind::Rotate; rot.angleDeg = 90; rot.centerGiven = true; rot.center = {0, 0};
        transformEntities(c.es, c.cs, c.members, rot, c.nextE, c.nextC);
        GroupTransformParams tr; tr.kind = GroupTransformKind::Translate; tr.delta = {10, 0};
        transformEntities(c.es, c.cs, c.members, tr, c.nextE, c.nextC);
        bool same = true; for (int i = 1; i <= 4; ++i) for (int k = 0; k < 2; ++k) if (!nearP(b.e(i).points[k], c.e(i).points[k])) same = false;
        ck(same, "rotate with a post-delta equals rotate then translate");
    }
    std::printf("=== mirror across a picked line ===\n");
    {
        Rect r = makeRect(true);
        const int dof0 = solve(r).dof;
        GroupTransformParams m; m.kind = GroupTransformKind::Mirror; m.mirrorLineGiven = true; m.mirrorA = {0, 0}; m.mirrorB = {1, 1};
        auto res = transformEntities(r.es, r.cs, r.members, m, r.nextE, r.nextC);
        ck(res.applied && r.c(5).type == ConstraintType::Vertical && r.c(6).type == ConstraintType::Horizontal, "45-degree line: H and V swap, no reference line");
        ck(res.addedEntities.empty(), "no construction line for a 45-degree axis");
        auto s = solve(r);
        ck(s.success && isRectangle(r) && near(width(r), 100, 1e-3) && s.dof == dof0, "still a rectangle, dof unchanged");
        Rect q = makeRect(true);
        const int dof1 = solve(q).dof;
        GroupTransformParams m2; m2.kind = GroupTransformKind::Mirror; m2.mirrorLineGiven = true; m2.mirrorA = {0, 0}; m2.mirrorB = {std::cos(0.5236), std::sin(0.5236)};   // 30 degrees
        auto r2 = transformEntities(q.es, q.cs, q.members, m2, q.nextE, q.nextC);
        ck(r2.applied && r2.addedEntities.size() == 1 && q.c(5).type == ConstraintType::Parallel, "30-degree line: H becomes Parallel to a pinned reference");
        auto s2 = solve(q);
        ck(s2.success && isRectangle(q) && near(width(q), 100, 1e-3) && s2.dof == dof1, "still a rectangle at the mirrored angle, dof unchanged");
        Rect z = makeRect(false);
        GroupTransformParams bad; bad.kind = GroupTransformKind::Mirror; bad.mirrorLineGiven = true; bad.mirrorA = {3, 3}; bad.mirrorB = {3, 3};
        ck(!transformEntities(z.es, z.cs, z.members, bad, z.nextE, z.nextC).applied, "a zero-length mirror line is refused");
    }
    std::printf("=== a check that can fail: scale by zero is refused ===\n");
    {
        Rect r = makeRect(false);
        GroupTransformParams p; p.kind = GroupTransformKind::Scale; p.factor = 0;
        ck(!transformEntities(r.es, r.cs, r.members, p, r.nextE, r.nextC).applied, "factor 0 refused");
    }
    std::printf("\n%s\n", failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}
