// =====================================================================
//  tests/solver/sketch3d_solve.cpp
//  3D sketch foundation: the (patched, installed) libslvs solves genuine
//  3D constraint systems and reports 3D free parameters. This is what a
//  dune3d-style 3D sketcher stands on, and the point-in-plane case is the
//  2D<->3D bridge (a 3D point pinned to a plane has exactly 2 DOF, i.e. it
//  behaves as a 2D sketch point).
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <cstdio>
#include <cstring>
#include <cmath>
#include <slvs.h>

static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static Slvs_Param      params[64];
static Slvs_Entity     entities[64];
static Slvs_Constraint cons[64];
static Slvs_hConstraint failed[64];
static Slvs_hParam     freeBuf[64];

static Slvs_System freshSystem() {
    Slvs_System sys;
    std::memset(&sys, 0, sizeof(sys));
    sys.param = params; sys.entity = entities; sys.constraint = cons;
    sys.failed = failed; sys.faileds = 64;
    sys.freeParams = freeBuf; sys.nfreeParams = 0;   // opt in to free-param reporting
    sys.calculateFaileds = 1;
    return sys;
}
static double paramVal(const Slvs_System& sys, Slvs_hParam h) {
    for (int i = 0; i < sys.params; ++i) if (sys.param[i].h == h) return sys.param[i].val;
    return NAN;
}
static bool freeContains(const Slvs_System& sys, Slvs_hParam h) {
    for (int i = 0; i < sys.nfreeParams; ++i) if (sys.freeParams[i] == h) return true;
    return false;
}

