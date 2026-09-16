// =====================================================================
//  tests/cli/bezier.cpp — the CLI `bezier` command
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Checks the SKETCH, not the message: the created entity must be a
//  Bezier spline (splineBezier set) carrying the control polygon that
//  the in/out/tan handle grammar implies: [P0, out0, in1, P1, ...].
#include <hobbycad/strutil.h>
#include <cstdio>

#include <cmath>
#include "cliengine.h"
#include "clihistory.h"
#include "headlesshost.h"

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

namespace {
struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};
    Fixture() { engine.setDocumentHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    const std::vector<SketchEntityData>& entities(int sketchIndex = 0) const {
        static const std::vector<SketchEntityData> empty;
        const auto& sketches = host.hostProject()->sketches();
        if (sketchIndex >= static_cast<int>(sketches.size())) return empty;
        return sketches[static_cast<size_t>(sketchIndex)].entities;
    }
};
bool near(double a, double b) { return (a > b ? a - b : b - a) < 1e-9; }
int idFromOutput(const std::string& o) {
    const int a = o.find("[id ");
    if (a < 0) return -1;
    const int b = o.find(']', a);
    return toInt(trim(o.substr(a + 4, b - (a + 4))));
}
}  // namespace

int main() {
    // ---- one segment, explicit out/in handles --------------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult r = f.run("bezier 0,0 out 0 1 3,0 in 180 1");
        ck(r.exitCode == 0, "one-segment bezier command succeeds");
        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 1 && es[0].type == sketch::EntityType::Spline && es[0].splineBezier,
           "creates a Bezier spline entity (splineBezier set)");
        ck(es.size() == 1 && es[0].points.size() == 4,
           "one segment -> 4 control points");
        // out0 = P0 + 1*(cos0,sin0) = (1,0); in1 = P1 + 1*(cos180,sin180) = (2,0)
        ck(es.size() == 1 && es[0].points.size() == 4 &&
           near(es[0].points[0].x,0) && near(es[0].points[1].x,1) &&
           near(es[0].points[2].x,2) && near(es[0].points[3].x,3),
           "control polygon is [P0, out0, in1, P1] from the handle angles/lengths");
    }
    // ---- corners: no handles -> anchors collapse -----------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        f.run("bezier 0,0 5,5 10,0");
        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 1 && es[0].splineBezier && es[0].points.size() == 7,
           "three corner anchors -> 7 control points (two segments)");
    }
    // ---- tan: symmetric handles, direction check -----------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        f.run("bezier 0,0 tan 90 2 4,0 tan 90 2");
        f.run("finish");
        const auto& es = f.entities();
        // out0 = (0,0)+2*(cos90,sin90) = (0,2); in1 = (4,0)-2*(cos90,sin90) = (4,-2)
        ck(es.size() == 1 && es[0].points.size() == 4 &&
           near(es[0].points[1].x,0) && near(es[0].points[1].y,2) &&
           near(es[0].points[2].x,4) && near(es[0].points[2].y,-2),
           "tan produces a symmetric handle pair in the right direction");
    }
    // ---- too few anchors is an error -----------------------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult r = f.run("bezier 0,0");
        ck(r.exitCode != 0, "a single anchor is rejected");
    }

    // ---- points <id> shows a Bezier as anchors + handles ----------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult c = f.run("bezier 0,0 out 0 1  3,0 in 180 1");
        int id = -1;
        const std::string o = c.output;
        const int a = o.find("[id ");
        if (a >= 0) { const int b = o.find(']', a); id = toInt(trim(o.substr(a+4, b-(a+4)))); }
        ck(id > 0, "create reports an entity id");
        const std::string cmd = subst("points %1", id);
        CliResult p = f.run(cmd.c_str());
        ck(p.exitCode == 0 && contains(p.output, "bezier"),
           "points <id> labels a Bezier spline as bezier");
        ck(contains(p.output, "out") && contains(p.output, "deg"),
           "points <id> shows handles as angle+length");
    }

    // ---- edit one handle precisely --------------------------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult c = f.run("bezier 0,0 out 0 1  3,0 in 180 1");
        int id = -1;
        { const std::string o=c.output; const int a=o.find("[id ");
          if(a>=0){ const int b=o.find(']',a); id=toInt(trim(o.substr(a+4,b-(a+4)))); } }
        const std::string e1 = subst("bezier %1 handle 0 out 90 2", id);
        CliResult ed = f.run(e1.c_str());
        ck(ed.exitCode == 0, "edit anchor 0 out handle succeeds");
        f.run("finish");
        const auto& es = f.entities();
        // out0 = control point index 1 -> (0,0)+2*(cos90,sin90) = (0,2)
        ck(es.size()==1 && es[0].points.size()==4 && near(es[0].points[1].x,0) && near(es[0].points[1].y,2),
           "edited out handle moved control point 1 to (0,2)");
    }
    // ---- an in-handle on the first anchor is rejected -------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult c = f.run("bezier 0,0 out 0 1  3,0 in 180 1");
        int id = -1;
        { const std::string o=c.output; const int a=o.find("[id ");
          if(a>=0){ const int b=o.find(']',a); id=toInt(trim(o.substr(a+4,b-(a+4)))); } }
        const std::string e2 = subst("bezier %1 handle 0 in 45 1", id);
        CliResult ed = f.run(e2.c_str());
        ck(ed.exitCode != 0, "in-handle on the first anchor is rejected");
    }

    // ---- constrain curvature between two bezier ends --------------------
    {
        Fixture f;
        f.run("create sketch XY g");
        CliResult a = f.run("bezier 0,0 out 45 1.5  3,3 in 225 1.5");
        CliResult b = f.run("bezier 3,3 out 20 1.5  6,0 in 135 1.5");
        const int ida = idFromOutput(a.output), idb = idFromOutput(b.output);
        ck(ida > 0 && idb > 0, "two beziers created with ids");
        const std::string cmd = subst("constrain curvature %1.1 %2.0", ida, idb);
        CliResult cc = f.run(cmd.c_str());
        ck(cc.exitCode == 0, "constrain curvature between two bezier ends succeeds");
        CliResult ls = f.run("constraints");
        ck(contains(ls.output, "Curvature"), "the Curvature constraint is listed");
    }
    // ---- curvature rejects a non-spline operand ------------------------
    {
        Fixture f;
        f.run("create sketch XY g");
        CliResult a = f.run("bezier 0,0 out 0 1  3,3 out 0 1");
        CliResult l = f.run("line from 0,0 to 5,5");
        const int ida = idFromOutput(a.output), idl = idFromOutput(l.output);
        ck(ida > 0 && idl > 0, "a bezier and a line created");
        const std::string cmd = subst("constrain curvature %1.1 %2.0", ida, idl);
        CliResult cc = f.run(cmd.c_str());
        ck(cc.exitCode != 0, "curvature on a non-spline operand is rejected");
    }

    // ---- point on spline: the point is pulled onto the bezier -----------
    {
        Fixture f;
        f.run("create sketch XY g");
        // cubic (0,0)(0,1)(1,1)(1,0), peaks near (0.5,0.75)
        CliResult sp = f.run("bezier 0,0 out 90 1  1,0 in 90 1");
        CliResult pt = f.run("point at 0.5,0.6");            // started near the curve
        const int sid = idFromOutput(sp.output), pid = idFromOutput(pt.output);
        ck(sid > 0 && pid > 0, "bezier and point created");
        const std::string cmd = subst("constrain pointonspline %1.0 %2", pid, sid);
        CliResult cc = f.run(cmd.c_str());
        ck(cc.exitCode == 0, "constrain pointonspline succeeds");
        CliResult sv = f.run("solve");
        ck(sv.exitCode == 0, "solve succeeds with a point-on-spline constraint");
        f.run("finish");
        const auto& es = f.entities();
        const SketchEntityData* pe = nullptr; const SketchEntityData* se = nullptr;
        for (const auto& e : es) {
            if (e.type == sketch::EntityType::Point)  pe = &e;
            if (e.type == sketch::EntityType::Spline) se = &e;
        }
        bool onCurve = false;
        if (pe && se && !pe->points.empty()) {
            hobbycad::Point2D pp{ pe->points[0].x, pe->points[0].y };
            onCurve = se->distanceTo(pp) < 5e-3;  // sampled distanceTo; off-curve would be ~0.15
        }
        ck(onCurve, "the point ends up on the bezier curve after solving");
    }
    // ---- curvature between a bezier and an arc (cubic<->arc G2, fork 0012) --
    {
        Fixture f;
        f.run("create sketch XY g");
        CliResult bz = f.run("bezier 0,0 out 45 1.5  3,3 in 225 1.5");
        CliResult ar = f.run("arc at 0,0 radius 2 angle 0 to 90");
        const int bid = idFromOutput(bz.output), aid = idFromOutput(ar.output);
        ck(bid > 0 && aid > 0, "bezier and arc created");
        const std::string cmd = subst("constrain curvature %1.1 %2.1", bid, aid);
        CliResult cc = f.run(cmd.c_str());
        ck(cc.exitCode == 0, "curvature between a bezier and an arc is accepted");
        CliResult sv = f.run("solve");
        ck(sv.exitCode == 0, "solve succeeds with a cubic<->arc curvature constraint");
    }
    // ---- radius-of-curvature dimension (sign kept from geometry) ---------
    {
        Fixture f;
        f.run("create sketch XY g");
        // control polygon (0,0)(0,1)(1,1)(1,0): start curvature is -1 (R=1)
        CliResult bz = f.run("bezier 0,0 out 90 1  1,0 in 90 1");
        const int bid = idFromOutput(bz.output);
        ck(bid > 0, "bezier created");
        const std::string cmd = subst("constrain radiusofcurvature %1.0 4", bid);
        CliResult cc = f.run(cmd.c_str());
        ck(cc.exitCode == 0, "radius-of-curvature dimension accepted");
        CliResult sv = f.run("solve");
        ck(sv.exitCode == 0, "solve succeeds with a radius-of-curvature dimension");
        f.run("finish");
        const auto& es = f.entities();
        double kappa = 0.0; bool got = false;
        for (const auto& e : es) if (e.type == sketch::EntityType::Spline && e.points.size() >= 3) {
            const double Tx = e.points[1].x - e.points[0].x, Ty = e.points[1].y - e.points[0].y;
            const double Sx = e.points[0].x - 2*e.points[1].x + e.points[2].x;
            const double Sy = e.points[0].y - 2*e.points[1].y + e.points[2].y;
            const double m = std::hypot(Tx, Ty);
            if (m > 1e-9) { kappa = (Tx*Sy - Ty*Sx) / (m*m*m); got = true; }
        }
        ck(got && std::fabs(std::fabs(kappa) - 0.25) < 1e-3, "radius 4 -> |curvature| = 0.25 after solve");
        ck(got && kappa < 0.0, "the original (negative) bend direction is kept");
    }
    // ---- rational bezier via per-anchor weight ---------------------------
    {
        Fixture f;
        f.run("create sketch XY b");
        CliResult r = f.run("bezier 0,0 out 90 1 weight 1  1,0 in 90 1 weight 3");
        ck(r.exitCode == 0, "rational bezier command succeeds");
        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 1 && es[0].splineBezier && es[0].splineRational,
           "per-anchor weight creates a rational bezier");
        ck(es.size() == 1 && es[0].weights.size() == es[0].points.size() && es[0].points.size() == 4,
           "a weight per control point (4 for one segment)");
        ck(es.size() == 1 && es[0].weights.size() == 4 &&
           near(es[0].weights[0], 1.0) && near(es[0].weights[3], 3.0),
           "anchor weights map to their control points (1,1,3,3)");
    }
    {   // no weights -> plain (non-rational) bezier
        Fixture f;
        f.run("create sketch XY b");
        f.run("bezier 0,0 5,5 10,0");
        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 1 && es[0].splineBezier && !es[0].splineRational,
           "no weights -> non-rational bezier");
    }
    std::printf("%s\n", fails ? "FAILED" : "OK");
    return fails ? 1 : 0;
}
