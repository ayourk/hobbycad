// =====================================================================
//  src/libhobbycad/sketch/solver.cpp — Constraint solver implementation
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/sketch/solver.h>
#include <hobbycad/units.h>

#ifdef HAVE_SLVS
#include <slvs.h>
#endif

#include <hobbycad/crashhandler.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdarg>

namespace {
// Solver diagnostics: useful, but a re-solve on every mouse move would repeat the
// same line dozens of times. De-duplicate: each distinct message prints ONCE
// (per process) so the signal stays without the flood. A new/different problem
// still shows. (Aaron: "the noise is a good diagnostic, but it needs to be
// meaningful.")
void solverDiag(const char* fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    static std::set<std::string> seen;
    std::string line(buf);
    if (seen.insert(line).second) std::fputs(line.c_str(), stderr);
}
}  // namespace
#include <utility>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Solver Implementation Class
// =====================================================================

class Solver::Impl {
public:
#ifdef HAVE_SLVS
    // Map HobbyCAD entity IDs to solver handles
    std::map<int, Slvs_hEntity> entityHandles;
    /// Keys in entityHandles that name a genuine solver POINT (everything
    /// registered through addPoint2d): standalone Point entities plus the
    /// compound endpoint/center/control-point keys. getPointHandle() must
    /// never hand a non-point handle (a line, circle, arc, distance...) to a
    /// constraint that expects a point; libslvs asserts "Unexpected entity
    /// type" in PointGetExprs and aborts the whole solve.
    std::set<int> pointKeys;
    std::map<int, Slvs_hParam> paramHandles;
    std::map<int, Slvs_hConstraint> constraintHandles;
    std::vector<std::pair<int, int>> draggedPoints;   ///< (entityId, pointIndex) for the next solve
    std::vector<std::pair<int, int>> weightedPoints;  ///< [0009] points held with a stiffness next solve
    double weightStiffness = 1.0;                     ///< [0009] resistance applied to weightedPoints

    static constexpr Slvs_hGroup workplaneGroupId = 1;  // Group for workplane definition
    static constexpr Slvs_hGroup sketchGroupId = 2;    // Group for sketch entities/constraints
    Slvs_hEntity workplaneHandle = 0;
    int nextParamHandle = 1;
    int nextEntityHandle = 1;
    int nextConstraintHandle = 1;

    void reset() {
        entityHandles.clear();
        pointKeys.clear();
        paramHandles.clear();
        constraintHandles.clear();
        nextParamHandle = 1;
        nextEntityHandle = 1;
        nextConstraintHandle = 1;
    }

    Slvs_hParam addParam(std::vector<Slvs_Param>& params, double value,
                         Slvs_hGroup group = 0) {
        Slvs_hParam h = nextParamHandle++;
        params.push_back(Slvs_MakeParam(h, group ? group : sketchGroupId, value));
        return h;
    }

    Slvs_hEntity addPoint2d(std::vector<Slvs_Param>& params, std::vector<Slvs_Entity>& entities,
                            const Point2D& pt, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;
        pointKeys.insert(entityId);   // this key names a real POINT

        Slvs_hParam u = addParam(params, pt.x, sketchGroupId);
        Slvs_hParam v = addParam(params, pt.y, sketchGroupId);

        paramHandles[entityId * 10 + 0] = u;
        paramHandles[entityId * 10 + 1] = v;

        entities.push_back(Slvs_MakePoint2d(h, sketchGroupId, workplaneHandle, u, v));
        return h;
    }

    Slvs_hEntity addLineSegment(std::vector<Slvs_Entity>& entities,
                                Slvs_hEntity p1, Slvs_hEntity p2, int entityId) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;
        entities.push_back(Slvs_MakeLineSegment(h, sketchGroupId, workplaneHandle, p1, p2));
        return h;
    }

    Slvs_hEntity addCircle(std::vector<Slvs_Param>& params, std::vector<Slvs_Entity>& entities,
                           const Point2D& center, double radius, int entityId,
                           Slvs_hEntity normalHandle) {
        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[entityId] = h;

        // Create center point
        Slvs_hEntity centerHandle = addPoint2d(params, entities, center, entityId * 1000);

        // Create radius as a distance entity (SolveSpace requires SLVS_E_DISTANCE,
        // not a raw parameter, for the circle's distance sub-entity)
        Slvs_hParam radiusParam = addParam(params, radius, sketchGroupId);
        Slvs_hEntity distHandle = nextEntityHandle++;
        entities.push_back(Slvs_MakeDistance(distHandle, sketchGroupId, workplaneHandle, radiusParam));

        entities.push_back(Slvs_MakeCircle(h, sketchGroupId, workplaneHandle, centerHandle, normalHandle, distHandle));
        return h;
    }

    /// Arcs whose endpoints were swapped when handed to the solver.
    /// SolveSpace arcs always run counter-clockwise from start to end, so a
    /// clockwise HobbyCAD arc (negative sweepAngle) is registered with its
    /// endpoints exchanged and swapped back on readback.
    std::set<int> arcSwapped;

    /// Register an arc as a real SLVS_E_ARC_OF_CIRCLE.
    ///
    /// Points are keyed by their HobbyCAD index (center = id*1000+0,
    /// start = +1, end = +2) so constraints can address an arc's endpoints
    /// through getPointHandle().  Registering arcs as plain circles - which
    /// this used to do - left them with no endpoints at all.
    Slvs_hEntity addArc(std::vector<Slvs_Param>& params,
                        std::vector<Slvs_Entity>& entities,
                        const Entity& e, Slvs_hEntity normalHandle) {
        Slvs_hEntity center = addPoint2d(params, entities, e.points[0], e.id * 1000 + 0);
        Slvs_hEntity pStart = addPoint2d(params, entities, e.points[1], e.id * 1000 + 1);
        Slvs_hEntity pEnd   = addPoint2d(params, entities, e.points[2], e.id * 1000 + 2);

        const bool clockwise = (e.sweepAngle < 0.0);
        if (clockwise) arcSwapped.insert(e.id);

        Slvs_hEntity h = nextEntityHandle++;
        entityHandles[e.id] = h;
        entities.push_back(Slvs_MakeArcOfCircle(
            h, sketchGroupId, workplaneHandle, normalHandle, center,
            clockwise ? pEnd : pStart,
            clockwise ? pStart : pEnd));
        return h;
    }

    /// Read one solved 2D point back out of the system by its HobbyCAD point
    /// key. Returns false when the key was never registered, leaving `out`
    /// untouched so the caller keeps its pre-solve value.
    ///
    /// Hoisted out of the Arc readback: five entity types need exactly this
    /// loop, and copying it per type is how the two sides drift apart.
    bool readSolvedPoint(const Slvs_System& sys, int key, Point2D& out) {
        auto it = entityHandles.find(key);
        if (it == entityHandles.end()) return false;
        const Slvs_hEntity ph = it->second;
        for (int i = 0; i < sys.entities; ++i) {
            if (sys.entity[i].h != ph) continue;
            const Slvs_hParam u = sys.entity[i].param[0];
            const Slvs_hParam v = sys.entity[i].param[1];
            for (int k = 0; k < sys.params; ++k) {
                if (sys.param[k].h == u) out.x = sys.param[k].val;
                if (sys.param[k].h == v) out.y = sys.param[k].val;
            }
            return true;
        }
        return false;
    }

    // 3D-point overload: read the solved (u,v) into a Point3, preserving its
    // off-plane z (the 2D path solves on the plane, so z is untouched).
    bool readSolvedPoint(const Slvs_System& sys, int key, Point3& out) {
        Point2D tmp = out.xy();
        if (!readSolvedPoint(sys, key, tmp)) return false;
        out.x = tmp.x;
        out.y = tmp.y;
        return true;
    }

    Slvs_hEntity getPointHandle(int entityId, int pointIndex) {
        // First try compound key (line/arc/circle endpoint or center).
        // Lines register endpoints as entityId*1000+0 and entityId*1000+1.
        // Circles/arcs register centers as entityId*1000.  Only accept it when
        // that key actually names a POINT: some compound keys (e.g. a polygon's
        // decomposed edge lines at id*1000+1000+k) are NOT points.
        const int pointId = entityId * 1000 + pointIndex;
        if (pointKeys.count(pointId) > 0) {
            return entityHandles[pointId];
        }

        // Fall back to the direct handle ONLY for a standalone Point entity.
        // Returning a Line/Circle/Arc handle here (an out-of-range index, or a
        // point index on an entity that does not register per-point handles)
        // hands a non-point to a point-expecting constraint, which makes
        // libslvs abort in PointGetExprs.  Return 0 so the caller skips the
        // constraint gracefully instead.
        if (pointKeys.count(entityId) > 0) {
            return entityHandles[entityId];
        }

        return 0;
    }

    void buildSolverSystem(
        Slvs_System& sys,
        std::vector<Slvs_Param>& params,
        std::vector<Slvs_Entity>& slvsEntities,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities,
        const std::vector<Constraint>& constraints);

    void extractSolution(
        const Slvs_System& sys,
        std::vector<Entity>& entities);

    void addConstraintToSolver(
        const Constraint& constraint,
        std::vector<Slvs_Param>& params,
        std::vector<Slvs_Entity>& slvsEntities,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities);
    // Per-type helpers split out of addConstraintToSolver (verbatim bodies).
    void addTangentConstraint(
        const Constraint& constraint, Slvs_hConstraint ch,
        std::vector<Slvs_Param>& params,
        std::vector<Slvs_Entity>& slvsEntities,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities);
    void addMidpointConstraint(
        const Constraint& constraint, Slvs_hConstraint ch,
        std::vector<Slvs_Constraint>& slvsConstraints,
        const std::vector<Entity>& entities);
#endif
};

// =====================================================================
//  Solver Implementation
// =====================================================================

Solver::Solver()
    : m_impl(new Impl())
{
}

Solver::~Solver()
{
    delete m_impl;
}

void Solver::setDraggedPoints(const std::vector<std::pair<int, int>>& points)
{
    m_impl->draggedPoints = points;
}

void Solver::setPointWeights(const std::vector<std::pair<int, int>>& points, double stiffness)
{
    m_impl->weightedPoints = points;
    m_impl->weightStiffness = stiffness;
}

bool Solver::isAvailable()
{
#ifdef HAVE_SLVS
    return true;
#else
    return false;
#endif
}

SolveResult Solver::solve(
    std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    SolveResult result;
    result.success = false;
    result.errorMessage = "Solver not available (libslvs not compiled)";
    return result;
#else
    m_impl->reset();

    // Prepare solver data structures
    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

    // Build solver system
    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, entities, constraints);

    // Set up Slvs_System pointers
    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    // Allocate space for failed constraints
    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Dragged points: hand libslvs the parameters the user is holding. This
    // libslvs takes a pointer plus a count (sys.dragged / sys.ndragged), so
    // the buffer must outlive Slvs_Solve(); it lives in this frame.
    std::vector<Slvs_hParam> draggedParams;
    for (const auto& [eid, pidx] : m_impl->draggedPoints) {
        // Line/arc/circle points are keyed entityId*1000+index; a bare Point
        // entity is keyed by its own id (see the addPoint2d callers).
        for (int key : {eid * 1000 + pidx, eid}) {
            auto u = m_impl->paramHandles.find(key * 10 + 0);
            auto v = m_impl->paramHandles.find(key * 10 + 1);
            if (u == m_impl->paramHandles.end() || v == m_impl->paramHandles.end()) continue;
            draggedParams.push_back(u->second);
            draggedParams.push_back(v->second);
            break;
        }
    }
    m_impl->draggedPoints.clear();
    sys.dragged = draggedParams.empty() ? nullptr : draggedParams.data();
    sys.ndragged = static_cast<int>(draggedParams.size());

#if defined(SLVS_HAS_DRAG_WEIGHTS)
    // [HobbyCAD 0009] per-point drag stiffness: hold chosen points harder than
    // the grabbed element so a body-drag deforms rather than shoves. Buffers
    // must outlive Slvs_Solve(), so they live in this frame like draggedParams.
    std::vector<Slvs_hParam> weightParamBuf;
    std::vector<double> weightValBuf;
    for (const auto& [eid, pidx] : m_impl->weightedPoints) {
        for (int key : {eid * 1000 + pidx, eid}) {
            auto u = m_impl->paramHandles.find(key * 10 + 0);
            auto v = m_impl->paramHandles.find(key * 10 + 1);
            if (u == m_impl->paramHandles.end() || v == m_impl->paramHandles.end()) continue;
            weightParamBuf.push_back(u->second); weightValBuf.push_back(m_impl->weightStiffness);
            weightParamBuf.push_back(v->second); weightValBuf.push_back(m_impl->weightStiffness);
            break;
        }
    }
    sys.weightParam = weightParamBuf.empty() ? nullptr : weightParamBuf.data();
    sys.weightVal   = weightValBuf.empty()   ? nullptr : weightValBuf.data();
    sys.nweight     = static_cast<int>(weightParamBuf.size());
