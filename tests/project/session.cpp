// =====================================================================
//  tests/project/session.cpp — ProjectSession, the one backend every
//  front end edits a project through.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/project_session.h>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace hobbycad;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static sketch::Entity line(int id, double x1, double y1, double x2, double y2) {
    sketch::Entity e;
    e.id = id;
    e.type = sketch::EntityType::Line;
    e.points = {Point3{x1, y1, 0}, Point3{x2, y2, 0}};
    return e;
}

static SketchDraft square(ProjectSession& s, SketchPlane plane = SketchPlane::XY) {
    SketchDraft d = s.beginSketch(plane);
    d.sketch.entities = {line(1, 0, 0, 10, 0), line(2, 10, 0, 10, 10),
                         line(3, 10, 10, 0, 10), line(4, 0, 10, 0, 0)};
    return d;
}

static double volumeOf(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

int main(int argc, char** argv) {
    std::printf("project session\n");

    {
        Project p;
        ProjectSession s(p);
        SketchDraft d = square(s);
        check(d.isNew() && d.sketch.name == "Sketch1", "a new draft is named Sketch1");
        check(p.sketches().empty(), "beginning a sketch stores nothing");
        const int id = s.finishSketch(d);
        check(id > 0 && p.sketches().size() == 1 && p.features().size() == 1
              && p.features()[0].type == FeatureType::Sketch && p.features()[0].id == id,
              "finishing stores the sketch and its feature record");
        const auto tl = s.timeline();
        check(tl.size() == 1 && tl[0].featureId == id && tl[0].name == "Sketch1", "the timeline lists it once");
        check(s.undo() && p.sketches().empty() && p.features().empty(), "one undo removes both");
        check(s.redo() && p.sketches().size() == 1 && p.features().size() == 1, "redo puts both back");

        FeatureData ex;
        ex.type = FeatureType::Extrude;
        ex.name = "E";
        const int exId = s.addFeature(ex);
        check(exId > id, "a feature id is past the sketch's");
        SketchDraft d2 = s.beginSketch(SketchPlane::XZ);
        check(d2.sketch.name == "Sketch2" && s.finishSketch(d2) > exId, "the next sketch id is past every feature");

        SketchDraft named = s.beginSketch(SketchPlane::XY, 0.0, PlaneRotationAxis::X, 0.0, -1, "Sketch4");
        s.finishSketch(named);
        check(s.nextSketchName() == "Sketch5", "a default name skips one already taken");
    }

    {
        // A sketch with a record is listed once; one without is adopted.
        Project p;
        SketchData a; a.id = 3; a.name = "A"; p.addSketch(a);
        SketchData b; b.id = 5; b.name = "B"; p.addSketch(b);
        FeatureData fa; fa.id = 3; fa.type = FeatureType::Sketch; fa.name = "A";
        p.setFeatures({fa});
        p.setModified(false);
        ProjectSession s(p);
        check(s.timeline().size() == 2, "a recorded sketch and an orphan are each listed once");
        s.adoptOrphanSketches();
        check(p.features().size() == 2 && !p.isModified(),
              "adopting gives the orphan a record without marking the project edited");
    }

    {
        // A sketch with no record of its own still owns its id.
        Project p;
        SketchData lone; lone.id = 9; lone.name = "Lone"; p.addSketch(lone);
        ProjectSession s(p);
        FeatureData next;
        next.type = FeatureType::Extrude;
        next.name = "E";
        check(s.addFeature(next) > 9, "a new feature id is past a sketch that has no record of its own");
    }

    {
        Project p;
        ProjectSession s(p);
        const int id = s.finishSketch(square(s));
        check(s.renameFeature(id, "Base") && p.sketches()[0].name == "Base" && p.features()[0].name == "Base",
              "rename changes the sketch and its record");
        check(s.undo() && p.sketches()[0].name == "Sketch1" && p.features()[0].name == "Sketch1", "and undoes as one");
        check(s.setFeatureSuppressed(id, true) && p.features()[0].suppressed && s.timeline()[0].suppressed, "suppress");
        check(s.undo() && !p.features()[0].suppressed, "suppression undoes");

        SketchDraft edit;
        check(s.beginEditSketch(id, edit) && !edit.isNew(), "a stored sketch opens for editing");
        const int depth = s.history().undoLevels();
        check(s.finishSketch(edit) == id && s.history().undoLevels() == depth, "finishing an unchanged edit records nothing");
        edit.sketch.entities[0].points[1].x = 12;
        check(s.finishSketch(edit) == id && p.sketches()[0].entities[0].points[1].x == 12
              && s.history().undoLevels() == depth + 1, "moving a point is an edit");
        check(s.undo() && p.sketches()[0].entities[0].points[1].x == 10, "and undoes");

        check(s.deleteFeature(id) && p.sketches().empty() && p.features().empty(),
              "delete removes the sketch and its record");
        check(s.undo() && p.sketches().size() == 1 && p.features().size() == 1
              && p.sketches()[0].entities.size() == 4, "undo restores both, contents and all");
    }

    {
        Project p;
        ProjectSession s(p);
        const int a = s.finishSketch(square(s));
        const int b = s.finishSketch(square(s));
        const int c = s.finishSketch(square(s));
        check(s.moveFeature(c, 0), "move the last feature to the front");
        auto tl = s.timeline();
        check(tl.size() == 3 && tl[0].featureId == c && tl[1].featureId == a && tl[2].featureId == b,
              "the timeline follows");
        check(s.undo() && s.timeline()[2].featureId == c, "and the move undoes");
    }

    {
        // A Save mid-sketch writes the draft without storing it.
        const fs::path root = fs::path(argc > 1 ? argv[1] : "session.data");
        fs::remove_all(root);
        fs::create_directories(root);
        const std::string dir = (root / "proj").string();
        Project p;
        ProjectSession s(p);
        s.finishSketch(square(s));
        SketchDraft open = square(s);
        std::string err;
        check(s.save(dir, &err, &open), "save with a draft open");
        check(p.sketches().size() == 1 && !p.isModified(), "saving does not store the draft");
        Project q;
        check(q.load(dir, &err) && q.sketches().size() == 2, "but the file has it");
        fs::remove_all(root);
    }

    {
        // Model operations need no viewport.
        Project p;
        ProjectSession s(p);
        const int id = s.finishSketch(square(s));
        ModelResult r = s.extrudeSketch(id, 5, ExtrudeExtent::Normal, BodyOperation::NewBody);
        check(r.ok && p.bodies().size() == 1 && std::fabs(volumeOf(p.bodies()[0].shape) - 500) < 1e-6,
              "extrude makes a 10 x 10 x 5 body");
        const auto tl = s.timeline();
        check(tl.size() == 2 && tl[1].type == FeatureType::Extrude && tl[1].dependsOn == std::vector<int>{id},
              "the extrude is on the timeline, depending on its sketch");
        ModelResult join = s.extrudeSketch(id, 8, ExtrudeExtent::Normal, BodyOperation::Join);
        check(join.ok && p.bodies().size() == 1 && std::fabs(volumeOf(p.bodies()[0].shape) - 800) < 1e-6,
              "join fuses into the one body");
        check(s.undo() && std::fabs(volumeOf(p.bodies()[0].shape) - 500) < 1e-6, "undoing the join restores the body");
        check(s.undo() && p.bodies().empty() && s.timeline().size() == 1, "undoing the extrude removes body and feature");

        const int xz = s.finishSketch(square(s, SketchPlane::XZ));
        ModelResult rx = s.extrudeSketch(xz, 5, ExtrudeExtent::Normal, BodyOperation::NewBody);
        Bnd_Box box;
        if (rx.ok) BRepBndLib::Add(p.bodies().back().shape, box);
        double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
        if (rx.ok) box.Get(x0, y0, z0, x1, y1, z1);
        check(rx.ok && std::fabs((z1 - z0) - 10) < 0.01 && std::fabs((y1 - y0) - 5) < 0.01,
              "a sketch on XZ extrudes from XZ, not from XY");

        // Not a square: about either axis a square gives the same cylinder, so
        // a revolve about the wrong axis passed.
        SketchDraft band = s.beginSketch(SketchPlane::XY);
        band.sketch.entities = {line(1, 0, 0, 10, 0), line(2, 10, 0, 10, 5),
                                line(3, 10, 5, 0, 5), line(4, 0, 5, 0, 0)};
        const int ring = s.finishSketch(band);
        ModelResult rv = s.revolveSketch(ring, 360, RevolveAxisKind::SketchYAxis, -1, BodyOperation::NewBody);
        const double aboutY = 3.14159265358979 * 100 * 5;   // radius 10 (x), height 5 (y)
        check(rv.ok && std::fabs(volumeOf(p.bodies().back().shape) - aboutY) < aboutY * 1e-4,
              "revolving a 10 x 5 rectangle about its y axis gives radius 10, height 5");
        // Negative, not zero: OCCT refuses a zero-length prism by itself, so a
        // zero check passed with the session's own guard removed.
        check(!s.extrudeSketch(ring, -5, ExtrudeExtent::Normal, BodyOperation::NewBody).ok,
              "a negative distance is refused");
    }

    std::printf("%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
