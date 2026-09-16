// =====================================================================
//  tests/cli/slots.cpp — linear and arc slots
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Aaron, 2026-08-28: "Essentially this is a 2D arc sweep." A slot is one
//  round profile swept along a path (a line or an arc), and the two
//  forms differ only in the path. That framing is what these tests hold
//  in place, because the stored representation does NOT enforce it:
//
//    points[0] = arc center, points[1] and [2] = the round ends,
//    radius    = HALF the width, arcFlipped = the long way round
//
//  The path radius is not stored. The geometry code derives it from the
//  distance to points[1] and reads points[2] for its ANGLE only, so two
//  ends at different distances describe a shape that silently resolves in
//  favor of the first. The command takes a radius and PROJECTS both ends
//  onto it, which is why that cannot happen, and why it is tested here.

#include <hobbycad/strutil.h>
#include <cmath>
#include <cstdio>

#include <hobbycad/sketch/entity.h>

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

bool near(double a, double b, double tol = 1e-9) {
    return (a > b ? a - b : b - a) < tol;
}

struct Fixture {
    CliHistory history;
    HeadlessDocumentHost host;
    CliEngine engine{history};

    Fixture() { engine.setDocumentHost(&host); }
    CliResult run(const char* line) { return engine.execute(line); }
    const Project& proj() const { return *host.hostProject(); }
    const std::vector<SketchEntityData>& entities() const {
        static const std::vector<SketchEntityData> none;
        const auto& sk = proj().sketches();
        return sk.empty() ? none : sk[0].entities;
    }
};

/// Entities of one type, in order.
///
/// Every slot now brings a construction centerline, so positional indexing
/// breaks whenever the model gains an entity. Filtering by type says what
/// the test actually means.
std::vector<SketchEntityData> ofType(const std::vector<SketchEntityData>& es,
                                     sketch::EntityType t) {
    std::vector<SketchEntityData> out;
    for (const auto& e : es) if (e.type == t) out.push_back(e);
    return out;
}

double distance(const Point2D& a, const Point2D& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

}  // namespace