#endif
    m_impl->weightedPoints.clear();

    // [HobbyCAD] Ask libslvs which parameters remain free (under-constrained),
    // when the linked library supports it. Opt-in: a NULL freeParams keeps the
    // old behavior, so this is a no-op against a stock libslvs.
#if defined(SLVS_HAS_FREE_PARAMS)
    std::vector<Slvs_hParam> freeBuf(std::max(1, static_cast<int>(params.size())));
    sys.freeParams = freeBuf.data();
    sys.nfreeParams = 0;
#endif

    // Solve
    Slvs_Solve(&sys, m_impl->sketchGroupId);

    SolveResult result;
    result.dof = sys.dof;
    result.resultCode = static_cast<SolveResult::ResultCode>(sys.result);

    if (sys.result == SLVS_RESULT_OKAY || sys.result == SLVS_RESULT_REDUNDANT_OKAY) {
        m_impl->extractSolution(sys, entities);
        result.success = true;

#if defined(SLVS_HAS_FREE_PARAMS)
        // Map free parameter handles back to (entityId, pointIndex). paramHandles
        // is keyed pointKey*10 + {0,1}; pointKey is a bare entity id for a Point,
        // or realId*1000 + pointIndex for a line/arc/circle sub-point. A point is
        // free if either coordinate param is free; dedup accordingly.
        result.freePointsValid = true;
        if (sys.nfreeParams > 0) {
            std::map<Slvs_hParam, int> handleToKey;
            for (const auto& kv : m_impl->paramHandles) handleToKey[kv.second] = kv.first;
            std::set<std::pair<int, int>> seen;
            for (int i = 0; i < sys.nfreeParams; ++i) {
                auto it = handleToKey.find(sys.freeParams[i]);
                if (it == handleToKey.end()) continue;   // e.g. a free radius param
                int pointKey = it->second / 10;
                int realId = pointKey >= 1000 ? pointKey / 1000 : pointKey;
                int pointIndex = pointKey >= 1000 ? pointKey % 1000 : 0;
                if (seen.insert({realId, pointIndex}).second)
                    result.freePoints.emplace_back(realId, pointIndex);
            }
        }
#endif

        // NOTE: libslvs's `failed` list is NOT read on a redundant solve, and
        // must not be. Measured: with ONE duplicate constraint it names FOUR
        // constraints (the whole interdependent set, not the culprit), and
        // with THREE duplicates it names NONE. It does not identify which
        // constraint is superfluous, so surfacing it would point the user at
        // innocent constraints. Redundancy is reported as a state only.

        // Classify. Note an empty system is reported by libslvs as a clean
        // solve with dof 0, which is right: no geometry means no freedoms.
        if (entities.empty()) {
            result.state = SketchState::Empty;
            result.dof = 0;
        } else if (sys.result == SLVS_RESULT_REDUNDANT_OKAY) {
            // Redundant but satisfiable. Still a usable sketch, so this is
            // NOT lumped in with the failures.
            result.state = SketchState::OverConstrained;
        } else {
            result.state = (result.dof == 0) ? SketchState::FullyConstrained
                                             : SketchState::UnderConstrained;
        }
    } else {
        result.success = false;
        // On any failure the dof libslvs hands back is meaningless: a
        // contradictory pair of distances reports a cheerful positive number.
        // Say so with the state rather than smuggling a sentinel into `dof`.
        result.state = (sys.result == SLVS_RESULT_INCONSISTENT)
                           ? SketchState::Inconsistent
                           : SketchState::Failed;

        // Map failed constraint handles back to HobbyCAD IDs
        for (int i = 0; i < sys.faileds; ++i) {
            for (auto it = m_impl->constraintHandles.begin(); it != m_impl->constraintHandles.end(); ++it) {
                if (it->second == sys.failed[i]) {
                    result.failedConstraintIds.push_back(it->first);
                }
            }
        }

#ifdef SLVS_RESULT_INTERNAL_ERROR
        if (sys.result == SLVS_RESULT_INTERNAL_ERROR) {
            // The solver hit a fault and was recovered rather than being
            // allowed to end the process.  Its internal state was torn
            // down, so the next solve starts clean; the caller's entities
            // were never modified.
            result.errorMessage =
                "The constraint solver failed internally and was recovered. "
                "The sketch is unchanged; see the crash log for details.";
            return result;
        }
#endif
        result.errorMessage = solveResultName(result.resultCode);
    }

    return result;
#endif
}

SolveResult Solver::solve3D(
    std::vector<Sketch3DPoint>& points,
    const std::vector<Sketch3DConstraint>& constraints,
    const PlaneBasis& plane)
{
#ifndef HAVE_SLVS
    SolveResult result;
    result.success = false;
    result.errorMessage = "Solver not available (libslvs not compiled)";
    result.state = SketchState::Unknown;
    return result;
#else
    m_impl->reset();

    std::vector<Slvs_Param>      params;
    std::vector<Slvs_Entity>     ents;
    std::vector<Slvs_Constraint> cons;
    Slvs_hParam      ph = 1;
    Slvs_hEntity     eh = 1;
    Slvs_hConstraint ch = 1;
    const Slvs_hGroup G_FIXED = 1;   // plane + fixed references (not solved)
    const Slvs_hGroup G_SOLVE = 2;   // the points we solve for

    std::map<int, std::array<Slvs_hParam, 3>> pparams;  // point id -> x,y,z handles
    std::map<int, Slvs_hEntity>               pent;     // point id -> point entity

    // A workplane from the caller's basis, in G_FIXED, for the on-plane pins.
    Slvs_hParam ox = ph++, oy = ph++, oz = ph++;
    params.push_back(Slvs_MakeParam(ox, G_FIXED, plane.origin.x));
    params.push_back(Slvs_MakeParam(oy, G_FIXED, plane.origin.y));
    params.push_back(Slvs_MakeParam(oz, G_FIXED, plane.origin.z));
    Slvs_hEntity originH = eh++;
    ents.push_back(Slvs_MakePoint3d(originH, G_FIXED, ox, oy, oz));
    double qw, qx, qy, qz;
    Slvs_MakeQuaternion(plane.uAxis.x, plane.uAxis.y, plane.uAxis.z,
                        plane.vAxis.x, plane.vAxis.y, plane.vAxis.z,
                        &qw, &qx, &qy, &qz);
    Slvs_hParam nw = ph++, nx = ph++, ny = ph++, nz = ph++;
    params.push_back(Slvs_MakeParam(nw, G_FIXED, qw));
    params.push_back(Slvs_MakeParam(nx, G_FIXED, qx));
    params.push_back(Slvs_MakeParam(ny, G_FIXED, qy));
    params.push_back(Slvs_MakeParam(nz, G_FIXED, qz));
    Slvs_hEntity normalH = eh++;
    ents.push_back(Slvs_MakeNormal3d(normalH, G_FIXED, nw, nx, ny, nz));
    Slvs_hEntity planeH = eh++;
    ents.push_back(Slvs_MakeWorkplane(planeH, G_FIXED, originH, normalH));

    // Points: fixed ones live in G_FIXED (constants); the rest in G_SOLVE.
    for (const auto& p : points) {
        const Slvs_hGroup g = p.fixed ? G_FIXED : G_SOLVE;
        Slvs_hParam xh = ph++, yh = ph++, zh = ph++;
        params.push_back(Slvs_MakeParam(xh, g, p.pos.x));
        params.push_back(Slvs_MakeParam(yh, g, p.pos.y));
        params.push_back(Slvs_MakeParam(zh, g, p.pos.z));
        Slvs_hEntity peh = eh++;
        ents.push_back(Slvs_MakePoint3d(peh, g, xh, yh, zh));
        pparams[p.id] = { xh, yh, zh };
        pent[p.id]    = peh;
        if (p.onPlane && !p.fixed) {
            cons.push_back(Slvs_MakeConstraint(ch++, G_SOLVE, SLVS_C_PT_IN_PLANE,
                                               SLVS_FREE_IN_3D, 0.0, peh, 0, planeH, 0));
        }
    }

    // Constraints (in 3D; expand this vocabulary as the sketcher grows).
    for (const auto& c : constraints) {
        auto ia = pent.find(c.a), ib = pent.find(c.b);
        if (ia == pent.end() || ib == pent.end()) continue;
        if (c.kind == Sketch3DConstraint::Distance) {
            cons.push_back(Slvs_MakeConstraint(ch++, G_SOLVE, SLVS_C_PT_PT_DISTANCE,
                                               SLVS_FREE_IN_3D, c.value,
                                               ia->second, ib->second, 0, 0));
        } else {
            cons.push_back(Slvs_MakeConstraint(ch++, G_SOLVE, SLVS_C_POINTS_COINCIDENT,
                                               SLVS_FREE_IN_3D, 0.0,
                                               ia->second, ib->second, 0, 0));
        }
    }

    Slvs_System sys = {};
    sys.param = params.data();      sys.params = static_cast<int>(params.size());
    sys.entity = ents.data();       sys.entities = static_cast<int>(ents.size());
    sys.constraint = cons.data();   sys.constraints = static_cast<int>(cons.size());
    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(cons.size())));
    sys.failed = failed.data();     sys.faileds = static_cast<int>(failed.size());
    sys.calculateFaileds = 1;
#if defined(SLVS_HAS_FREE_PARAMS)
    std::vector<Slvs_hParam> freeBuf(std::max(1, static_cast<int>(params.size())));
    sys.freeParams = freeBuf.data();
    sys.nfreeParams = 0;
#endif

    Slvs_Solve(&sys, G_SOLVE);

    SolveResult result;
    result.dof = sys.dof;
    result.resultCode = static_cast<SolveResult::ResultCode>(sys.result);

    if (sys.result == SLVS_RESULT_OKAY || sys.result == SLVS_RESULT_REDUNDANT_OKAY) {
        result.success = true;
        // Write solved coordinates back into the caller's points.
        std::map<Slvs_hParam, double> valOf;
        for (int i = 0; i < sys.params; ++i) valOf[sys.param[i].h] = sys.param[i].val;
        for (auto& p : points) {
            auto it = pparams.find(p.id);
            if (it == pparams.end()) continue;
            p.pos.x = valOf[it->second[0]];
            p.pos.y = valOf[it->second[1]];
            p.pos.z = valOf[it->second[2]];
        }
#if defined(SLVS_HAS_FREE_PARAMS)
        result.freePointsValid = true;
        if (sys.nfreeParams > 0) {
            std::map<Slvs_hParam, int> handleToPoint;
            for (const auto& kv : pparams)
                for (int k = 0; k < 3; ++k) handleToPoint[kv.second[k]] = kv.first;
            std::set<int> seen;
            for (int i = 0; i < sys.nfreeParams; ++i) {
                auto it = handleToPoint.find(sys.freeParams[i]);
                if (it == handleToPoint.end()) continue;
                if (seen.insert(it->second).second)
                    result.freePoints.emplace_back(it->second, 0);
            }
        }
#endif
        if (points.empty()) {
            result.state = SketchState::Empty;
            result.dof = 0;
        } else if (sys.result == SLVS_RESULT_REDUNDANT_OKAY) {
            result.state = SketchState::OverConstrained;
        } else {
            result.state = (result.dof == 0) ? SketchState::FullyConstrained
                                             : SketchState::UnderConstrained;
        }
    } else {
        result.success = false;
        result.state = (sys.result == SLVS_RESULT_INCONSISTENT)
                           ? SketchState::Inconsistent
                           : SketchState::Failed;
        result.errorMessage = solveResultName(result.resultCode);
    }
    return result;
#endif
}

bool Solver::wouldOverConstrain(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info = checkOverConstrain(entities, existingConstraints, newConstraint);
    return info.wouldOverConstrain;
}

