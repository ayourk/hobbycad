// =====================================================================
//  tests/project/property_schema.cpp — what a properties sheet shows
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The main window's properties tree listed each entity type's fields by
//  hand, and the sketch panel named points from a list of its own. Both
//  now read sketch/property_schema.h. Two edits the hand lists got wrong
//  are held here too: a slot's Width row wrote the typed width into the
//  radius (a slot typed 10 wide came out 20), and an ellipse radius typed
//  into the tree changed the number but not the axis points the solver
//  reads, so the next solve could put the old size back.
// =====================================================================
#include <hobbycad/geometry/utils.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/properties.h>
#include <hobbycad/sketch/property_schema.h>
#include <hobbycad/sketch/undo.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static bool near(double a, double b)
{
    return std::fabs(a - b) < 1e-9;
}

/// Apply a field's own value back through the setter named by its key.
static bool roundTrips(Entity e, const PropertyField& f)
{
    PropertyEdit edit;
    if (f.kind == FieldKind::Point) {
        edit = setEntityPoint(e, f.pointIndex, Point2D(e.points[f.pointIndex]));
    } else if (f.kind == FieldKind::Text) {
        edit = setEntityText(e, e.text);
    } else {
        edit = setEntityNumber(e, f.key, fieldNumber(e, f));
    }
    return edit.problem == PropertyProblem::None && edit.changed;
}

static std::vector<Entity> samples()
{
    Entity conic = createBezierSpline(
        9, {Point2D(0, 0), Point2D(1, 1), Point2D(2, 1), Point2D(3, 0)});
    conic.conicRho = 0.5;
    return {
        createPoint(1, Point2D(1, 2)),
        createLine(2, Point2D(0, 0), Point2D(3, 4)),
        createRectangle(3, Point2D(0, 0), Point2D(10, 5)),
        createCircle(4, Point2D(1, 1), 2.0),
        createArc(5, Point2D(0, 0), 3.0, 10.0, 90.0),
        createPolygon(6, Point2D(0, 0), 5.0, 6),
        createSlot(7, Point2D(0, 0), Point2D(10, 0), 2.0),
        createEllipse(8, Point2D(0, 0), 5.0, 3.0, 30.0),
        conic,
        createText(10, Point2D(0, 0), "Hi", {}, 12.0),
    };
}

