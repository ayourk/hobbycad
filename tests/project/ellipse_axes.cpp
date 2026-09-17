// =====================================================================
//  tests/project/ellipse_axes.cpp — an ellipse's axes are real geometry
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  An ellipse used to store its axes as three scalars the solver could not
//  see, so no dimension could ever reach a radius and no drag could turn
//  one. The axes are geometry now: the center plus a point on each axis.
//
//  The scalars remain the serialized form, so the file format is unchanged
//  and an ellipse written by an older build still loads. These check the
//  two representations stay in step, in both directions.
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/project.h>
#include <cstdio>
#include <cmath>
#include <filesystem>
using namespace hobbycad;
using namespace hobbycad::sketch;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures;
}
static bool near(double a, double b, double e = 1e-9) { return std::fabs(a - b) < e; }

int main(int argc, char** argv)
{
    const std::string base = argc > 1 ? argv[1] : "/tmp/hobbycad_ellipse_axes";
    std::printf("an ellipse's axes are real points\n");

    // ---- created with its axes ------------------------------------------
    {
        Entity e = createEllipse(1, Point2D(10.0, 5.0), 8.0, 3.0, 30.0);
        const bool sized = e.points.size() == 3;
        check(sized, "a new ellipse carries center plus two axis points");

        const double th = 30.0 * M_PI / 180.0;
        // +major axis, and +minor a quarter turn from it. Guarded: indexing
        // a point that is not there is undefined behavior, and a test that
        // reads out of bounds is not entitled to the answer it gets.
        check(sized && near(e.points[1].x, 10.0 + 8.0 * std::cos(th), 1e-9)
              && near(e.points[1].y, 5.0 + 8.0 * std::sin(th), 1e-9),
              "the major-axis point sits at major radius along the rotation");
        check(sized && near(e.points[2].x, 10.0 - 3.0 * std::sin(th), 1e-9)
              && near(e.points[2].y, 5.0 + 3.0 * std::cos(th), 1e-9),
              "the minor-axis point sits a quarter turn from the major one");
    }

    // ---- an older ellipse is brought up to date -------------------------
    {
        // What a file written before the axes became geometry holds, and
        // what DXF import and projection used to produce: center only.
        Entity old;
        old.id = 7;
        old.type = EntityType::Ellipse;
        old.points.push_back(Point2D(0.0, 0.0));
        old.majorRadius = 6.0;
        old.minorRadius = 2.0;
        old.ellipseRotation = 90.0;

        check(ensureEllipseAxisPoints(old), "a one-point ellipse is upgraded");
        check(old.points.size() == 3, "and now has its two axis points");
        check(near(old.points[1].x, 0.0, 1e-9) && near(old.points[1].y, 6.0, 1e-9),
              "the axis points follow the stored rotation, not the +X axis");

        check(!ensureEllipseAxisPoints(old), "upgrading again is a no-op");
    }

    // ---- moving the points moves the scalars ----------------------------
    {
        Entity e = createEllipse(2, Point2D(0.0, 0.0), 10.0, 4.0, 0.0);
        const bool sized = e.points.size() == 3;
        if (sized) {
            // Pretend the solver moved the major-axis point: same length,
            // turned a quarter, with the minor still perpendicular to it.
            e.points[1] = Point3{0.0, 10.0, 0.0};
            e.points[2] = Point3{-4.0, 0.0, 0.0};
        }
        check(sized && syncEllipseFields(e), "the scalars re-derive from the points");
        // These two are only meaningful once the points above were written:
        // without them the scalars keep their original values, which happen
        // to equal what is expected here.
        check(sized && near(e.majorRadius, 10.0, 1e-9), "major radius follows its point");
        check(sized && near(e.minorRadius, 4.0, 1e-9), "minor radius follows its point");
        check(sized && near(e.ellipseRotation, 90.0, 1e-9),
              "the rotation is the major axis's own angle");
    }

    // ---- the longer axis IS the major one -------------------------------
    {
        Entity e = createEllipse(3, Point2D(0.0, 0.0), 10.0, 4.0, 0.0);
        const bool sized = e.points.size() == 3;
        // Drag the minor axis out past the major: the roles must swap, and
        // the frame turn a quarter with them, or the shape would change.
        if (sized) e.points[2] = Point3{0.0, 25.0, 0.0};
        check(sized && syncEllipseFields(e), "the scalars re-derive after the drag");
        check(sized && near(e.majorRadius, 25.0, 1e-9),
              "the longer axis became the major one");
        check(sized && near(e.minorRadius, 10.0, 1e-9),
              "and the shorter one the minor");
        check(sized && near(e.ellipseRotation, 90.0, 1e-9),
              "the frame turned a quarter with the swap, so the SHAPE is "
              "unchanged rather than silently redrawn");
    }

    // ---- through a save and a load --------------------------------------
    {
        fs::remove_all(base); fs::create_directories(base);
        const std::string dir = base + "/proj";
        {
            Project p;
            SketchData s; s.name = "axes";
            s.entities.push_back(createEllipse(1, Point2D(2.0, 2.0), 9.0, 4.0, 20.0));
            p.addSketch(s);
            std::string err;
            check(p.save(dir, &err), "save succeeds");
        }
        {
            Project p; std::string err;
            check(p.load(dir, &err), "load succeeds");
            bool ok = !p.sketches().empty() && !p.sketches()[0].entities.empty();
            check(ok, "the ellipse loads");
            if (ok) {
                const auto& e = p.sketches()[0].entities[0];
                check(e.points.size() == 3,
                      "and comes back with its axis points, so a loaded "
                      "ellipse drags and solves like a new one");
                check(near(e.majorRadius, 9.0, 1e-9) && near(e.minorRadius, 4.0, 1e-9)
                      && near(e.ellipseRotation, 20.0, 1e-9),
                      "with the scalars unchanged (they are still what is saved)");
            }
        }
        fs::remove_all(base);
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