OverConstraintInfo Solver::checkOverConstrain(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& existingConstraints,
    const Constraint& newConstraint)
{
    OverConstraintInfo info;

#ifndef HAVE_SLVS
    info.wouldOverConstrain = false;
    return info;
#else
    // Create a temporary list with the new constraint added
    std::vector<Constraint> testConstraints = existingConstraints;
    testConstraints.push_back(newConstraint);

    // Make a copy of entities (solve modifies them)
    std::vector<Entity> testEntities = entities;

    m_impl->reset();

    // Prepare solver data structures
    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

    // Build solver system
    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, testEntities, testConstraints);

    // Set up Slvs_System pointers
    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    // Allocate space for failed constraints
    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();
    sys.calculateFaileds = 1;

    // Solve
    Slvs_Solve(&sys, m_impl->sketchGroupId);

    // Two DIFFERENT ways a constraint can be one too many, and both count:
    //
    //   * CONTRADICTORY: inconsistent, or the solver cannot get there
    //     (did not converge, too many unknowns). No solution exists.
    //   * REDUNDANT: SLVS_RESULT_REDUNDANT_OKAY. A solution still exists,
    //     but the constraint says nothing new: a second Horizontal on a line
    //     that is already horizontal. Left unflagged, these accumulate
    //     silently and every later conflict becomes harder to read.
    //
    // Redundancy used to be missed here, so the driven-dimension offer never
    // appeared for it. Fusion 360 rejects that case too.
    const bool contradictory = (sys.result == SLVS_RESULT_INCONSISTENT ||
                                sys.result == SLVS_RESULT_DIDNT_CONVERGE ||
                                sys.result == SLVS_RESULT_TOO_MANY_UNKNOWNS);
    const bool redundant = (sys.result == SLVS_RESULT_REDUNDANT_OKAY);

    info.wouldOverConstrain = contradictory || redundant;
    info.isRedundant = redundant;

    if (contradictory) {
        // Map failed constraint handles back to IDs (excluding the new one).
        // Only meaningful for the contradictory case; see the note in
        // solve() on why the redundant list must not be trusted.
        for (int i = 0; i < sys.faileds; ++i) {
            Slvs_hConstraint failedHandle = sys.failed[i];
            for (auto it = m_impl->constraintHandles.begin(); it != m_impl->constraintHandles.end(); ++it) {
                if (it->second == failedHandle && it->first != newConstraint.id) {
                    info.conflictingConstraintIds.push_back(it->first);
                }
            }
        }
    }

    if (info.wouldOverConstrain) {
        info.reason = solveResultName(static_cast<SolveResult::ResultCode>(sys.result));
    }

    return info;
#endif
}

std::vector<int> Solver::findRedundantConstraints(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    std::vector<int> candidates;
#ifndef HAVE_SLVS
    (void)entities; (void)constraints;
    return candidates;
#else
    // One solve to establish there is anything to look for. This is the
    // common case and it keeps the cheap path cheap. Its DOF is also the
    // baseline the test below compares against.
    int baselineDof = 0;
    {
        std::vector<Entity> probe = entities;
        const SolveResult first = solve(probe, constraints);
        if (first.state != SketchState::OverConstrained) {
            return candidates;
        }
        baselineDof = first.dof;
    }

    // Now the expensive part: drop each constraint in turn and see whether
    // the redundancy goes away. Driven constraints are skipped because they
    // never entered the system in the first place, so removing one could not
    // possibly change the result, and offering the user a reference
    // dimension as the thing to delete would be actively misleading.
    for (const Constraint& dropped : constraints) {
        if (!dropped.enabled || !dropped.isDriving) {
            continue;
        }

        std::vector<Constraint> subset;
        subset.reserve(constraints.size());
        for (const Constraint& c : constraints) {
            if (c.id != dropped.id) {
                subset.push_back(c);
            }
        }

        // solve() rewrites the entities it is given, so probe on a copy;
        // this function must not move the user's geometry.
        std::vector<Entity> probe = entities;
        const SolveResult r = solve(probe, subset);

        // TWO tests, and the second is what makes this useful.
        //
        // (1) The redundancy must be GONE without this constraint. Still
        //     redundant means this one was not what caused it.
        //
        // (2) The degrees of freedom must be UNCHANGED. This is the test
        //     Dune 3D's version omits, and without it the result is mostly
        //     noise: removing any load-bearing constraint also clears the
        //     redundancy, so a sketch with one duplicate Horizontal reported
        //     four candidates including the Fixed and Coincident constraints
        //     holding it in place. A genuinely redundant constraint adds
        //     nothing to the rank, so dropping it CANNOT free up a degree of
        //     freedom; a load-bearing one always does. Measured on that same
        //     sketch, this narrows four candidates to exactly the two
        //     Horizontals, which is the honest answer, since either may go.
        const bool redundancyCleared =
            (r.state == SketchState::FullyConstrained
             || r.state == SketchState::UnderConstrained
             || r.state == SketchState::Empty);
        const bool wasLoadBearing = (r.dof != baselineDof);

        if (redundancyCleared && !wasLoadBearing) {
            candidates.push_back(dropped.id);
        }
    }
    return candidates;
#endif
}

int Solver::degreesOfFreedom(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
#ifndef HAVE_SLVS
    return -1;  // Unknown
#else
    // Make a copy of entities (solve may modify them)
    std::vector<Entity> testEntities = entities;

    m_impl->reset();

    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> slvsEntities;
    std::vector<Slvs_Constraint> slvsConstraints;

    Slvs_System sys = {};
    m_impl->buildSolverSystem(sys, params, slvsEntities, slvsConstraints, testEntities, constraints);

    sys.param = params.data();
    sys.params = params.size();
    sys.entity = slvsEntities.data();
    sys.entities = slvsEntities.size();
    sys.constraint = slvsConstraints.data();
    sys.constraints = slvsConstraints.size();

    std::vector<Slvs_hConstraint> failed(std::max(1, static_cast<int>(slvsConstraints.size())));
    sys.failed = failed.data();
    sys.faileds = failed.size();

    Slvs_Solve(&sys, m_impl->sketchGroupId);

    return sys.dof;
#endif
}

// =====================================================================
//  libslvs System Building (only when HAVE_SLVS is defined)
// =====================================================================

#ifdef HAVE_SLVS

void Solver::Impl::buildSolverSystem(
    Slvs_System& sys,
    std::vector<Slvs_Param>& params,
    std::vector<Slvs_Entity>& slvsEntities,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints)
{
    // Create 2D workplane (fixed XY plane) in group 1.
    // Solvespace requires the workplane to be in a lower-numbered group
    // than the sketch entities/constraints (group 2) so it is treated
    // as already-solved infrastructure.
    Slvs_hEntity originHandle = nextEntityHandle++;
    Slvs_hEntity normalHandle = nextEntityHandle++;
    workplaneHandle = nextEntityHandle++;

    // Origin point (0, 0, 0)
    Slvs_hParam originX = addParam(params, 0.0, workplaneGroupId);
    Slvs_hParam originY = addParam(params, 0.0, workplaneGroupId);
    Slvs_hParam originZ = addParam(params, 0.0, workplaneGroupId);

    slvsEntities.push_back(Slvs_MakePoint3d(originHandle, workplaneGroupId, originX, originY, originZ));

    // Normal quaternion for XY plane (basis vectors (1,0,0) and (0,1,0))
    double qw, qx, qy, qz;
    Slvs_MakeQuaternion(1, 0, 0,   // unit X
                        0, 1, 0,   // unit Y
                        &qw, &qx, &qy, &qz);

    Slvs_hParam normalW  = addParam(params, qw, workplaneGroupId);
    Slvs_hParam normalXP = addParam(params, qx, workplaneGroupId);
    Slvs_hParam normalYP = addParam(params, qy, workplaneGroupId);
    Slvs_hParam normalZP = addParam(params, qz, workplaneGroupId);

    slvsEntities.push_back(Slvs_MakeNormal3d(normalHandle, workplaneGroupId, normalW, normalXP, normalYP, normalZP));

    // Workplane
    slvsEntities.push_back(Slvs_MakeWorkplane(workplaneHandle, workplaneGroupId, originHandle, normalHandle));

    // A fixed 2D origin point at (0,0) in the workplane, registered under the
    // sketch-origin sentinel so a constraint can target it (grounding a sketch
    // to the origin). It lives in the fixed group, so it never moves.
    {
        Slvs_hParam ou = addParam(params, 0.0, workplaneGroupId);
        Slvs_hParam ov = addParam(params, 0.0, workplaneGroupId);
        Slvs_hEntity origin2d = nextEntityHandle++;
        slvsEntities.push_back(Slvs_MakePoint2d(origin2d, workplaneGroupId, workplaneHandle, ou, ov));
        entityHandles[kSketchOriginEntity] = origin2d;
        pointKeys.insert(kSketchOriginEntity);   // it is a genuine point (grounding targets it)
    }

    // Create solver entities from library entities
    for (const Entity& entity : entities) {
        // Projected entities are reference geometry: fully determined by their
        // source in another sketch, re-derived after the solve. They are never
        // registered with the solver, so they add ZERO degrees of freedom and
        // cannot be moved by it; they are fixed from the source.
        if (entity.projectionSourceId >= 0) continue;
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.empty()) {
                addPoint2d(params, slvsEntities, entity.points[0], entity.id);
            }
            break;

        case EntityType::Line:
            if (entity.points.size() >= 2) {
                Slvs_hEntity p1 = addPoint2d(params, slvsEntities, entity.points[0], entity.id * 1000 + 0);
                Slvs_hEntity p2 = addPoint2d(params, slvsEntities, entity.points[1], entity.id * 1000 + 1);
                addLineSegment(slvsEntities, p1, p2, entity.id);
            }
            break;

        case EntityType::Circle:
            if (!entity.points.empty()) {
                addCircle(params, slvsEntities, entity.points[0], entity.radius, entity.id, normalHandle);
            }
            break;

        case EntityType::Arc:
            if (entity.points.size() >= 3) {
                addArc(params, slvsEntities, entity, normalHandle);
            } else if (!entity.points.empty()) {
                // Degenerate arc: fall back to a circle so it still solves.
                addCircle(params, slvsEntities, entity.points[0], entity.radius, entity.id, normalHandle);
            }
            break;

        // ---- Entity types added 2026-08-26 -----------------------------
        //
        // GOVERNING RULE for what gets registered: register a point only when
        // its FREE movement is representable by the entity's own stored
        // parameters. Registering a point the entity cannot actually express
        // lets the solver produce a shape that renders wrong: a hexagon
        // solved into a random heptagonal blob, or an arc slot whose two
        // endpoints stop being equidistant from its center.
        //
        // libslvs has no polygon, ellipse or spline primitive, so nothing
        // here fabricates one.

        case EntityType::Spline: {
            // Control points are free BY DEFINITION (moving one is exactly
            // what a spline means), so every one is registered as a point.
            std::vector<Slvs_hEntity> cps;
            cps.reserve(entity.points.size());
            for (std::size_t i = 0; i < entity.points.size(); ++i) {
                cps.push_back(addPoint2d(params, slvsEntities, entity.points[i],
                              entity.id * 1000 + static_cast<int>(i)));
            }
            // [piece B] A Bezier spline is additionally a SOLVER CURVE: register
            // each cubic segment (control points [3k..3k+3], sharing endpoints)
            // as SLVS_E_CUBIC, so tangent/curvature (G2) constraints can act on
            // it. Catmull-Rom stays points-only: it is fully determined by its
            // control points, so it has no free handles for such a constraint.
            if (entity.splineBezier) {
                const bool rational =
                    entity.splineRational && entity.weights.size() == entity.points.size();
                for (std::size_t k = 0; k + 3 < cps.size(); k += 3) {
                    Slvs_hEntity ch = nextEntityHandle++;
                    entityHandles[entity.id * 1000 + 500 + static_cast<int>(k / 3)] = ch;
#if defined(SLVS_HAS_RATIONAL_CUBIC)
                    if (rational) {
                        // Rational segment: the four control-point WEIGHTS are
                        // fixed params (group 1, not solved), the curve is an
                        // SLVS_E_RATIONAL_CUBIC. Control points stay free (group 2).
                        Slvs_hParam w0 = addParam(params, entity.weights[k],     1);
                        Slvs_hParam w1 = addParam(params, entity.weights[k + 1], 1);
                        Slvs_hParam w2 = addParam(params, entity.weights[k + 2], 1);
                        Slvs_hParam w3 = addParam(params, entity.weights[k + 3], 1);
                        slvsEntities.push_back(Slvs_MakeRationalCubic(
                            ch, sketchGroupId, workplaneHandle,
                            cps[k], cps[k + 1], cps[k + 2], cps[k + 3], w0, w1, w2, w3));
                        continue;
                    }
#endif
                    slvsEntities.push_back(Slvs_MakeCubic(ch, sketchGroupId, workplaneHandle,
                                           cps[k], cps[k + 1], cps[k + 2], cps[k + 3]));
                }
                // Closed spline: the wrap segment from the last anchor back to
                // control point 0 (cps has 3N points; segment N-1 wraps to cps[0]).
                if (entity.splineClosed && cps.size() >= 6 && cps.size() % 3 == 0) {
                    const int N = static_cast<int>(cps.size()) / 3;
                    const int k = 3 * (N - 1);
                    Slvs_hEntity ch = nextEntityHandle++;
                    entityHandles[entity.id * 1000 + 500 + (N - 1)] = ch;
                    slvsEntities.push_back(Slvs_MakeCubic(ch, sketchGroupId, workplaneHandle,
                                           cps[k], cps[k + 1], cps[k + 2], cps[0]));
                }
            }
            break;
        }

        case EntityType::Polygon:
            if (isRegularPolygon(entity)) {
                // Parametric: the vertices are DERIVED from center, radius,
                // sides and rotation. Free vertices are not representable, so
                // only the center is registered. The polygon can be
                // positioned and constrained by its center; it cannot be
                // deformed. Constraining its radius needs a real
                // regular-polygon primitive, which libslvs does not have.
                if (!entity.points.empty()) {
                    addPoint2d(params, slvsEntities, entity.points[0],
                               entity.id * 1000 + 0);
                }
            } else if (entity.points.size() >= 3) {
                // Freeform: a closed polyline. Every vertex is free, and the
                // edges are registered too so parallel/perpendicular/tangent
                // constraints can address them.
                const int n = static_cast<int>(entity.points.size());
                std::vector<Slvs_hEntity> verts;
                verts.reserve(static_cast<std::size_t>(n));
                for (int i = 0; i < n; ++i) {
                    verts.push_back(addPoint2d(params, slvsEntities,
                                               entity.points[static_cast<std::size_t>(i)],
                                               entity.id * 1000 + i));
                }
                // Edge i joins vertex i to vertex i+1, wrapping closed. Edge
                // keys start at 1000 so they cannot collide with vertex keys.
                for (int i = 0; i < n; ++i) {
                    addLineSegment(slvsEntities, verts[static_cast<std::size_t>(i)],
                                   verts[static_cast<std::size_t>((i + 1) % n)],
                                   entity.id * 1000 + 1000 + i);
                }
            }
            break;

        case EntityType::Ellipse:
            // Same reasoning as the regular polygon: the major/minor axes are
            // stored as scalars, so an axis endpoint moved freely by the
            // solver could not be written back. Center only.
            if (!entity.points.empty()) {
                addPoint2d(params, slvsEntities, entity.points[0],
                           entity.id * 1000 + 0);
            }
            break;

        case EntityType::Slot:
            // A path-following slot (pathEntityIds non-empty: one segment = a
            // capsule slot, many = a chain/loop/tree) is DERIVED from its
            // centerline after the solve (updateSlotFromPath / OutlineFromPaths);
            // its own points are not free, so registering them would add spurious
            // DOF the re-derive then overwrites. (Aaron: slots follow by
            // re-derivation, not a constraint.)
            if (!entity.pathEntityIds.empty()) break;
            if (entity.points.size() >= 3) {
                // ARC slot: center, start, end. start and end must stay
                // equidistant from center, and nothing here can enforce that:
                // a plain arc entity would fight the slot's half-width
                // radius, which means something different. Center only.
                addPoint2d(params, slvsEntities, entity.points[0],
                           entity.id * 1000 + 0);
            } else if (entity.points.size() == 2) {
                // LINEAR slot: the two points ARE its centerline ends, and
                // moving either is exactly what dragging its handle does, so
                // both are free. The centerline segment is registered too so
                // the slot can be made parallel to something.
                Slvs_hEntity c1 = addPoint2d(params, slvsEntities, entity.points[0],
                                             entity.id * 1000 + 0);
                Slvs_hEntity c2 = addPoint2d(params, slvsEntities, entity.points[1],
                                             entity.id * 1000 + 1);
                addLineSegment(slvsEntities, c1, c2, entity.id * 1000 + 1000);
            }
            break;

        // Rectangle and Parallelogram never reach the solver as compounds;
        // they are decomposed into lines plus constraints on creation. Text
        // and Dimension carry no solvable geometry.
        default:
            break;
        }
    }

    // Add constraints.
    //
    // DRIVEN constraints are skipped. A driven (reference) dimension MEASURES
    // the geometry; it does not move it. That is the whole distinction
    // Fusion 360 draws by putting driven values in parentheses, and this
    // codebase already honors it when rendering and in
    // getConstrainedEntityIds(). Handing one to the solver made it drive,
    // so a reference dimension whose value disagreed with the geometry
    // reported a phantom "Constraints are inconsistent" on a sketch that was
    // perfectly consistent. Driven values are recomputed FROM the solution
    // afterwards, by updateDrivenDimensions().
    for (const Constraint& constraint : constraints) {
        if (!constraint.enabled || !constraint.isDriving) continue;
        addConstraintToSolver(constraint, params, slvsEntities, slvsConstraints, entities);
    }
}