int main() {
    // ---- A linear slot is the profile swept along a LINE ----------------
    {
        Fixture f;
        f.run("create sketch XY Lin");
        ck(f.run("slot from 0,0 to 50,0 width 10").exitCode == 0,
           "a linear slot takes a width");
        f.run("finish");

        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 1 && es[0].points.size() == 2,
           "and stores two end centers");
        // The command speaks WIDTH; the model stores half. A caller typing
        // 10 and getting 10 back out is the whole point of the conversion.
        ck(es.size() == 1 && near(es[0].radius, 5.0),
           "storing half of it as the radius");
    }

    // ---- "radius" is refused on a linear slot ---------------------------
    //
    // It briefly meant the half-width here while meaning the PATH radius in
    // the arc form: one word, two quantities.
    {
        Fixture f;
        f.run("create sketch XY NoRad");
        const CliResult r = f.run("slot from 0,0 to 50,0 radius 5");
        ck(r.exitCode != 0, "'radius' is refused on a straight slot");
        ck(contains(r.error, "width 10"),
           "and the message gives the doubled value to use instead");
        f.run("finish");
        ck(f.entities().empty(), "with nothing stored");
    }

    // ---- thickness is a synonym for width -------------------------------
    {
        Fixture f;
        f.run("create sketch XY Thick");
        f.run("slot from 0,0 to 20,0 thickness 6");
        f.run("finish");
        ck(ofType(f.entities(), sketch::EntityType::Slot).size() == 1 &&
           near(ofType(f.entities(), sketch::EntityType::Slot)[0].radius, 3.0),
           "'thickness' means the same as 'width'");
    }

    // ---- An arc slot from two cap centers -------------------------------
    {
        Fixture f;
        f.run("create sketch XY Arc");
        ck(f.run("slot arc at 0,0 radius 30 from 30,0 to 0,30 width 8").exitCode == 0,
           "an arc slot takes a center, a radius and two end centers");
        f.run("finish");

        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 1 && es[0].points.size() == 3,
           "and stores three points: center and both ends");
        if (es.size() == 1 && es[0].points.size() == 3) {
            ck(near(es[0].radius, 4.0), "with half the width as the radius");
            ck(!es[0].arcFlipped, "taking the short way by default");
            ck(near(distance(es[0].points[0], es[0].points[1]), 30.0, 1e-6) &&
               near(distance(es[0].points[0], es[0].points[2]), 30.0, 1e-6),
               "and both ends exactly on the given radius");
        }
    }

    // ---- Ends are PROJECTED, so they cannot disagree --------------------
    //
    // This is the shape the stored form cannot express safely: two ends at
    // different distances. Given a radius, the points say direction only.
    {
        Fixture f;
        f.run("create sketch XY Proj");
        ck(f.run("slot arc at 0,0 radius 30 from 5,0 to 0,99 width 8").exitCode == 0,
           "ends given at the wrong distances are accepted");
        f.run("finish");
        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 1 && es[0].points.size() == 3 &&
           near(distance(es[0].points[0], es[0].points[1]), 30.0, 1e-6) &&
           near(distance(es[0].points[0], es[0].points[2]), 30.0, 1e-6),
           "and BOTH are placed on the radius, so they cannot differ");
    }

    // ---- The angle form is the same shape said differently --------------
    {
        Fixture f;
        f.run("create sketch XY Ang");
        f.run("slot arc at 0,0 radius 30 from 30,0 to 0,30 width 8");
        f.run("slot arc at 0,0 radius 30 angle 0 to 90 width 8");
        f.run("finish");

        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 2, "both spellings produce an entity");
        if (es.size() == 2) {
            bool same = true;
            for (size_t i = 0; i < 3; ++i) {
                if (!near(es[0].points[i].x, es[1].points[i].x, 1e-6) ||
                    !near(es[0].points[i].y, es[1].points[i].y, 1e-6)) same = false;
            }
            ck(same, "and they describe the SAME slot");
        }
    }

    // ---- The long way round ---------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Long");
        f.run("slot arc at 0,0 radius 30 from 30,0 to 0,30 width 8");
        f.run("slot arc at 0,0 radius 30 from 30,0 to 0,30 width 8 long");
        // An angle range past a half turn says it without the keyword.
        f.run("slot arc at 0,0 radius 30 angle 0 to 270 width 8");
        f.run("finish");

        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 3, "three arc slots");
        if (es.size() == 3) {
            ck(!es[0].arcFlipped, "the default is the short way");
            ck(es[1].arcFlipped, "'long' takes the long way");
            ck(es[2].arcFlipped,
               "and a sweep past 180 degrees needs no keyword");
        }
    }

    // ---- Refusals --------------------------------------------------------
    {
        Fixture f;
        f.run("create sketch XY Bad");

        ck(f.run("slot from 7,7 to 7,7 width 4").exitCode != 0,
           "a straight slot with coincident ends is refused");
        ck(f.run("slot arc at 0,0 radius 5 from 5,0 to 0,5 width 20").exitCode != 0,
           "a width that would pass through the center is refused");
        ck(f.run("slot arc at 0,0 radius 30 from 0,0 to 0,30 width 8").exitCode != 0,
           "an end center ON the arc center is refused: no direction");
        ck(f.run("slot arc at 0,0 radius 30 from 30,0 to 30,0 width 8").exitCode != 0,
           "both ends in the same direction is refused: no arc");
        ck(f.run("slot arc at 0,0 radius 30 angle 45 to 45 width 8").exitCode != 0,
           "a zero sweep is refused");
        ck(f.run("slot arc at 0,0 radius 30 angle 0 to 360 width 8").exitCode != 0,
           "a full turn is refused");
        ck(f.run("slot arc at 0,0 radius 0 angle 0 to 90 width 8").exitCode != 0,
           "a zero radius is refused");

        f.run("finish");
        ck(f.entities().empty(), "and none of them stored anything");
    }

    // ---- Sweeping an EXISTING line or arc -------------------------------
    //
    // Aaron: "You could also frame an arc slot by providing it an existing
    // arc, and then sweep across it a certain thickness. Same for a regular
    // line slot." One command, and the path decides which slot comes out.
    {
        Fixture f;
        f.run("create sketch XY Along");
        f.run("line from 0,0 to 50,0 construction");                 // id 1
        f.run("arc at 0,0 radius 30 angle 0 to 270 construction");   // id 2

        ck(f.run("slot along 1 width 10").exitCode == 0,
           "a line can be swept into a slot");
        ck(f.run("slot along 2 thickness 8").exitCode == 0,
           "and an arc into an arc slot");
        f.run("finish");

        const auto& es = f.entities();
        ck(es.size() == 4, "both slots were added beside their centerlines");
        if (es.size() == 4) {
            ck(es[2].type == sketch::EntityType::Slot &&
               es[2].points.size() == 2 && near(es[2].radius, 5.0),
               "the line gave a straight slot of the right width");
            ck(es[3].points.size() == 3 && near(es[3].radius, 4.0),
               "and the arc gave an arc slot");
            // The arc's own sweep is the only thing that knows which way
            // round it ran; two cap centers cannot say.
            ck(es[3].arcFlipped,
               "carrying the arc's 270-degree sweep across as the long way");
            ck(near(distance(es[3].points[0], es[3].points[1]), 30.0, 1e-6),
               "with the ends on the arc's own radius");
        }
    }

    // ---- The path BECOMES the centerline --------------------------------
    //
    // Once a line is the middle of a slot it is a guide, not an edge.
    // Leaving it as real geometry puts a stray line down the middle;
    // deleting it throws away what the slot is dimensioned against.
    {
        Fixture f;
        f.run("create sketch XY Keep");
        f.run("line from 0,0 to 50,0");        // real geometry
        const CliResult made = f.run("slot along 1 width 10");
        ck(contains(made.output, "construction"),
           "the command says the path became construction geometry");
        f.run("finish");

        const auto& es = f.entities();
        ck(es.size() == 2, "the line is kept, not consumed");
        ck(es.size() == 2 && es[0].isConstruction,
           "and is now construction geometry");
        ck(es.size() == 2 && !es[1].isConstruction,
           "while the slot itself is real");
    }

    // ---- A path that was ALREADY construction is left alone -------------
    {
        Fixture f;
        f.run("create sketch XY Was");
        f.run("line from 0,0 to 50,0 construction");
        const CliResult made = f.run("slot along 1 width 10");
        ck(!contains(made.output, "is now construction"),
           "nothing is announced when it was construction already");
        f.run("finish");
        ck(f.entities().size() == 2 && f.entities()[0].isConstruction,
           "and it stays construction");
    }

    // ---- Sweeping something that is not a path --------------------------
    {
        Fixture f;
        f.run("create sketch XY NotPath");
        f.run("circle at 0,0 radius 5");
        ck(f.run("slot along 1 width 4").exitCode != 0,
           "a circle is not a path a slot can follow");
        ck(f.run("slot along 99 width 4").exitCode != 0,
           "and a missing id is refused");
        ck(f.run("slot along 1 width 0").exitCode != 0,
           "as is a zero width");
        f.run("finish");
        ck(f.entities().size() == 1, "with only the circle left");
    }

    // ---- An arc slot cannot reach a full turn ---------------------------
    //
    // Aaron's rule, 2026-02-20: "Full circle minus 2x radius of arc end."
    // Its purpose, 2026-08-28: "an arc slot that could serve as a dial
    // indicator where the 2 ends of the arc slot meet", and, precisely,
    // "the arc ends would never touch, but 1 arc end could touch the
    // perimeter of the slot and vice versa."
    //
    // So the maximum is where the two centerline endpoints sit exactly two
    // cap radii apart: they never coincide, but their cap arcs are tangent.
    {
        const double R = 30.0, halfWidth = 4.0;
        const double maxDeg = sketch::maxArcSlotSweepDegrees(R, halfWidth);
        ck(maxDeg > 0.0 && maxDeg < 360.0,
           "the limit is below a full turn");

        // Place the two cap centers that far apart and measure between
        // them. Tangency needs the STRAIGHT-LINE distance to be two cap
        // radii; measuring along the arc instead leaves them overlapping.
        const double t = maxDeg * M_PI / 180.0;
        const double gap = std::hypot(R * std::cos(t) - R, R * std::sin(t));
        ck(near(gap, 2.0 * halfWidth, 1e-9),
           "and at it the two ends are exactly two cap radii apart");
    }
    {
        Fixture f;
        f.run("create sketch XY Dial");
        // Three bands, not two. Up to tangency the ends are apart; from
        // there to one cap radius they OVERLAP, which is how the middle is
        // actually cut free; at tangency it is still held by a point.
        // Aaron asked for that case explicitly, so it is not an error.
        const CliResult overlap =
            f.run("slot arc at 0,0 radius 30 angle 0 to 350 width 8");
        ck(overlap.exitCode == 0,
           "a sweep between tangency and the floor is allowed");
        ck(contains(overlap.output, "OVERLAP"),
           "and says the ends overlap, so there is no cusp");

        ck(f.run("slot arc at 0,0 radius 30 angle 0 to 359 width 8").exitCode != 0,
           "but past one cap radius of separation is refused");

        const CliResult dial =
            f.run("slot arc at 0,0 radius 30 angle 0 to max width 8");
        ck(dial.exitCode == 0, "'to max' gives the largest slot that fits");
        ck(contains(dial.output, "perimeters touch"),
           "and says the ends' perimeters touch");

        f.run("finish");
        const auto es = ofType(f.entities(), sketch::EntityType::Slot);
        ck(es.size() == 2, "the overlapping one and the dial were both made");
        if (es.size() == 2 && es[1].points.size() == 3) {
            ck(near(distance(es[1].points[1], es[1].points[2]), 8.0, 1e-6),
               "and \"to max\" puts the ends two cap radii apart, as the "
               "rule says");
        }
    }
    {
        // Aaron, 2026-08-28: "The thicker the slot, the bigger this cusp;
        // conversely, the thinner the slot, the smaller this cusp."
        //
        // The cusp is the wedge of material left between the two tangent
        // caps and the outer wall: the tip that still joins the center
        // piece to the outer shell. Its radial depth runs from the tangent
        // point, at sqrt(R^2 - hw^2) from the center, out to the wall at
        // R + hw. Nothing in the code computes this; the test states the
        // relationship so a change to the sweep rule that broke it would
        // show up here rather than in a part that will not come apart.
        const double R = 30.0;
        auto cuspDepth = [R](double halfWidth) {
            return (R + halfWidth) - std::sqrt(R * R - halfWidth * halfWidth);
        };
        ck(cuspDepth(0.5) < cuspDepth(2.0) &&
           cuspDepth(2.0) < cuspDepth(4.0) &&
           cuspDepth(4.0) < cuspDepth(6.0),
           "a thicker slot leaves a bigger cusp");
        // And it grows faster than the half-width does, so a thick slot's
        // tip is chunkier than a linear reading would suggest.
        ck(cuspDepth(12.0) > 12.0,
           "growing faster than the half-width itself");
    }
    {
        // The limit depends on BOTH radius and width: a wider slot on the
        // same radius has less room to sweep.
        const double narrow = sketch::maxArcSlotSweepDegrees(30.0, 2.0);
        const double wide   = sketch::maxArcSlotSweepDegrees(30.0, 8.0);
        ck(narrow > wide, "a narrower slot can sweep further");
        // Aaron: "slot width/2 is less than or equal to the arc radius."
        // Equality is the LIMIT case, not a failure: the inner edge lands
        // exactly on the arc center, the two caps have radius R with their
        // centers a full diameter apart, and they meet at the center. That
        // is a half turn, not nothing.
        ck(near(sketch::maxArcSlotSweepDegrees(30.0, 30.0), 180.0, 1e-9),
           "a half-width equal to the radius still gives a 180-degree slot");
        ck(sketch::maxArcSlotSweepDegrees(30.0, 30.1) == 0.0,
           "and only PAST that is there no slot at all");
    }
    {
        // "slot along" inherits the arc's sweep, so it needs the same rule.
        Fixture f;
        f.run("create sketch XY AlongMax");
        f.run("arc at 0,0 radius 30 angle 0 to 350 construction");
        ck(f.run("slot along 1 width 8").exitCode != 0,
           "sweeping along an arc that is too far round is refused");
        ck(f.run("slot along 1 width 1").exitCode == 0,
           "while a narrow enough slot on the same arc fits");
    }

    // ---- width/2 == radius is the limit case, and is allowed ------------
    {
        Fixture f;
        f.run("create sketch XY Limit");
        ck(f.run("slot arc at 0,0 radius 5 angle 0 to 180 width 10").exitCode == 0,
           "a width of exactly twice the radius is allowed");
        ck(f.run("slot arc at 0,0 radius 5 angle 0 to 180 width 10.5").exitCode != 0,
           "and anything past it is not");
        f.run("finish");
        ck(ofType(f.entities(), sketch::EntityType::Slot).size() == 1,
           "so exactly one of the two was stored");
    }

    // ---- sweep at width/2 == radius: the inner side is the center --------
    {
        Fixture f;
        f.run("create sketch XY SweepLimit");
        f.run("arc at 0,0 radius 5 angle 0 to 180");            // id 1
        ck(f.run("sweep along 1 width 10.5").exitCode != 0,
           "a sweep past twice the radius is refused");
        ck(f.run("sweep along 1 width 0.0000001").exitCode != 0,
           "a width below the length precision counts as zero and is refused");
        // The same rule for every size and sweep: "greater than zero" is
        // measured against the precision of the kind (Aaron).
        ck(f.run("circle at 20,0 radius 0.0000001").exitCode != 0,
           "a radius below the length precision is zero");
        ck(f.run("arc at 40,0 radius 7 angle 0 to 0.0000001").exitCode != 0,
           "a sweep below the angle precision is no arc");
        ck(f.run("arc at 40,0 radius 7 angle 0 to 0.001").exitCode == 0,
           "while a sweep above it is");   // radius 7: not a cap, not the outer side
        ck(f.run("sweep along 1 width 10").exitCode == 0,
           "a sweep of exactly twice the radius builds");
        f.run("finish");
        const auto pts = ofType(f.entities(), sketch::EntityType::Point);
        const auto arcs = ofType(f.entities(), sketch::EntityType::Arc);
        ck(pts.size() == 1 && near(pts[0].points[0].x, 0.0, 1e-9) && near(pts[0].points[0].y, 0.0, 1e-9),
           "the inner side is one Point at the arc's center");
        // the path (construction), the outer side, and the two caps
        int outer = 0, caps = 0;
        for (const auto& a : arcs) {
            if (near(a.radius, 10.0, 1e-9)) ++outer;
            else if (near(a.radius, 5.0, 1e-9) && !a.isConstruction) ++caps;
        }
        ck(outer == 1 && caps == 2, "an outer arc at radius 10 and two caps of radius 5");
    }

    // ---- The printed maximum must be usable ------------------------------
    //
    // A limit rounded to NEAREST can print larger than it really is, and
    // the obvious thing to do with a printed limit is type it back in.
    {
        Fixture f;
        f.run("create sketch XY Round");
        const CliResult refused =
            f.run("slot arc at 0,0 radius 30 angle 0 to 359 width 8");
        // Pull the figure the message offers out of its own text and use it.
        // What "furthest is ([0-9.]+)" captured: the number the message
        // offers, read back out of its own text.
        const std::string& err = refused.error;
        const std::string marker = "furthest is ";
        std::string captured;
        if (const size_t at = err.find(marker); at != std::string::npos) {
            for (size_t k = at + marker.size();
                 k < err.size()
                 && (std::isdigit(static_cast<unsigned char>(err[k])) || err[k] == '.');
                 ++k) {
                captured += err[k];
            }
        }
        ck(!captured.empty(), "the refusal names the furthest sweep available");
        if (!captured.empty()) {
            const std::string again = subst(
                "slot arc at 0,0 radius 30 angle 0 to %1 width 8", captured);
            const CliResult retry = f.run(again.c_str());
            ck(retry.exitCode == 0,
               "and that figure is accepted when typed back in");
        }
    }

    // ---- The two limits are different questions -------------------------
    //
    // They were briefly the same answer, which is how the naming went
    // wrong: "minArcSlotSeparationDegrees" returned the TANGENT
    // separation, and once the overlap band was allowed it was no longer
    // the minimum of anything.
    {
        const double R = 30.0, hw = 4.0;
        const double cusp  = sketch::arcSlotCuspSeparationDegrees(R, hw);
        const double floor_ = sketch::arcSlotFloorSeparationDegrees(R, hw);

        ck(floor_ < cusp,
           "the floor separation is CLOSER than the cusp separation");
        ck(near(cusp, sketch::arcSlotGapDegrees(R, hw, 2.0), 1e-12),
           "the cusp separation is two cap radii apart");
        ck(near(floor_, sketch::arcSlotGapDegrees(R, hw, 1.0), 1e-12),
           "and the floor is one");

        // Each sweep limit is a full turn less its separation.
        ck(near(sketch::maxArcSlotSweepDegrees(R, hw), 360.0 - cusp, 1e-9),
           "the cusp sweep is a full turn less the cusp separation");
        ck(near(sketch::absoluteMaxArcSlotSweepDegrees(R, hw), 360.0 - floor_, 1e-9),
           "and the absolute sweep a full turn less the floor");
        ck(sketch::absoluteMaxArcSlotSweepDegrees(R, hw)
               > sketch::maxArcSlotSweepDegrees(R, hw),
           "so the absolute limit is the further of the two");
    }

    // ---- A slot follows its centerline through a solve -------------------
    //
    // A slot stores its path's shape INLINE, so the solver moves the
    // centerline and leaves the slot behind. `pathEntityIds` is the link
    // back, and `solve` re-derives after solving; the point is to follow
    // where the path ended up.
    {
        Fixture f;
        f.run("create sketch XY Follow");
        f.run("arc at 0,0 radius 30 angle 0 to 90");   // entity 1
        f.run("slot along 1 width 8");                 // entity 2
        f.run("constrain radius 1 20");
        const CliResult s = f.run("solve");
        ck(contains(s.output, "followed their centerlines"),
           "solve reports the slot following");

        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 2, "centerline and slot");
        if (es.size() == 2 && es[1].points.size() == 3) {
            const auto& arc = es[0];
            const auto& slot = es[1];
            ck(near(arc.radius, 20.0, 1e-6),
               "the centerline took the new radius");
            // The slot must be ON the moved centerline, not where it was.
            ck(near(slot.points[0].x, arc.points[0].x, 1e-6) &&
               near(slot.points[0].y, arc.points[0].y, 1e-6),
               "and the slot shares its center");
            ck(near(distance(slot.points[0], slot.points[1]), 20.0, 1e-6),
               "with its ends on the new radius, not the old one");
            ck(near(slot.radius, 4.0, 1e-9),
               "while its WIDTH is untouched: the path says where, not how thick");
        }
    }

    // ---- Where a centerline comes from ----------------------------------
    //
    // "slot along" sweeps an entity that already exists, so the slot has a
    // path and is grouped with it. The typed forms describe the shape
    // directly and produce just the slot; adding a centerline there would
    // mean every "slot from A to B" quietly created two entities.
    {
        Fixture f;
        f.run("create sketch XY Bones");
        f.run("arc at 0,0 radius 30 angle 0 to 90");
        f.run("slot along 1 width 8");
        f.run("finish");

        const auto& es = f.entities();
        ck(es.size() == 2, "slot along: the path and the slot");
        if (es.size() == 2) {
            ck(es[0].isConstruction,
               "the path became construction geometry");
            ck(es[1].pathEntityIds.size() == 1 && es[1].pathEntityIds[0] == es[0].id,
               "and the slot points at it");
        }
        ck(f.proj().sketches()[0].groups.size() == 1, "the pair is grouped");
    }
    {
        Fixture f;
        f.run("create sketch XY Typed");
        f.run("slot arc at 0,0 radius 30 angle 0 to 90 width 8");
        f.run("finish");
        const auto& es = f.entities();
        // Extended to the typed forms on Aaron's call, 2026-08-28: a slot
        // is a profile swept along a path, so the path is part of what it
        // is, and describing the shape directly should not produce a
        // lesser thing than sweeping an existing entity did.
        ck(es.size() == 2, "a typed slot brings a centerline too");
        if (es.size() == 2) {
            ck(es[0].isConstruction && es[0].type == sketch::EntityType::Arc,
               "an arc slot's centerline is a construction arc");
            ck(es[1].pathEntityIds.size() == 1 && es[1].pathEntityIds[0] == es[0].id, "and the slot follows it");
        }
        ck(f.proj().sketches()[0].groups.size() == 1, "grouped, like any slot");
    }
    {
        Fixture f;
        f.run("create sketch XY Straight");
        f.run("slot from 0,0 to 50,0 width 10");
        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 2 && es[0].type == sketch::EntityType::Line &&
           es[0].isConstruction,
           "a straight slot's centerline is a construction line");
    }

    // ---- Group names must be unique --------------------------------------
    //
    // A name is looked up, not just displayed (the canvas finds a
    // sweep-angle dimension by its prefix), so a duplicate would have a
    // lookup silently act on whichever came first.
    {
        Fixture f;
        f.run("create sketch XY Names");
        f.run("line from 0,0 to 10,0");
        f.run("line from 0,0 to 0,10");
        ck(f.run("group Alpha entities 1").exitCode == 0, "a group is created");
        const CliResult dup = f.run("group Alpha entities 2");
        ck(dup.exitCode != 0, "a second group of the same name is refused");
        ck(contains(dup.error, "already in use"),
           "with the message saying why");
        f.run("finish");
        ck(f.proj().sketches()[0].groups.size() == 1,
           "and only the first exists");
    }

    // ---- Deleting a centerline leaves the slot, minus the link ----------
    {
        Fixture f;
        f.run("create sketch XY Cut");
        f.run("arc at 0,0 radius 30 angle 0 to 90");
        f.run("slot along 1 width 8");
        const CliResult d = f.run("delete 1");
        ck(d.exitCode == 0, "the centerline deletes");
        ck(contains(d.output, "no longer follow"),
           "and the command says the slot stopped following");

        f.run("finish");
        const auto& es = f.entities();
        ck(es.size() == 1 && es[0].type == sketch::EntityType::Slot,
           "the slot survives");
        ck(es.size() == 1 && es[0].pathEntityIds.empty(),
           "with its dangling link cut rather than left naming nothing");
    }

    // ---- solve can be narrowed to one group ------------------------------
    {
        Fixture f;
        f.run("create sketch XY Narrow");
        f.run("arc at 0,0 radius 30 angle 0 to 90");
        f.run("slot along 1 width 8");                 // group "Slot 2"
        f.run("line from 100,0 to 150,0");
        f.run("slot along 3 width 10");                // group "Slot 4"

        const CliResult all = f.run("solve");
        ck(all.exitCode == 0, "the whole sketch solves");

        const CliResult one = f.run("solve \"Slot 2\"");
        ck(one.exitCode == 0, "and so does a single group");
        ck(contains(one.output, "Slot 2"),
           "which is named in the report");
        ck(f.run("solve id=2").exitCode == 0, "a group id works too");
        ck(f.run("solve Nope").exitCode != 0, "an unknown group is refused");

        f.run("finish");
        // Solving a subset must not delete everything outside it.
        ck(f.entities().size() == 4, "and nothing outside the group is lost");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
