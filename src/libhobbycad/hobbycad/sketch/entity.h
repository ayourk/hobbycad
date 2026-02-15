// =====================================================================
//  src/libhobbycad/hobbycad/sketch/entity.h — Sketch entity types
// =====================================================================
//
//  Unified sketch entity representation used by both the library and GUI.
//  The GUI can extend these with view-specific state.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_ENTITY_H
#define HOBBYCAD_SKETCH_ENTITY_H

#include "../core.h"
#include "../geometry/types.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  Entity Types
// =====================================================================

/// Types of sketch entities
enum class EntityType {
    Point,       ///< Single point
    Line,        ///< Line segment (2 endpoints)
    Rectangle,   ///< Axis-aligned rectangle (2 corner points)
    Circle,      ///< Circle (center + radius)
    Arc,         ///< Arc (center + radius + angles)
    Spline,      ///< Catmull-Rom spline (control points)
    Polygon,     ///< Regular polygon (center + radius + sides)
    Slot,        ///< Obround/stadium slot (2 centers + radius)
    Ellipse,     ///< Ellipse (center + major/minor radii)
    Text         ///< Text annotation
};

// =====================================================================
//  Sketch Entity
// =====================================================================

/// A single sketch entity (point, line, circle, etc.)
///
/// This is the core data structure for sketch geometry. The GUI extends
/// this with selection state and other view-specific properties.
struct HOBBYCAD_EXPORT Entity {
    int id = 0;                           ///< Unique ID within the sketch
    EntityType type = EntityType::Line;   ///< Entity type

    // Geometry data (interpretation depends on type)
    QVector<QPointF> points;              ///< Control/definition points
    double radius = 0.0;                  ///< For circles, arcs, slots, polygons
    double startAngle = 0.0;              ///< For arcs (degrees)
    double sweepAngle = 360.0;            ///< For arcs (degrees)
    int sides = 6;                        ///< For polygons
    double majorRadius = 0.0;             ///< For ellipses
    double minorRadius = 0.0;             ///< For ellipses
    QString text;                         ///< For text entities
    QString fontFamily;                   ///< Font family (empty = default)
    double fontSize = 12.0;               ///< Font size in mm
    bool fontBold = false;                ///< Bold text
    bool fontItalic = false;              ///< Italic text
    double textRotation = 0.0;            ///< Text rotation in degrees

    // State
    bool isConstruction = false;          ///< Construction geometry flag
    bool constrained = false;             ///< Has constraints applied

    // ---- Convenience Methods ----

    /// Get the bounding box of the entity
    geometry::BoundingBox boundingBox() const;

    /// Get endpoints for entities that have them (lines, arcs)
    /// Returns empty vector for other types
    QVector<QPointF> endpoints() const;

    /// Check if a point is on this entity within tolerance
    bool containsPoint(const QPointF& point, double tolerance = 0.5) const;

    /// Get the closest point on this entity to a given point
    QPointF closestPoint(const QPointF& point) const;

    /// Get distance from a point to this entity
    double distanceTo(const QPointF& point) const;

    /// Transform the entity by a 2D transformation
    void transform(const geometry::Transform2D& t);

    /// Create a transformed copy
    Entity transformed(const geometry::Transform2D& t) const;

    /// Clone the entity with a new ID
    Entity clone(int newId) const;
};

// =====================================================================
//  Entity Factory Functions
// =====================================================================

/// Create a point entity
HOBBYCAD_EXPORT Entity createPoint(int id, const QPointF& position);

/// Create a line entity
HOBBYCAD_EXPORT Entity createLine(int id, const QPointF& start, const QPointF& end);

/// Create a rectangle entity
HOBBYCAD_EXPORT Entity createRectangle(int id, const QPointF& corner1, const QPointF& corner2);

/// Create a circle entity
HOBBYCAD_EXPORT Entity createCircle(int id, const QPointF& center, double radius);

/// Create an arc entity (from center, radius, angles)
HOBBYCAD_EXPORT Entity createArc(int id, const QPointF& center, double radius,
                                  double startAngle, double sweepAngle);

/// Create an arc entity from three points
HOBBYCAD_EXPORT Entity createArcFromThreePoints(int id, const QPointF& start,
                                                  const QPointF& mid, const QPointF& end);

/// Create a spline entity
HOBBYCAD_EXPORT Entity createSpline(int id, const QVector<QPointF>& controlPoints);

/// Create a polygon entity
HOBBYCAD_EXPORT Entity createPolygon(int id, const QPointF& center, double radius, int sides);

/// Create a slot entity
HOBBYCAD_EXPORT Entity createSlot(int id, const QPointF& center1, const QPointF& center2, double radius);

/// Create an ellipse entity
HOBBYCAD_EXPORT Entity createEllipse(int id, const QPointF& center, double majorRadius, double minorRadius);

/// Create a text entity
HOBBYCAD_EXPORT Entity createText(int id, const QPointF& position, const QString& text,
                                   const QString& fontFamily = QString(),
                                   double fontSize = 12.0, bool bold = false,
                                   bool italic = false, double rotation = 0.0);

// =====================================================================
//  Entity Query Functions
// =====================================================================

/// Check if two entities are connected (share a common endpoint)
HOBBYCAD_EXPORT bool entitiesConnected(const Entity& e1, const Entity& e2,
                                        double tolerance = geometry::POINT_TOLERANCE);

/// Get the connection point between two entities (if connected)
HOBBYCAD_EXPORT std::optional<QPointF> connectionPoint(const Entity& e1, const Entity& e2,
                                                        double tolerance = geometry::POINT_TOLERANCE);

/// Check if entity intersects a rectangle
HOBBYCAD_EXPORT bool entityIntersectsRect(const Entity& entity, const QRectF& rect);

/// Check if entity is fully enclosed by a rectangle
HOBBYCAD_EXPORT bool entityEnclosedByRect(const Entity& entity, const QRectF& rect);

/// Find the index of the nearest control point in the entity's points vector
/// @param entity The entity to search
/// @param point The reference point
/// @return Index of nearest point, or -1 if entity has no points
HOBBYCAD_EXPORT int nearestPointIndex(const Entity& entity, const QPointF& point);

/// Get the angle of a line entity in degrees (0-360)
/// @param entity The entity (must be a line)
/// @return Angle in degrees, or 0.0 if not a line
HOBBYCAD_EXPORT double getEntityAngle(const Entity& entity);

/// Get all control/definition points of an entity as a polygon
/// For complex entities (arcs, circles, ellipses), returns approximated points
/// @param entity The entity
/// @param segments Number of segments for curved entities (default 32)
/// @return Vector of points representing the entity
HOBBYCAD_EXPORT QVector<QPointF> entityToPolygon(const Entity& entity, int segments = 32);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_ENTITY_H