void Solver::Impl::extractSolution(
    const Slvs_System& sys,
    std::vector<Entity>& entities)
{
    for (Entity& entity : entities) {
        switch (entity.type) {
        case EntityType::Point:
            if (!entity.points.empty() && entityHandles.count(entity.id) > 0) {
                Slvs_hEntity pointHandle = entityHandles[entity.id];
                for (int i = 0; i < sys.entities; ++i) {
                    if (sys.entity[i].h == pointHandle) {
                        Slvs_hParam uParam = sys.entity[i].param[0];
                        Slvs_hParam vParam = sys.entity[i].param[1];
                        for (int j = 0; j < sys.params; ++j) {
                            if (sys.param[j].h == uParam) {
                                entity.points[0].x = sys.param[j].val;
                            }
                            if (sys.param[j].h == vParam) {
                                entity.points[0].y = sys.param[j].val;
                            }
                        }
                        break;
                    }
                }
            }
            break;

        case EntityType::Line:
            if (entity.points.size() >= 2) {
                for (int idx = 0; idx < 2; ++idx) {
                    int pointId = entity.id * 1000 + idx;
                    if (entityHandles.count(pointId) > 0) {
                        Slvs_hEntity pointHandle = entityHandles[pointId];
                        for (int i = 0; i < sys.entities; ++i) {
                            if (sys.entity[i].h == pointHandle) {
                                Slvs_hParam uParam = sys.entity[i].param[0];
                                Slvs_hParam vParam = sys.entity[i].param[1];
                                for (int j = 0; j < sys.params; ++j) {
                                    if (sys.param[j].h == uParam) {
                                        entity.points[idx].x = sys.param[j].val;
                                    }
                                    if (sys.param[j].h == vParam) {
                                        entity.points[idx].y = sys.param[j].val;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
            break;

        case EntityType::Arc:
            if (entity.points.size() >= 3) {
                // Read center/start/end back by their HobbyCAD point keys,
                // then re-derive radius and angles.  A SolveSpace arc keeps
                // |start-center| == |end-center| itself, so the radius comes
                // straight from the start point.
                Point2D c = entity.points[0];
                Point2D a = entity.points[1];
                Point2D b = entity.points[2];
                const bool okC = readSolvedPoint(sys, entity.id * 1000 + 0, c);
                const bool okA = readSolvedPoint(sys, entity.id * 1000 + 1, a);
                const bool okB = readSolvedPoint(sys, entity.id * 1000 + 2, b);

                if (okC && okA && okB) {
                    entity.points[0] = c;
                    entity.points[1] = a;
                    entity.points[2] = b;

                    const double dx = a.x - c.x, dy = a.y - c.y;
                    const double r = std::sqrt(dx * dx + dy * dy);
                    if (r > geometry::kExactEps) entity.radius = r;

                    const double a0 = radiansToDegrees(std::atan2(a.y - c.y, a.x - c.x));
                    const double a1 = radiansToDegrees(std::atan2(b.y - c.y, b.x - c.x));
                    const double ccw = normalizeAngle360(a1 - a0);

                    entity.startAngle = a0;
                    // A clockwise arc was registered with swapped endpoints;
                    // its magnitude is the complement of the CCW sweep.
                    const bool wasClockwise = (arcSwapped.count(entity.id) > 0);
                    double sweep = wasClockwise ? (ccw - 360.0) : ccw;
                    // Guard a degenerate readback wiping a valid sweep.
                    if (std::fabs(sweep) > geometry::kZeroEps || std::fabs(entity.sweepAngle) < geometry::kZeroEps) {
                        entity.sweepAngle = sweep;
                    }
                }
            }
            break;

        case EntityType::Circle:
            if (!entity.points.empty() && entityHandles.count(entity.id) > 0) {
                Slvs_hEntity circleHandle = entityHandles[entity.id];
                for (int i = 0; i < sys.entities; ++i) {
                    if (sys.entity[i].h == circleHandle) {
                        Slvs_hEntity centerHandle = sys.entity[i].point[0];
                        Slvs_hEntity radiusHandle = sys.entity[i].distance;

                        // Extract center point
                        for (int j = 0; j < sys.entities; ++j) {
                            if (sys.entity[j].h == centerHandle) {
                                Slvs_hParam uParam = sys.entity[j].param[0];
                                Slvs_hParam vParam = sys.entity[j].param[1];
                                for (int k = 0; k < sys.params; ++k) {
                                    if (sys.param[k].h == uParam) {
                                        entity.points[0].x = sys.param[k].val;
                                    }
                                    if (sys.param[k].h == vParam) {
                                        entity.points[0].y = sys.param[k].val;
                                    }
                                }
                                break;
                            }
                        }

                        // Extract radius (distance entity → param)
                        for (int j = 0; j < sys.entities; ++j) {
                            if (sys.entity[j].h == radiusHandle) {
                                Slvs_hParam rParam = sys.entity[j].param[0];
                                for (int k = 0; k < sys.params; ++k) {
                                    if (sys.param[k].h == rParam) {
                                        entity.radius = sys.param[k].val;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            break;

        // ---- Entity types added 2026-08-26 -----------------------------
        //
        // Each mirrors exactly what buildSolverSystem() registered. Anything
        // NOT registered there (a regular polygon's vertices, an ellipse's
        // axes, an arc slot's endpoints) is deliberately left alone here:
        // writing back a point the solver never owned would be worse than
        // not solving it at all.

        case EntityType::Spline:
            for (std::size_t i = 0; i < entity.points.size(); ++i) {
                readSolvedPoint(sys, entity.id * 1000 + static_cast<int>(i),
                                entity.points[i]);
            }
            break;

        case EntityType::Polygon:
            if (isRegularPolygon(entity)) {
                // Only the center was free. The vertices are derived, so
                // translate them by however far the center moved; otherwise
                // a constrained polygon would tear itself apart, center in
                // one place and outline in another.
                if (!entity.points.empty()) {
                    Point2D solved = entity.points[0];
                    if (readSolvedPoint(sys, entity.id * 1000 + 0, solved)) {
                        const double dx = solved.x - entity.points[0].x;
                        const double dy = solved.y - entity.points[0].y;
                        if (dx != 0.0 || dy != 0.0) {
                            for (Point3& p : entity.points) {
                                p.x += dx;
                                p.y += dy;
                            }
                        }
                    }
                }
            } else {
                for (std::size_t i = 0; i < entity.points.size(); ++i) {
                    readSolvedPoint(sys, entity.id * 1000 + static_cast<int>(i),
                                    entity.points[i]);
                }
            }
            break;

        case EntityType::Ellipse:
            // Center only; the axes are scalars and were never registered.
            // Any further points (the GUI keeps a major-axis point) ride
            // along with the center for the same reason as the polygon.
            if (!entity.points.empty()) {
                Point2D solved = entity.points[0];
                if (readSolvedPoint(sys, entity.id * 1000 + 0, solved)) {
                    const double dx = solved.x - entity.points[0].x;
                    const double dy = solved.y - entity.points[0].y;
                    if (dx != 0.0 || dy != 0.0) {
                        for (Point3& p : entity.points) {
                            p.x += dx;
                            p.y += dy;
                        }
                    }
                }
            }
            break;

        case EntityType::Slot:
            if (entity.points.size() >= 3) {
                // Arc slot: center was the only free point, so start and end
                // follow it rigidly and stay equidistant from it.
                Point2D solved = entity.points[0];
                if (readSolvedPoint(sys, entity.id * 1000 + 0, solved)) {
                    const double dx = solved.x - entity.points[0].x;
                    const double dy = solved.y - entity.points[0].y;
                    if (dx != 0.0 || dy != 0.0) {
                        for (Point3& p : entity.points) {
                            p.x += dx;
                            p.y += dy;
                        }
                    }
                }
            } else if (entity.points.size() == 2) {
                readSolvedPoint(sys, entity.id * 1000 + 0, entity.points[0]);
                readSolvedPoint(sys, entity.id * 1000 + 1, entity.points[1]);
            }
            break;

        default:
            break;
        }
    }
}

namespace {
// Lookups shared by addConstraintToSolver and the per-type helpers split out
// of it (addTangentConstraint, addMidpointConstraint): one definition each.
int safeIndexAt(const std::vector<int>& vec, size_t index, int defaultVal)
{
    return (index < vec.size()) ? vec[index] : defaultVal;
}
EntityType typeOfEntity(const std::vector<Entity>& entities, int entityId)
{
    const Entity* e = findEntityById(entities, entityId);
    return e ? e->type : EntityType::Point;
}
bool isCurveEntity(const std::vector<Entity>& entities, int entityId)
{
    const EntityType t = typeOfEntity(entities, entityId);
    return t == EntityType::Circle || t == EntityType::Arc;
}
bool isLineEntity(const std::vector<Entity>& entities, int entityId)
{
    return typeOfEntity(entities, entityId) == EntityType::Line;
}
}  // namespace


// addConstraintToSolver, Tangent case: line-to-circle and curve-curve tangency
// built from stock constraints. Split out verbatim; each early `break` of the
// original case is a `return` here (nothing follows the switch in the caller).
void Solver::Impl::addTangentConstraint(
    const Constraint& constraint, Slvs_hConstraint ch,
    std::vector<Slvs_Param>& params,
    std::vector<Slvs_Entity>& slvsEntities,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities)
{
    auto typeOf = [&entities](int entityId) -> EntityType { return typeOfEntity(entities, entityId); };
    auto isCurve = [&entities](int entityId) { return isCurveEntity(entities, entityId); };
    auto isLine = [&entities](int entityId) { return isLineEntity(entities, entityId); };

        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            solverDiag( "Solver: Tangent constraint %d: missing entities, skipping\n",
                     constraint.id);
            return;
        }
        {
            const int id1 = constraint.entityIds[0];
            const int id2 = constraint.entityIds[1];

            // SolveSpace's tangent constraints are ENDPOINT tangency: they
            // dereference the arc's point[1]/point[2].  This solver still
            // registers arcs via addCircle(), so an arc has no endpoints in
            // the system and both SLVS tangent constraints abort the process
            // (verified: circle+circle asserts "Unexpected entity types",
            // arc+line asserts "Cannot find handle").
            //
            // Skipping with a diagnostic is strictly better than aborting.
            // Real tangency needs arcs registered with Slvs_MakeArcOfCircle
            // first; once that lands, re-enable the branch below.
            const bool arcsAreRealArcsInSolver = true;
            if (!arcsAreRealArcsInSolver) {
                solverDiag(
                        "Solver: Tangent constraint %d skipped - arcs are "
                        "currently registered as circles, so SLVS tangent "
                        "constraints cannot be satisfied\n", constraint.id);
                return;
            }
            if (isCurve(id1) && isCurve(id2)) {
                // FREE TANGENCY OF TWO CURVES (circle or arc, any mix).
                //
                // SolveSpace's CURVE_CURVE_TANGENT is ENDPOINT tangency: it
                // reads an arc's center->endpoint direction, and it asserts
                // outright on a full circle, which has no endpoint (that was
                // the arc-circle and circle-circle abort). Build the real
                // thing from the definition instead, so "tangent" means the
                // curves TOUCH at one derived point, not that they share a
                // named vertex:
                //
                //   one touch point T that lies on BOTH curves' circles and
                //   is collinear with the two centers.
                //
                // "T lies on curve C": PT_ON_CIRCLE for a full circle; for an
                // arc (no PT_ON_ARC exists) make |center->T| equal the arc's
                // radius via EQUAL_LENGTH_LINES against center->start. The
                // collinear-centers line handles internal vs external by the
                // seed. Uses only stock libslvs constraints: no library
                // patch needed. DOF: one removed, as tangency should.
                const int a = id1, b = id2;
                const Slvs_hEntity centerA = getPointHandle(a, 0);
                const Slvs_hEntity centerB = getPointHandle(b, 0);
                if (centerA == 0 || centerB == 0) {
                    solverDiag( "Solver: Tangent constraint %d: a curve is not in the solver, skipping\n", constraint.id);
                    return;
                }
                const Entity* eA = nullptr; const Entity* eB = nullptr;
                for (const Entity& e : entities) {
                    if (e.id == a) eA = &e;
                    if (e.id == b) eB = &e;
                }
                double seedX = 0.0, seedY = 0.0;
                if (eA && eB && !eA->points.empty() && !eB->points.empty()) {
                    const Point2D ca = eA->points[0], cb = eB->points[0];
                    double vx = cb.x - ca.x, vy = cb.y - ca.y;
                    double vlen = std::sqrt(vx * vx + vy * vy);
                    if (vlen < geometry::kZeroEps) { vx = 1.0; vy = 0.0; vlen = 1.0; }
                    seedX = ca.x + eA->radius * vx / vlen;   // on curve A, toward B: external side
                    seedY = ca.y + eA->radius * vy / vlen;
                }
                const Slvs_hParam tu = addParam(params, seedX, sketchGroupId);
                const Slvs_hParam tv = addParam(params, seedY, sketchGroupId);
                const Slvs_hEntity touch = nextEntityHandle++;
                slvsEntities.push_back(Slvs_MakePoint2d(touch, sketchGroupId, workplaneHandle, tu, tv));

                // "touch lies on curve X": circle -> PT_ON_CIRCLE; arc ->
                // |centerX->touch| == arc radius (EQUAL_LENGTH_LINES).
                auto touchOnCurve = [&](int curveId, Slvs_hEntity centerH) {
                    if (typeOf(curveId) == EntityType::Circle) {
                        slvsConstraints.push_back(
                            Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                                SLVS_C_PT_ON_CIRCLE, workplaneHandle, 0.0,
                                touch, 0, entityHandles[curveId], 0));
                    } else {
                        const Slvs_hEntity startH = getPointHandle(curveId, 1);
                        if (startH == 0) return;
                        const Slvs_hEntity radLine = nextEntityHandle++;
                        slvsEntities.push_back(Slvs_MakeLineSegment(radLine, sketchGroupId, workplaneHandle, centerH, touch));
                        const Slvs_hEntity refLine = nextEntityHandle++;
                        slvsEntities.push_back(Slvs_MakeLineSegment(refLine, sketchGroupId, workplaneHandle, centerH, startH));
                        slvsConstraints.push_back(
                            Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                                SLVS_C_EQUAL_LENGTH_LINES, workplaneHandle, 0.0,
                                0, 0, radLine, refLine));
                    }
                };
                // The first constraint carries the HobbyCAD constraint id `ch`
                // so diagnostics can trace it; the rest get fresh handles.
                if (typeOf(a) == EntityType::Circle) {
                    slvsConstraints.push_back(
                        Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_PT_ON_CIRCLE,
                            workplaneHandle, 0.0, touch, 0, entityHandles[a], 0));
                } else {
                    const Slvs_hEntity startH = getPointHandle(a, 1);
                    const Slvs_hEntity radLine = nextEntityHandle++;
                    slvsEntities.push_back(Slvs_MakeLineSegment(radLine, sketchGroupId, workplaneHandle, centerA, touch));
                    const Slvs_hEntity refLine = nextEntityHandle++;
                    slvsEntities.push_back(Slvs_MakeLineSegment(refLine, sketchGroupId, workplaneHandle, centerA, startH));
                    slvsConstraints.push_back(
                        Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_EQUAL_LENGTH_LINES,
                            workplaneHandle, 0.0, 0, 0, radLine, refLine));
                }
                touchOnCurve(b, centerB);
                const Slvs_hEntity centerLine = nextEntityHandle++;
                slvsEntities.push_back(Slvs_MakeLineSegment(centerLine, sketchGroupId, workplaneHandle, centerA, centerB));
                slvsConstraints.push_back(
                    Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                        SLVS_C_PT_ON_LINE, workplaneHandle, 0.0, touch, 0, centerLine, 0));
            } else if ((isCurve(id1) && isLine(id2))
                       || (isLine(id1) && isCurve(id2))) {
                // LINE TANGENT TO A CIRCLE OR AN ARC.
                //
                // [HobbyCAD/Aaron] Arcs use this same touch-point construction
                // as circles, so the line actually TOUCHES the curve: a true
                // tangency in POSITION and direction. The old arc path used
                // SLVS_C_ARC_LINE_TANGENT, which only makes the line
                // perpendicular to the radius at an arc ENDPOINT: the line could
                // be "tangent" while floating far from the arc, never touching.
                // Treating the arc as its circle (PT_ON_CIRCLE derives the arc's
                // radius from center-to-start) fixes that; the touch may land off
                // the drawn span (the off-segment-tangent marker), as for a
                // circle.
                //
                // ARC_LINE_TANGENT cannot express this. It is ENDPOINT
                // tangency: it dereferences the arc's point[1]/point[2] and
                // constrains the line perpendicular to the radius there. A
                // circle registers only a center, so those handles do not
                // exist and libslvs aborts with "Cannot find handle".
                //
                // Built instead from the definition: a line is tangent to a
                // circle when it touches at a point whose radius meets it at
                // a right angle. Three constraints say exactly that:
                //
                //   the touch point lies ON the circle      (PT_ON_CIRCLE)
                //   the touch point lies ON the line        (PT_ON_LINE)
                //   the radius to it is perpendicular       (PERPENDICULAR)
                //
                // The first two alone would only make the line a SECANT,
                // crossing at that point; the right angle is what turns an
                // intersection into a tangency. Two of the three sit on a
                // helper point and a helper radius line that exist only in
                // the solver, the same device FixedAngle uses for its
                // reference line. Net effect on the sketch is one degree of
                // freedom removed, matching tangency to an arc.
                //
                // TANGENT DOES NOT IMPLY COINCIDENT, here or for an arc.
                // It says the line TOUCHES the circle, not that it touches
                // at either of its own endpoints: after solving, the touch
                // point sits wherever it lands along the line (measured at
                // t=0.487 of the segment in the usual case), and the line
                // stays free to slide along itself. Joining the line's END
                // to the circle is a separate constraint that removes a
                // further freedom: 7 -> 6 for the tangent, 6 -> 5 once
                // the endpoint is pinned. That is the same shape as the arc
                // case (9 -> 7 -> 6) and is deliberate: the two are
                // independent, and a sketch usually wants both, but the
                // solver should not decide that for the user.
                const int circleId = isCurve(id1) ? id1 : id2;   // the circle OR arc
                const int lineId   = isCurve(id1) ? id2 : id1;

                const Slvs_hEntity centerH = getPointHandle(circleId, 0);
                if (centerH == 0 || entityHandles.count(circleId) == 0
                                 || entityHandles.count(lineId) == 0) {
                    solverDiag( "Solver: Tangent constraint %d: circle or "
                                    "line not in solver, skipping\n", constraint.id);
                    return;
                }

                // Seed the touch point where the circle comes closest to the
                // line, so the solver starts beside the answer rather than
                // hunting for it. A bad seed on a system with two valid
                // solutions is how you get the far side of the circle.
                const Entity* circleE = nullptr;
                const Entity* lineE = nullptr;
                for (const Entity& e : entities) {
                    if (e.id == circleId) circleE = &e;
                    if (e.id == lineId)   lineE   = &e;
                }
                double seedX = 0.0, seedY = 0.0;
                if (circleE && lineE && !circleE->points.empty()
                        && lineE->points.size() >= 2) {
                    const Point2D c = circleE->points[0];
                    const Point2D a = lineE->points[0];
                    const Point2D b = lineE->points[1];
                    const double dx = b.x - a.x, dy = b.y - a.y;
                    const double len2 = dx * dx + dy * dy;
                    double fx = c.x, fy = c.y;
                    if (len2 > geometry::kExactEps) {
                        const double t = ((c.x - a.x) * dx + (c.y - a.y) * dy) / len2;
                        fx = a.x + t * dx;
                        fy = a.y + t * dy;
                    }
                    double vx = fx - c.x, vy = fy - c.y;
                    double vlen = std::sqrt(vx * vx + vy * vy);
                    if (vlen < geometry::kZeroEps) {
                        // The line runs through the center, so there is no
                        // "closest approach" direction to seed from. Step off
                        // along the line's NORMAL, never along the line: a
                        // touch point placed along the line puts the radius
                        // COLLINEAR with it, and a collinear radius can never
                        // be made perpendicular. Seeded that way the system
                        // starts on a contradiction and does not converge;
                        // the whole sketch then fails to solve, not just this
                        // constraint.
                        if (len2 > geometry::kExactEps) {
                            const double nlen = std::sqrt(len2);
                            vx = -dy / nlen;
                            vy =  dx / nlen;
                        } else {
                            vx = 0.0; vy = 1.0;   // degenerate zero-length line
                        }
                        vlen = 1.0;
                    }
                    seedX = c.x + circleE->radius * vx / vlen;
                    seedY = c.y + circleE->radius * vy / vlen;
                }

                const Slvs_hParam tu = addParam(params, seedX, sketchGroupId);
                const Slvs_hParam tv = addParam(params, seedY, sketchGroupId);
                const Slvs_hEntity touch = nextEntityHandle++;
                slvsEntities.push_back(Slvs_MakePoint2d(touch, sketchGroupId,
                                                        workplaneHandle, tu, tv));

                const Slvs_hEntity radiusLine = nextEntityHandle++;
                slvsEntities.push_back(Slvs_MakeLineSegment(radiusLine, sketchGroupId,
                                                            workplaneHandle,
                                                            centerH, touch));

                slvsConstraints.push_back(
                    Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_PT_ON_CIRCLE,
                                        workplaneHandle, 0.0,
                                        touch, 0, entityHandles[circleId], 0));
                slvsConstraints.push_back(
                    Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                                        SLVS_C_PT_ON_LINE, workplaneHandle, 0.0,
                                        touch, 0, entityHandles[lineId], 0));
                slvsConstraints.push_back(
                    Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                                        SLVS_C_PERPENDICULAR, workplaneHandle, 0.0,
                                        0, 0, radiusLine, entityHandles[lineId]));
            } else {
                solverDiag( "Solver: Tangent constraint %d: unsupported "
                                "operand types, skipping\n", constraint.id);
            }
        }
        return;

}

// addConstraintToSolver, Midpoint case: a point at the midpoint of a line (or,
// with the fork, of an arc). Split out verbatim; early `break`s are `return`s.
void Solver::Impl::addMidpointConstraint(
    const Constraint& constraint, Slvs_hConstraint ch,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities)
{
    auto safeGet = [](const std::vector<int>& vec, size_t index, int defaultVal) -> int {
        return safeIndexAt(vec, index, defaultVal);
    };
    auto typeOf = [&entities](int entityId) -> EntityType { return typeOfEntity(entities, entityId); };

        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Midpoint constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            return;
        }
        {
            // First entity names the point (its point index says which end,
            // defaulting to 0), second is the line it must sit at the middle
            // of. Reading the index lets "midpoint 3.1 5" pin the END of
            // line 3, not always its start.
            Slvs_hEntity pt = getPointHandle(constraint.entityIds[0],
                                              safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity line = 0;
            if (entityHandles.count(constraint.entityIds[1]) > 0) {
                line = entityHandles[constraint.entityIds[1]];
            }
            if (!pt || !line) {
                solverDiag( "Solver: Midpoint constraint %d: failed to resolve handles "
                         "(pt=%u, line=%u), skipping\n",
                         constraint.id, pt, line);
                return;
            }
            // A point at a LINE's midpoint uses SLVS_C_AT_MIDPOINT; at an ARC's
            // midpoint it needs the arc-aware constraint (0019), since the line
            // formula would read the arc's center/start as a segment.
            int midType = SLVS_C_AT_MIDPOINT;
            const bool midIsArc =
                (typeOf(constraint.entityIds[1]) == EntityType::Arc);
    #if defined(SLVS_HAS_ARC_MIDPOINT)
            if (midIsArc) midType = SLVS_C_ARC_MIDPOINT;
    #else
            if (midIsArc) {
                solverDiag( "Solver: Midpoint constraint %d on an arc skipped: "
                        "linked libslvs lacks SLVS_HAS_ARC_MIDPOINT (needs the 0019 cut)\n",
                        constraint.id);
                return;
            }
    #endif
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    midType,
                    workplaneHandle,
                    0.0,
                    pt, 0,
                    line, 0
                )
            );
        }
        return;

}

void Solver::Impl::addConstraintToSolver(
    const Constraint& constraint,
    std::vector<Slvs_Param>& params,
    std::vector<Slvs_Entity>& slvsEntities,
    std::vector<Slvs_Constraint>& slvsConstraints,
    const std::vector<Entity>& entities)
{
    Slvs_hConstraint ch = nextConstraintHandle++;
    constraintHandles[constraint.id] = ch;

    // Small lookups, shared with the per-type helpers split out of this
    // function (see safeIndexAt / typeOfEntity / isCurveEntity / isLineEntity).
    // "Equal" and "Tangent" mean different SLVS constraints depending on
    // whether the operands are lines or curves.
    auto safeGet = [](const std::vector<int>& vec, size_t index, int defaultVal) -> int {
        return safeIndexAt(vec, index, defaultVal);
    };
    auto typeOf = [&entities](int entityId) -> EntityType { return typeOfEntity(entities, entityId); };
    auto isCurve = [&entities](int entityId) { return isCurveEntity(entities, entityId); };
    auto isLine = [&entities](int entityId) { return isLineEntity(entities, entityId); };

    switch (constraint.type) {
    case ConstraintType::Distance:
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Distance constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));

            if (!pt1 || !pt2) {
                solverDiag( "Solver: Distance constraint %d: failed to resolve point handles "
                         "(entity %d -> handle %u, entity %d -> handle %u), skipping\n",
                         constraint.id,
                         constraint.entityIds[0], pt1,
                         constraint.entityIds[1], pt2);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_PT_PT_DISTANCE,
                    workplaneHandle,
                    constraint.value,
                    pt1, pt2,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (constraint.entityIds.empty()) {
            solverDiag( "Solver: Radius/Diameter constraint %d has no entityIds, skipping\n",
                     constraint.id);
            break;
        }
        if (entityHandles.count(constraint.entityIds[0]) == 0) {
            solverDiag( "Solver: Radius/Diameter constraint %d: entity %d not in solver, skipping\n",
                     constraint.id, constraint.entityIds[0]);
            break;
        }
        {
            Slvs_hEntity circleEntity = entityHandles[constraint.entityIds[0]];
            double diameterValue = (constraint.type == ConstraintType::Diameter)
                                   ? constraint.value
                                   : constraint.value * 2.0;

            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_DIAMETER,
                    workplaneHandle,
                    diameterValue,
                    0, 0,
                    circleEntity, 0
                )
            );
        }
        break;

    case ConstraintType::Angle:
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Angle constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        if (entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            solverDiag( "Solver: Angle constraint %d: entity not in solver (e0=%d, e1=%d), skipping\n",
                     constraint.id, constraint.entityIds[0], constraint.entityIds[1]);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];

            Slvs_Constraint ac = Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_ANGLE,
                    workplaneHandle,
                    constraint.value,
                    0, 0,
                    line1, line2);
            ac.other = constraint.supplementary ? 1 : 0;   // interior vs exterior
            slvsConstraints.push_back(ac);
        }
        break;

    case ConstraintType::Horizontal:
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            solverDiag( "Solver: Horizontal constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_HORIZONTAL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::Vertical:
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            solverDiag( "Solver: Vertical constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line = entityHandles[constraint.entityIds[0]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_VERTICAL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::Parallel:
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            solverDiag( "Solver: Parallel constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_PARALLEL,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line1, line2
                )
            );
        }
        break;

    case ConstraintType::Perpendicular:
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            solverDiag( "Solver: Perpendicular constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity line1 = entityHandles[constraint.entityIds[0]];
            Slvs_hEntity line2 = entityHandles[constraint.entityIds[1]];
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_PERPENDICULAR,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    line1, line2
                )
            );
        }
        break;

    case ConstraintType::Coincident:
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Coincident constraint %d has only %d entityIds (need 2), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));
            if (!pt1 || !pt2) {
                solverDiag( "Solver: Coincident constraint %d: failed to resolve point handles, skipping\n",
                         constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_POINTS_COINCIDENT,
                    workplaneHandle,
                    0.0,
                    pt1, pt2,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::Equal:
        if (constraint.entityIds.size() < 2 ||
            entityHandles.count(constraint.entityIds[0]) == 0 ||
            entityHandles.count(constraint.entityIds[1]) == 0) {
            solverDiag( "Solver: Equal constraint %d: missing entities, skipping\n",
                     constraint.id);
            break;
        }
        {
            const int id1 = constraint.entityIds[0];
            const int id2 = constraint.entityIds[1];
            Slvs_hEntity e1 = entityHandles[id1];
            Slvs_hEntity e2 = entityHandles[id2];

            // Equal means equal RADIUS for circles/arcs and equal LENGTH
            // for lines.  Emitting EQUAL_LENGTH_LINES for two circles is
            // silently wrong, which is what this used to do.
            int slvsType = 0;
            if (isCurve(id1) && isCurve(id2)) {
                slvsType = SLVS_C_EQUAL_RADIUS;
            } else if (isLine(id1) && isLine(id2)) {
                slvsType = SLVS_C_EQUAL_LENGTH_LINES;
            } else {
                solverDiag( "Solver: Equal constraint %d: operands are not "
                                "both lines or both curves, skipping\n",
                        constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    slvsType,
                    workplaneHandle,
                    0.0,
                    0, 0,
                    e1, e2
                )
            );
        }
        break;

#if defined(SLVS_HAS_CURVATURE)
    case ConstraintType::Curvature: {
        // [G2 piece C] Curvature (G2) continuity between two Bezier-spline
        // segments at a shared junction. entityIds = {splineA, splineB};
        // pointIndices = {endA, endB} pick the end (0 = start/first segment,
        // 1 = finish/last). Maps to the fork's SLVS_C_CURVATURE_CONTINUOUS on the
        // two SLVS_E_CUBIC segment entities piece B registered.
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Curvature %d: needs two curves, skipping\n", constraint.id);
            break;
        }
        const Entity* eA = nullptr; const Entity* eB = nullptr;
        for (const Entity& e : entities) {
            if (e.id == constraint.entityIds[0]) eA = &e;
            if (e.id == constraint.entityIds[1]) eB = &e;
        }
        // [0012] Each side may be a Bezier spline (a cubic SEGMENT) or an arc.
        auto isG2Curve = [](const Entity* e) {
            return (e->type == EntityType::Spline && e->splineBezier) ||
                   (e->type == EntityType::Arc && e->points.size() >= 3);
        };
        if (!eA || !eB || !isG2Curve(eA) || !isG2Curve(eB) ||
            !(eA->type == EntityType::Spline || eB->type == EntityType::Spline)) {
            solverDiag( "Solver: Curvature %d: each side must be a Bezier spline or an arc, "
                            "and at least one a spline, skipping\n", constraint.id);
            break;
        }
        auto resolveEnd = [&](const Entity* e, int endFlag, Slvs_hEntity& cubic, int& other) {
            other = (endFlag != 0) ? 1 : 0;
            if (e->type == EntityType::Arc) {
                // the arc's own SLVS_E_ARC_OF_CIRCLE; other picks its endpoint
                // (0 = start = point[1], 1 = end = point[2]), matching the fork.
                auto it = entityHandles.find(e->id);
                cubic = (it != entityHandles.end()) ? it->second : 0;
                return;
            }
            const int nseg = (static_cast<int>(e->points.size()) - 1) / 3;
            const int seg  = (endFlag != 0) ? (nseg > 0 ? nseg - 1 : 0) : 0;
            auto it = entityHandles.find(e->id * 1000 + 500 + seg);
            cubic = (it != entityHandles.end()) ? it->second : 0;
        };
        const int endA = constraint.pointIndices.size() > 0 ? constraint.pointIndices[0] : 1;
        const int endB = constraint.pointIndices.size() > 1 ? constraint.pointIndices[1] : 0;
        Slvs_hEntity cubicA = 0, cubicB = 0; int oA = 0, oB = 0;
        resolveEnd(eA, endA, cubicA, oA);
        resolveEnd(eB, endB, cubicB, oB);
        if (cubicA == 0 || cubicB == 0) {
            solverDiag( "Solver: Curvature %d: a spline segment is not in the solver, skipping\n", constraint.id);
            break;
        }
        // Slvs_MakeConstraint is the 9-arg handle maker; other/other2 (which end
        // of each cubic) are set on the struct afterward.
        Slvs_Constraint g2c = Slvs_MakeConstraint(
            nextConstraintHandle++, sketchGroupId, SLVS_C_CURVATURE_CONTINUOUS,
            workplaneHandle, 0.0, /*ptA*/0, /*ptB*/0, cubicA, cubicB);
        g2c.other = oA; g2c.other2 = oB;
        slvsConstraints.push_back(g2c);
        break;
    }
#else
    case ConstraintType::Curvature:
        solverDiag( "Solver: Curvature (G2) constraint %d skipped: linked libslvs lacks "
                        "SLVS_HAS_CURVATURE (needs the 0010 cut)\n", constraint.id);
        break;
#endif

#if defined(SLVS_HAS_CURVATURE_DIM)
    case ConstraintType::CurvatureDimension: {
        // [0013] Dimension the RADIUS of curvature at a Bezier spline end to a
        // length R. The fork's SLVS_C_CURVATURE sets SIGNED curvature; keep the
        // sign from the CURRENT geometry (so dimensioning R never flips the bend
        // direction) and pass kappa = sign(current) / R. pointIndices[0] picks
        // the end (0 = start/first segment, 1 = finish/last), default finish.
        if (constraint.entityIds.empty()) {
            solverDiag( "Solver: CurvatureDimension %d: needs a spline, skipping\n", constraint.id);
            break;
        }
        const Entity* e = nullptr;
        for (const Entity& x : entities) if (x.id == constraint.entityIds[0]) { e = &x; break; }
        if (!e || !(e->type == EntityType::Spline && e->splineBezier)) {
            solverDiag( "Solver: CurvatureDimension %d: entity must be a Bezier spline, skipping\n", constraint.id);
            break;
        }
        const double R = constraint.value;
        if (!(R > geometry::kZeroEps)) {
            solverDiag( "Solver: CurvatureDimension %d: radius must be > 0, skipping\n", constraint.id);
            break;
        }
        const int nseg = (static_cast<int>(e->points.size()) - 1) / 3;
        if (nseg < 1) {
            solverDiag( "Solver: CurvatureDimension %d: not a cubic spline, skipping\n", constraint.id);
            break;
        }
        const int endFlag = safeGet(constraint.pointIndices, 0, 1);
        const int seg = (endFlag != 0) ? nseg - 1 : 0;
        auto it = entityHandles.find(e->id * 1000 + 500 + seg);
        Slvs_hEntity cubic = (it != entityHandles.end()) ? it->second : 0;
        if (!cubic) {
            solverDiag( "Solver: CurvatureDimension %d: segment not in solver, skipping\n", constraint.id);
            break;
        }
        // current signed curvature at that end (same forward-tangent convention
        // as the fork): start uses P0,P1,P2; finish uses P1,P2,P3 of the segment.
        auto P = [&](int i){ return e->points[static_cast<size_t>(i)]; };
        double Tx, Ty, Sx, Sy;
        if (endFlag != 0) {
            const int b = 3 * seg;
            Tx = P(b+3).x - P(b+2).x;               Ty = P(b+3).y - P(b+2).y;
            Sx = P(b+3).x - 2*P(b+2).x + P(b+1).x;  Sy = P(b+3).y - 2*P(b+2).y + P(b+1).y;
        } else {
            Tx = P(1).x - P(0).x;                   Ty = P(1).y - P(0).y;
            Sx = P(0).x - 2*P(1).x + P(2).x;        Sy = P(0).y - 2*P(1).y + P(2).y;
        }
        const double cross = Tx*Sy - Ty*Sx;
        const double sign = (cross >= 0.0) ? 1.0 : -1.0;
        const double kappa = sign / R;
        // A rational segment's endpoint curvature depends on its weights
        // (scales by w0*w2/w1^2), so route it to the weight-aware constraint;
        // the sign, taken from the control polygon above, is unchanged by the
        // positive weights. Falls back to the unweighted id on an older lib.
        int curvType = SLVS_C_CURVATURE;
#if defined(SLVS_HAS_CURVATURE_RATIONAL)
        if (e->splineRational && e->weights.size() == e->points.size())
            curvType = SLVS_C_CURVATURE_RATIONAL;
#endif
        Slvs_Constraint cc = Slvs_MakeConstraint(
            ch, sketchGroupId, curvType, workplaneHandle, kappa,
            /*ptA*/0, /*ptB*/0, cubic, /*eB*/0);
        cc.other = (endFlag != 0) ? 1 : 0;
        slvsConstraints.push_back(cc);
        break;
    }
#else
    case ConstraintType::CurvatureDimension:
        solverDiag( "Solver: CurvatureDimension %d skipped: linked libslvs lacks "
                        "SLVS_HAS_CURVATURE_DIM (needs the 0013 cut)\n", constraint.id);
        break;
#endif

#if defined(SLVS_HAS_TANGENT_ANGLE)
    case ConstraintType::TangentAngle: {
        // [0015] Dimension the DIRECTED tangent angle (0-360 deg, constraint.value)
        // at a Bezier spline anchor. pointIndices[0] = the anchor control-point
        // index (3k); resolve to a cubic SEGMENT + end: first anchor -> seg 0
        // start, last anchor -> last seg finish, interior -> its segment's start
        // (the outgoing tangent). Maps to the fork SLVS_C_TANGENT_ANGLE on the
        // cubic; other picks start(0)/finish(1).
        if (constraint.entityIds.empty()) {
            solverDiag( "Solver: TangentAngle %d: needs a spline, skipping\n", constraint.id);
            break;
        }
        const Entity* e = nullptr;
        for (const Entity& x : entities) if (x.id == constraint.entityIds[0]) { e = &x; break; }
        if (!e || !(e->type == EntityType::Spline && e->splineBezier)) {
            solverDiag( "Solver: TangentAngle %d: entity must be a Bezier spline, skipping\n", constraint.id);
            break;
        }
        const int nseg = (static_cast<int>(e->points.size()) - 1) / 3;
        if (nseg < 1) {
            solverDiag( "Solver: TangentAngle %d: not a cubic spline, skipping\n", constraint.id);
            break;
        }
        const int a = safeGet(constraint.pointIndices, 0, 0);   // anchor control index
        const int lastCp = 3 * nseg;
        int seg, other;
        if (a <= 0)           { seg = 0;         other = 0; }
        else if (a >= lastCp) { seg = nseg - 1;  other = 1; }
        else                  { seg = a / 3;     other = 0; }   // interior = start of its segment
        auto it = entityHandles.find(e->id * 1000 + 500 + seg);
        Slvs_hEntity cubic = (it != entityHandles.end()) ? it->second : 0;
        if (!cubic) {
            solverDiag( "Solver: TangentAngle %d: segment not in solver, skipping\n", constraint.id);
            break;
        }
        int tanType = SLVS_C_TANGENT_ANGLE;
#if defined(SLVS_HAS_TANGENT_ANGLE_RATIONAL)
        if (e->splineRational && e->weights.size() == e->points.size())
            tanType = SLVS_C_TANGENT_ANGLE_RATIONAL;   // endpoint-equivalent, but explicit
#endif
        Slvs_Constraint cc = Slvs_MakeConstraint(
            ch, sketchGroupId, tanType, workplaneHandle, constraint.value,
            /*ptA*/0, /*ptB*/0, cubic, /*eB*/0);
        cc.other = other;
        slvsConstraints.push_back(cc);
        break;
    }
#else
    case ConstraintType::TangentAngle:
        solverDiag( "Solver: TangentAngle %d skipped: linked libslvs lacks "
                        "SLVS_HAS_TANGENT_ANGLE (needs the 0015 cut)\n", constraint.id);
        break;
#endif

    case ConstraintType::Tangent:
        addTangentConstraint(constraint, ch, params, slvsEntities, slvsConstraints, entities);
        break;
    case ConstraintType::Midpoint:
        addMidpointConstraint(constraint, ch, slvsConstraints, entities);
        break;
    case ConstraintType::Symmetric:
        if (constraint.entityIds.size() < 3) {
            solverDiag( "Solver: Symmetric constraint %d has only %d entityIds (need 3), skipping\n",
                     constraint.id, static_cast<int>(constraint.entityIds.size()));
            break;
        }
        {
            Slvs_hEntity pt1 = getPointHandle(constraint.entityIds[0], safeGet(constraint.pointIndices, 0, 0));
            Slvs_hEntity pt2 = getPointHandle(constraint.entityIds[1], safeGet(constraint.pointIndices, 1, 0));
            Slvs_hEntity line = 0;
            if (entityHandles.count(constraint.entityIds[2]) > 0) {
                line = entityHandles[constraint.entityIds[2]];
            }
            if (!pt1 || !pt2 || !line) {
                solverDiag( "Solver: Symmetric constraint %d: failed to resolve handles "
                         "(pt1=%u, pt2=%u, line=%u), skipping\n",
                         constraint.id, pt1, pt2, line);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_SYMMETRIC_LINE,
                    workplaneHandle,
                    0.0,
                    pt1, pt2,
                    line, 0
                )
            );
        }
        break;

    case ConstraintType::FixedPoint:
        // SLVS_C_WHERE_DRAGGED: tells the solver "keep this point where it is"
        if (constraint.entityIds.empty()) {
            solverDiag( "Solver: FixedPoint constraint %d has no entityIds, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity pt = getPointHandle(constraint.entityIds[0],
                                              safeGet(constraint.pointIndices, 0, 0));
            if (!pt) {
                solverDiag( "Solver: FixedPoint constraint %d: failed to resolve point handle, skipping\n",
                         constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_WHERE_DRAGGED,
                    workplaneHandle,
                    0.0,
                    pt, 0,
                    0, 0
                )
            );
        }
        break;

    case ConstraintType::FixedAngle:
        // Fix a line's angle from horizontal using an internal reference line.
        // The reference is placed in workplaneGroupId (group 1) so the solver
        // treats it as frozen geometry.
        if (constraint.entityIds.empty() || entityHandles.count(constraint.entityIds[0]) == 0) {
            solverDiag( "Solver: FixedAngle constraint %d: entity not in solver, skipping\n",
                     constraint.id);
            break;
        }
        {
            Slvs_hEntity targetLine = entityHandles[constraint.entityIds[0]];

            // Create internal horizontal reference line pinned at the origin
            Slvs_hParam ru1 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hParam rv1 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hEntity rp1 = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakePoint2d(rp1, workplaneGroupId,
                                                  workplaneHandle, ru1, rv1));

            Slvs_hParam ru2 = addParam(params, 100.0, workplaneGroupId);
            Slvs_hParam rv2 = addParam(params, 0.0, workplaneGroupId);
            Slvs_hEntity rp2 = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakePoint2d(rp2, workplaneGroupId,
                                                  workplaneHandle, ru2, rv2));

            Slvs_hEntity refLine = nextEntityHandle++;
            slvsEntities.push_back(Slvs_MakeLineSegment(refLine, workplaneGroupId,
                                                      workplaneHandle, rp1, rp2));

            slvsConstraints.push_back(
                Slvs_MakeConstraint(
                    ch, sketchGroupId,
                    SLVS_C_ANGLE,
                    workplaneHandle,
                    constraint.value,
                    0, 0,
                    refLine, targetLine
                )
            );
        }
        break;

    case ConstraintType::Concentric:
        // Two circles/arcs share a center: their center points coincide.
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Concentric constraint %d needs 2 entities, skipping\n",
                    constraint.id);
            break;
        }
        {
            Slvs_hEntity c1 = getPointHandle(constraint.entityIds[0], 0);
            Slvs_hEntity c2 = getPointHandle(constraint.entityIds[1], 0);
            if (!c1 || !c2) {
                solverDiag( "Solver: Concentric constraint %d: no center points, skipping\n",
                        constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_POINTS_COINCIDENT,
                                    workplaneHandle, 0.0, c1, c2, 0, 0));
        }
        break;

    case ConstraintType::PointOnLine:
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: PointOnLine constraint %d needs 2 entities, skipping\n",
                    constraint.id);
            break;
        }
        {
            // Whichever operand is the line is the line; the other supplies
            // the point, so selection order does not matter to the user.
            const int a = constraint.entityIds[0];
            const int b = constraint.entityIds[1];
            const int lineId  = isLine(a) ? a : b;
            const int pointId = isLine(a) ? b : a;
            if (!isLine(lineId)) {
                solverDiag( "Solver: PointOnLine constraint %d: no line operand, skipping\n",
                        constraint.id);
                break;
            }
            const int pIdx = (pointId == a) ? safeGet(constraint.pointIndices, 0, 0)
                                            : safeGet(constraint.pointIndices, 1, 0);
            Slvs_hEntity pt = getPointHandle(pointId, pIdx);
            if (!pt || !entityHandles.count(lineId)) {
                solverDiag( "Solver: PointOnLine constraint %d: unresolved handles, skipping\n",
                        constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_PT_ON_LINE,
                                    workplaneHandle, 0.0,
                                    pt, 0, entityHandles[lineId], 0));
        }
        break;

    case ConstraintType::PointOnCircle:
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: PointOnCircle constraint %d needs 2 entities, skipping\n",
                    constraint.id);
            break;
        }
        {
            const int a = constraint.entityIds[0];
            const int b = constraint.entityIds[1];
            const int circleId = isCurve(a) ? a : b;
            const int pointId  = isCurve(a) ? b : a;
            if (!isCurve(circleId)) {
                solverDiag( "Solver: PointOnCircle constraint %d: no circle operand, skipping\n",
                        constraint.id);
                break;
            }
            const int pIdx = (pointId == a) ? safeGet(constraint.pointIndices, 0, 0)
                                            : safeGet(constraint.pointIndices, 1, 0);
            Slvs_hEntity pt = getPointHandle(pointId, pIdx);
            if (!pt || !entityHandles.count(circleId)) {
                solverDiag( "Solver: PointOnCircle constraint %d: unresolved handles, skipping\n",
                        constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_PT_ON_CIRCLE,
                                    workplaneHandle, 0.0,
                                    pt, 0, entityHandles[circleId], 0));
        }
        break;