int main()
{
    std::printf("property schema\n");

    // ---- every field works through its setter -------------------------------
    {
        bool allTrip = true, labeled = true, pointsNamed = true;
        for (const Entity& e : samples()) {
            const std::vector<FieldLabel> names = pointLabels(e);
            pointsNamed = pointsNamed && names.size() == e.points.size();
            for (const PropertyField& f : entityGeometryFields(e)) {
                labeled = labeled && f.label.source && *f.label.source;
                if (!f.editable) continue;
                const bool ok = roundTrips(e, f);
                if (!ok) {
                    std::printf("         %s: %s\n", entityTypeName(e.type), f.key.c_str());
                }
                allTrip = allTrip && ok;
            }
        }
        check(allTrip, "every editable field is accepted by the setter it names");
        check(labeled, "every field has a label");
        check(pointsNamed, "every point has a name");
    }

    // ---- what the sheets show ------------------------------------------------
    {
        const Entity line = createLine(1, Point2D(0, 0), Point2D(3, 4));
        const std::vector<PropertyField> f = entityGeometryFields(line);
        check(f.size() == 3 && f[0].key == "point0"
                  && std::strcmp(f[0].label.source, "Start") == 0
                  && f[2].key == "length" && near(fieldNumber(line, f[2]), 5.0),
              "a line shows its ends and its length");

        const Entity rect = createRectangle(2, Point2D(0, 0), Point2D(10, 5));
        const std::vector<PropertyField> r = entityGeometryFields(rect);
        check(!r.empty() && !r[0].editable
                  && std::strcmp(r[0].label.source, "Corner %1") == 0
                  && r[0].label.number == 1,
              "a rectangle's corners are numbered and read-only");

        const Entity arc = createArc(3, Point2D(0, 0), 3.0, 10.0, 90.0);
        const std::vector<PropertyField> a = entityGeometryFields(arc);
        check(a.size() == 4 && !a[0].editable && a[2].kind == FieldKind::Angle,
              "an arc's center is read-only and its angles are angles");

        const Entity slot = createSlot(4, Point2D(0, 0), Point2D(10, 0), 2.0);
        const std::vector<PropertyField> s = entityGeometryFields(slot);
        check(s.size() == 4 && s[2].key.empty() && !s[2].editable
                  && near(fieldNumber(slot, s[2]), 10.0) && s[3].key == "width"
                  && near(fieldNumber(slot, s[3]), 4.0),
              "a slot shows its length as a readout and its width as twice the radius");

        Entity conic = createBezierSpline(
            5, {Point2D(0, 0), Point2D(1, 1), Point2D(2, 1), Point2D(3, 0)});
        conic.conicRho = 0.5;
        const std::vector<PropertyField> c = entityGeometryFields(conic);
        check(c.size() == 5 && c[1].editable && !c[2].editable && !c[3].editable
                  && c[4].editable
                  && std::strcmp(c[2].label.source, "Handle (derived)") == 0,
              "a conic's derived handles are named and not editable");

        const Entity ellipse = createEllipse(6, Point2D(0, 0), 5.0, 3.0);
        const std::vector<FieldLabel> names = pointLabels(ellipse);
        check(names.size() == 3 && std::strcmp(names[1].source, "Major axis") == 0,
              "an ellipse's axis points are named");

        const Entity spline = createBezierSpline(
            7, {Point2D(0, 0), Point2D(1, 1), Point2D(2, 1), Point2D(3, 0)});
        const std::vector<FieldLabel> pn = pointLabels(spline);
        bool numbered = pn.size() == 4;
        for (std::size_t i = 0; i < pn.size(); ++i) {
            numbered = numbered && std::strcmp(pn[i].source, "Point %1") == 0
                       && pn[i].number == static_cast<int>(i) + 1;
        }
        check(numbered, "points with no role are numbered from 1");
    }

    // ---- locks -----------------------------------------------------------------
    {
        Entity ellipse = createEllipse(6, Point2D(0, 0), 5.0, 3.0);
        const std::vector<PropertyField> f = entityGeometryFields(ellipse);
        const PropertyField& major = f[1];
        const PropertyField& minor = f[2];
        std::vector<Constraint> none;
        check(!fieldLocked(ellipse, major, none), "an ellipse radius is editable");

        Constraint d;
        d.type = ConstraintType::Distance;
        d.entityIds = {6, 6};
        d.pointIndices = {0, 1};
        const std::vector<Constraint> driven{d};
        check(fieldLocked(ellipse, major, driven) && !fieldLocked(ellipse, minor, driven),
              "a dimension on the major axis point locks the major radius only");

        ellipse.projectionSourceId = 3;
        check(fieldLocked(ellipse, minor, none) && fieldLocked(ellipse, f[0], none),
              "a projected entity is locked everywhere");
    }

    // ---- the setter fixes --------------------------------------------------------
    {
        Entity slot = createSlot(1, Point2D(0, 0), Point2D(10, 0), 2.0);
        const PropertyEdit edit = setEntityNumber(slot, "width", 10.0);
        check(edit.problem == PropertyProblem::None && near(slot.radius, 5.0)
                  && near(slot.points[1].x, 10.0),
              "a slot typed 10 wide is 10 wide and keeps its centers");

        Entity ellipse = createEllipse(2, Point2D(1, 1), 5.0, 3.0);
        ensureEllipseAxisPoints(ellipse);
        PropertyEdit m = setEntityNumber(ellipse, "majorRadius", 8.0);
        const double dMajor = geometry::length(Point2D(ellipse.points[1]) - Point2D(1, 1));
        check(m.problem == PropertyProblem::None && m.editedPointIndex == 1
                  && near(dMajor, 8.0),
              "a typed major radius moves the major axis point");
        m = setEntityNumber(ellipse, "minorRadius", 2.0);
        const double dMinor = geometry::length(Point2D(ellipse.points[2]) - Point2D(1, 1));
        check(m.editedPointIndex == 2 && near(dMinor, 2.0),
              "and a typed minor radius the minor one");
        m = setEntityNumber(ellipse, "ellipseRotation", 90.0);
        check(m.editedPointIndex == 1 && near(ellipse.points[1].x, 1.0)
                  && near(ellipse.points[1].y, 9.0),
              "a typed rotation turns the axis points");
        check(setEntityNumber(ellipse, "majorRadius", -1.0).problem
                  == PropertyProblem::NotPositive,
              "a negative radius is refused");

        Entity arc = createArc(3, Point2D(0, 0), 3.0, 0.0, 90.0);
        setEntityNumber(arc, "diameter", 10.0);
        check(near(arc.radius, 5.0) && near(arc.points[1].x, 5.0),
              "a typed arc diameter moves the arc's ends");
    }

    // ---- names --------------------------------------------------------------------
    check(std::strcmp(entityTypeName(EntityType::Ellipse), "Ellipse") == 0
              && std::strcmp(entityTypeContext(), "hobbycad::SketchEntity") == 0,
          "entity type names are English with their own translation context");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
