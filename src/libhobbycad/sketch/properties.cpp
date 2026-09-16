// =====================================================================
//  src/libhobbycad/sketch/properties.cpp — editing an entity property by name
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#include <hobbycad/sketch/properties.h>
#include <hobbycad/geometry/utils.h>

#include <cmath>

namespace hobbycad {
namespace sketch {

namespace {

const char* const kNumericProperties[] = {
    "radius", "diameter", "startAngle", "sweepAngle", "length", "width", "height",
    "sides", "majorRadius", "minorRadius", "fontSize", "textRotation",
};

PropertyEdit refused(PropertyProblem why)
{
    PropertyEdit edit;
    edit.problem = why;
    return edit;
}

PropertyEdit done()
{
    PropertyEdit edit;
    edit.changed = true;
    return edit;
}

}  // namespace

bool isNumericEntityProperty(const std::string& property)
{
    for (const char* name : kNumericProperties) if (property == name) return true;
    return false;
}

PropertyEdit setEntityNumber(Entity& e, const std::string& property, double value)
{
    if (!isNumericEntityProperty(property)) return refused(PropertyProblem::UnknownProperty);
    if (!std::isfinite(value)) return refused(PropertyProblem::NotANumber);

    if (property == "radius") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        e.radius = value;
        if (e.type == EntityType::Arc) resyncArcEndpoints(e);
        else if (e.type == EntityType::Circle) rescaleCircleToRadius(e, value);
        return done();
    }
    if (property == "diameter") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        const double r = value / 2.0;
        e.radius = r;
        if (e.type == EntityType::Circle) rescaleCircleToRadius(e, r);
        return done();
    }
    if (property == "startAngle") {
        e.startAngle = value;
        resyncArcEndpoints(e);
        return done();
    }
    if (property == "sweepAngle") {
        e.sweepAngle = value;
        resyncArcEndpoints(e);
        return done();
    }
    if (property == "length") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        if (e.points.size() < 2) return refused(PropertyProblem::Degenerate);
        const Point2D p0(e.points[0]);
        const Point2D dir = Point2D(e.points[1]) - p0;
        const double cur = geometry::length(dir);
        if (cur <= geometry::kDegenerateLen) return refused(PropertyProblem::Degenerate);
        const Point2D p1 = p0 + dir * (value / cur);
        e.points[1] = Point3{p1.x, p1.y, 0.0};
        return done();
    }
    if (property == "width" || property == "height") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        if (e.points.size() < 2) return refused(PropertyProblem::Degenerate);
        if (property == "width") {
            const double sign = (e.points[1].x >= e.points[0].x) ? 1.0 : -1.0;
            e.points[1].x = e.points[0].x + sign * value;
        } else {
            const double sign = (e.points[1].y >= e.points[0].y) ? 1.0 : -1.0;
            e.points[1].y = e.points[0].y + sign * value;
        }
        return done();
    }
    if (property == "sides") {
        const int sides = static_cast<int>(value);
        if (sides < 3 || sides > 100) return refused(PropertyProblem::OutOfRange);
        e.sides = sides;
        return done();
    }
    if (property == "majorRadius" || property == "minorRadius") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        (property == "majorRadius" ? e.majorRadius : e.minorRadius) = value;
        return done();
    }
    if (property == "fontSize") {
        if (!geometry::isPositiveLength(value)) return refused(PropertyProblem::NotPositive);
        e.fontSize = value;
        resyncTextHandle(e);
        return done();
    }
    // textRotation
    e.textRotation = value;
    resyncTextHandle(e);
    return done();
}

PropertyEdit setEntityPoint(Entity& e, int index, const Point2D& p)
{
    if (index < 0 || index >= static_cast<int>(e.points.size())) return refused(PropertyProblem::NoSuchPoint);
    e.points[static_cast<size_t>(index)] = Point3{p.x, p.y, 0.0};
    resyncTextHandle(e);
    PropertyEdit edit = done();
    edit.editedPointIndex = index;
    return edit;
}

PropertyEdit setEntityText(Entity& e, const std::string& text)
{
    e.text = text;
    resyncTextHandle(e);
    return done();
}

}  // namespace sketch
}  // namespace hobbycad