#if defined(SLVS_HAS_PT_ON_CUBIC)
    case ConstraintType::PointOnSpline:
        // [0011] Point lies on a Bezier spline. entityIds = {point-entity, spline}
        // (order-independent); the spline operand's point index selects the cubic
        // SEGMENT (default 0), the point operand's index selects the point. Maps
        // to the fork's SLVS_C_PT_ON_CUBIC on the SLVS_E_CUBIC segment piece B
        // registered. The curve parameter is unbounded, so a point dragged near
        // the segment stays on it (see the 0011 fork test).
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: PointOnSpline constraint %d needs 2 entities, skipping\n", constraint.id);
            break;
        }
        {
            const int a = constraint.entityIds[0], b = constraint.entityIds[1];
            const Entity* ea = nullptr; const Entity* eb = nullptr;
            for (const Entity& e : entities) { if (e.id == a) ea = &e; if (e.id == b) eb = &e; }
            const Entity* spline =
                (ea && ea->type == EntityType::Spline && ea->splineBezier) ? ea :
                (eb && eb->type == EntityType::Spline && eb->splineBezier) ? eb : nullptr;
            if (!spline) {
                solverDiag( "Solver: PointOnSpline constraint %d: needs a Bezier-spline operand, skipping\n", constraint.id);
                break;
            }
            const int splineId = spline->id;
            const int pointId  = (splineId == a) ? b : a;
            const int pIdx   = (pointId == a) ? safeGet(constraint.pointIndices, 0, 0)
                                              : safeGet(constraint.pointIndices, 1, 0);
            const int segReq = (splineId == a) ? safeGet(constraint.pointIndices, 0, 0)
                                               : safeGet(constraint.pointIndices, 1, 0);
            const int nseg = (static_cast<int>(spline->points.size()) - 1) / 3;
            const int seg  = (segReq >= 0 && segReq < nseg) ? segReq : 0;
            Slvs_hEntity pt = getPointHandle(pointId, pIdx);
            auto it = entityHandles.find(splineId * 1000 + 500 + seg);
            Slvs_hEntity cubic = (it != entityHandles.end()) ? it->second : 0;
            if (!pt || !cubic) {
                solverDiag( "Solver: PointOnSpline constraint %d: unresolved handles, skipping\n", constraint.id);
                break;
            }
            int rcType = SLVS_C_PT_ON_CUBIC;
#if defined(SLVS_HAS_RATIONAL_CUBIC)
            if (spline->splineRational && spline->weights.size() == spline->points.size())
                rcType = SLVS_C_PT_ON_RATIONAL_CUBIC;  // the segment is a rational cubic
#endif
            slvsConstraints.push_back(
                Slvs_MakeConstraint(ch, sketchGroupId, rcType,
                                    workplaneHandle, 0.0, pt, 0, cubic, 0));
        }
        break;