int main() {
#ifndef SLVS_HAS_FREE_PARAMS
    std::printf("SLVS_HAS_FREE_PARAMS not defined; header is not the patched one\n");
    return 2;
#endif
    std::printf("3D sketch solver foundation (libslvs, FREE_IN_3D)\n");

    // ---- A. a free 3D point: 3 DOF, all three coords reported free -------
    {
        Slvs_System sys = freshSystem();
        int np = 0, ne = 0;
        params[np++]   = Slvs_MakeParam(1, 1, 5.0);    // x
        params[np++]   = Slvs_MakeParam(2, 1, 6.0);    // y
        params[np++]   = Slvs_MakeParam(3, 1, 7.0);    // z
        entities[ne++] = Slvs_MakePoint3d(10, 1, 1, 2, 3);
        sys.params = np; sys.entities = ne; sys.constraints = 0;
        Slvs_Solve(&sys, 1);
        ck(sys.dof == 3, "A: free 3D point has 3 DOF");
        ck(sys.nfreeParams == 3, "A: three free parameters reported");
        ck(freeContains(sys, 1) && freeContains(sys, 2) && freeContains(sys, 3),
           "A: the free params are exactly x, y, z");
    }

    // ---- B. two 3D points, distance 10: solves to that distance ---------
    {
        Slvs_System sys = freshSystem();
        int np = 0, ne = 0, nc = 0;
        params[np++]   = Slvs_MakeParam(1, 1, 0.0);
        params[np++]   = Slvs_MakeParam(2, 1, 0.0);
        params[np++]   = Slvs_MakeParam(3, 1, 0.0);
        entities[ne++] = Slvs_MakePoint3d(10, 1, 1, 2, 3);
        params[np++]   = Slvs_MakeParam(4, 1, 3.0);   // start close, must move out
        params[np++]   = Slvs_MakeParam(5, 1, 0.0);
        params[np++]   = Slvs_MakeParam(6, 1, 0.0);
        entities[ne++] = Slvs_MakePoint3d(11, 1, 4, 5, 6);
        cons[nc++]     = Slvs_MakeConstraint(1, 1, SLVS_C_PT_PT_DISTANCE,
                                             SLVS_FREE_IN_3D, 10.0, 10, 11, 0, 0);
        sys.params = np; sys.entities = ne; sys.constraints = nc;
        Slvs_Solve(&sys, 1);
        ck(sys.result == SLVS_RESULT_OKAY, "B: distance system solved");
        const double dx = paramVal(sys,4) - paramVal(sys,1);
        const double dy = paramVal(sys,5) - paramVal(sys,2);
        const double dz = paramVal(sys,6) - paramVal(sys,3);
        const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        ck(std::fabs(dist - 10.0) < 1e-6, "B: the two points end up 10 apart in 3D");
        ck(sys.dof == 5, "B: one distance removes one of six DOF (5 left)");
    }

    // ---- C. a 3D point coincident with a FIXED reference: 0 DOF ---------
    {
        Slvs_System sys = freshSystem();
        int np = 0, ne = 0, nc = 0;
        // group 1: a fixed reference point at (2,3,4), not solved in group 2
        params[np++]   = Slvs_MakeParam(1, 1, 2.0);
        params[np++]   = Slvs_MakeParam(2, 1, 3.0);
        params[np++]   = Slvs_MakeParam(3, 1, 4.0);
        entities[ne++] = Slvs_MakePoint3d(10, 1, 1, 2, 3);
        // group 2: the point we solve for, starting elsewhere
        params[np++]   = Slvs_MakeParam(4, 2, 0.0);
        params[np++]   = Slvs_MakeParam(5, 2, 0.0);
        params[np++]   = Slvs_MakeParam(6, 2, 0.0);
        entities[ne++] = Slvs_MakePoint3d(11, 2, 4, 5, 6);
        cons[nc++]     = Slvs_MakeConstraint(1, 2, SLVS_C_POINTS_COINCIDENT,
                                             SLVS_FREE_IN_3D, 0.0, 11, 10, 0, 0);
        sys.params = np; sys.entities = ne; sys.constraints = nc;
        Slvs_Solve(&sys, 2);
        ck(sys.result == SLVS_RESULT_OKAY, "C: coincidence solved");
        ck(sys.dof == 0, "C: pinning to a fixed point removes all 3 DOF");
        ck(sys.nfreeParams == 0, "C: no free params when fully pinned");
        ck(std::fabs(paramVal(sys,4)-2.0) < 1e-6 &&
           std::fabs(paramVal(sys,5)-3.0) < 1e-6 &&
           std::fabs(paramVal(sys,6)-4.0) < 1e-6, "C: point lands on the reference");
    }

    // ---- D. the 2D<->3D bridge: a 3D point IN a plane has 2 DOF ---------
    {
        Slvs_System sys = freshSystem();
        int np = 0, ne = 0, nc = 0;
        // group 1: an XY workplane at the origin
        params[np++]   = Slvs_MakeParam(1, 1, 0.0);
        params[np++]   = Slvs_MakeParam(2, 1, 0.0);
        params[np++]   = Slvs_MakeParam(3, 1, 0.0);
        entities[ne++] = Slvs_MakePoint3d(10, 1, 1, 2, 3);
        double qw,qx,qy,qz; Slvs_MakeQuaternion(1,0,0, 0,1,0, &qw,&qx,&qy,&qz);
        params[np++]   = Slvs_MakeParam(4, 1, qw);
        params[np++]   = Slvs_MakeParam(5, 1, qx);
        params[np++]   = Slvs_MakeParam(6, 1, qy);
        params[np++]   = Slvs_MakeParam(7, 1, qz);
        entities[ne++] = Slvs_MakeNormal3d(11, 1, 4, 5, 6, 7);
        entities[ne++] = Slvs_MakeWorkplane(12, 1, 10, 11);
        // group 2: a free 3D point, then constrained to lie in the plane
        params[np++]   = Slvs_MakeParam(8, 2, 4.0);
        params[np++]   = Slvs_MakeParam(9, 2, 5.0);
        params[np++]   = Slvs_MakeParam(10, 2, 6.0);   // off the plane (z=6)
        entities[ne++] = Slvs_MakePoint3d(13, 2, 8, 9, 10);
        cons[nc++]     = Slvs_MakeConstraint(1, 2, SLVS_C_PT_IN_PLANE,
                                             SLVS_FREE_IN_3D, 0.0, 13, 0, 12, 0);
        sys.params = np; sys.entities = ne; sys.constraints = nc;
        Slvs_Solve(&sys, 2);
        ck(sys.result == SLVS_RESULT_OKAY, "D: point-in-plane solved");
        ck(sys.dof == 2, "D: a 3D point in a plane has 2 DOF (= a 2D sketch point)");
        ck(sys.nfreeParams == 2, "D: two free params for an on-plane 3D point");
    }

    if (fails == 0) std::printf("sketch3d_solve: ALL PASS\n");
    else            std::printf("sketch3d_solve: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