#else
    case ConstraintType::PointOnSpline:
        solverDiag( "Solver: PointOnSpline constraint %d skipped: linked libslvs lacks "
                        "SLVS_HAS_PT_ON_CUBIC (needs the 0011 cut)\n", constraint.id);
        break;
#endif

    case ConstraintType::Collinear:
        // libslvs has no single collinear constraint.  Two lines are
        // collinear when they are parallel AND a point of one lies on the
        // other, so emit both.  The extra constraint gets its own handle.
        if (constraint.entityIds.size() < 2) {
            solverDiag( "Solver: Collinear constraint %d needs 2 entities, skipping\n",
                    constraint.id);
            break;
        }
        {
            const int a = constraint.entityIds[0];
            const int b = constraint.entityIds[1];
            if (!isLine(a) || !isLine(b)) {
                solverDiag( "Solver: Collinear constraint %d: both operands must be "
                                "lines, skipping\n", constraint.id);
                break;
            }
            slvsConstraints.push_back(
                Slvs_MakeConstraint(ch, sketchGroupId, SLVS_C_PARALLEL,
                                    workplaneHandle, 0.0, 0, 0,
                                    entityHandles[a], entityHandles[b]));

            Slvs_hEntity ptB = getPointHandle(b, 0);
            if (ptB && entityHandles.count(a)) {
                slvsConstraints.push_back(
                    Slvs_MakeConstraint(nextConstraintHandle++, sketchGroupId,
                                        SLVS_C_PT_ON_LINE, workplaneHandle, 0.0,
                                        ptB, 0, entityHandles[a], 0));
            }
        }
        break;

    default:
        solverDiag( "Solver: constraint %d has unsupported type %d, skipping\n",
                 constraint.id, static_cast<int>(constraint.type));
        break;
    }
}

#endif  // HAVE_SLVS

// =====================================================================
//  Utility Functions
// =====================================================================

const char* sketchStateName(SketchState state)
{
    switch (state) {
    case SketchState::Empty:            return "empty";
    case SketchState::UnderConstrained: return "under-constrained";
    case SketchState::FullyConstrained: return "fully constrained";
    case SketchState::OverConstrained:  return "over-constrained";
    case SketchState::Inconsistent:     return "inconsistent";
    case SketchState::Failed:           return "failed";
    case SketchState::Unknown:          return "unknown";
    }
    return "unknown";
}

bool sketchStateHasDof(SketchState state)
{
    return state == SketchState::Empty
        || state == SketchState::UnderConstrained
        || state == SketchState::FullyConstrained
        || state == SketchState::OverConstrained;
}

std::string solveResultName(SolveResult::ResultCode code)
{
    switch (code) {
    case SolveResult::Okay:
        return "Solved successfully";
    case SolveResult::Inconsistent:
        return "Constraints are inconsistent (conflicting)";
    case SolveResult::DidntConverge:
        return "Solver didn't converge (too complex or ill-conditioned)";
    case SolveResult::TooManyUnknowns:
        return "Too many unknowns for solver";
    case SolveResult::RedundantOkay:
        return "Solved with redundant constraints";
    case SolveResult::InternalError:
        return "Solver failed internally and was recovered";
    default:
        return "Unknown solver result";
    }
}

bool constraintSupported(ConstraintType type)
{
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
    case ConstraintType::Angle:
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Coincident:
    case ConstraintType::Equal:
    case ConstraintType::Tangent:
    case ConstraintType::Midpoint:
    case ConstraintType::Symmetric:
    case ConstraintType::Concentric:
    case ConstraintType::Collinear:
    case ConstraintType::PointOnLine:
    case ConstraintType::PointOnCircle:
    case ConstraintType::FixedPoint:
    case ConstraintType::FixedAngle:
#if defined(SLVS_HAS_CURVATURE)
    case ConstraintType::Curvature:
#endif
#if defined(SLVS_HAS_PT_ON_CUBIC)
    case ConstraintType::PointOnSpline:
#endif
#if defined(SLVS_HAS_CURVATURE_DIM)
    case ConstraintType::CurvatureDimension:
#endif
        return true;
    default:
        return false;
    }
}

std::vector<ConstraintType> supportedConstraintTypes()
{
    return {
        ConstraintType::Distance,
        ConstraintType::Radius,
        ConstraintType::Diameter,
        ConstraintType::Angle,
        ConstraintType::Horizontal,
        ConstraintType::Vertical,
        ConstraintType::Parallel,
        ConstraintType::Perpendicular,
        ConstraintType::Coincident,
        ConstraintType::Equal,
        ConstraintType::Tangent,
        ConstraintType::Midpoint,
        ConstraintType::Symmetric,
        ConstraintType::FixedAngle,
#if defined(SLVS_HAS_CURVATURE)
        ConstraintType::Curvature,
#endif
#if defined(SLVS_HAS_PT_ON_CUBIC)
        ConstraintType::PointOnSpline,
#endif
#if defined(SLVS_HAS_CURVATURE_DIM)
        ConstraintType::CurvatureDimension,
#endif
    };
}


// =====================================================================
//  Fatal-error handler
// =====================================================================

// Both capabilities are advertised by the header itself, so no build-time
// probe is needed: SLVS_HAS_FATAL_ERROR_HANDLER means a handler can be
// installed, and SLVS_RESULT_INTERNAL_ERROR means the library can unwind
// and hand control back instead of ending the process.
#if defined(HAVE_SLVS) && defined(SLVS_HAS_FATAL_ERROR_HANDLER)
#  ifdef SLVS_RESULT_INTERNAL_ERROR
#    define HOBBYCAD_SLVS_CAN_RECOVER 1
#  endif

namespace {

Slvs_FatalErrorAction hobbycadSolverFatalHandler(const char* message, void* /*context*/)
{
    // Record what the solver told us and give the application a chance to
    // save, whichever way this ends.
    CrashHandler::reportLibraryFatal("libslvs", message);

#ifdef HOBBYCAD_SLVS_CAN_RECOVER
    // Ask the library to unwind and report instead of ending the process.
    // Slvs_Solve() then returns SLVS_RESULT_INTERNAL_ERROR, which
    // Solver::solve() reports as a failed solve.  The sketch geometry is
    // untouched, so the user keeps working.
    return SLVS_FATAL_RETURN_ERROR;
#else
    // Stock-plus-handler build: the process still ends, but the user's
    // work has been saved and the diagnostic recorded.
    return SLVS_FATAL_ABORT;
#endif
}

}  // namespace

void installSolverFatalHandler()
{
    Slvs_SetFatalErrorHandler(&hobbycadSolverFatalHandler, nullptr);
}

bool solverFatalHandlerAvailable() { return true; }

bool solverCanRecoverFromFaults()
{
#ifdef HOBBYCAD_SLVS_CAN_RECOVER
    return true;
#else
    return false;
#endif
}

#else

void installSolverFatalHandler()
{
    // Stock libslvs: nothing to install.  A solver fault will abort the
    // process, and the SIGABRT crash handler is the only net below it.
}

bool solverFatalHandlerAvailable() { return false; }
bool solverCanRecoverFromFaults() { return false; }

#endif

// Outside both branches: this has to answer even when there is no solver at
// all, and the macros it tests are only defined when there is one.
const char* solverVersionString()
{
#if !defined(HAVE_SLVS)
    return "not built";
#elif defined(SLVS_VERSION_STRING)
    return SLVS_VERSION_STRING;          // HobbyCAD-patched, e.g. "3.2p3"
#else
    // Stock libslvs carries NO version macro of any kind, so naming a number
    // here would be inventing one. Say what is actually known.
    return "stock (unversioned)";
#endif
}

}  // namespace sketch
}  // namespace hobbycad
