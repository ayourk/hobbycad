// =====================================================================
//  src/hobbycad/gui/sketchutils.h — Sketch utility functions for GUI
// =====================================================================
//
//  Conversion utilities between GUI sketch types and library types.
//  This bridges the SketchCanvas GUI structs with libhobbycad operations.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_GUI_SKETCHUTILS_H
#define HOBBYCAD_GUI_SKETCHUTILS_H

#include "sketchcanvas.h"

#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/profiles.h>
#include <hobbycad/sketch/patterns.h>
#include <hobbycad/sketch/operations.h>

namespace hobbycad {

// =====================================================================
//  Entity Type Conversion
// =====================================================================

/// Convert GUI entity type to library entity type
inline sketch::EntityType toLibraryEntityType(SketchEntityType guiType)
{
    switch (guiType) {
    case SketchEntityType::Point:      return sketch::EntityType::Point;
    case SketchEntityType::Line:       return sketch::EntityType::Line;
    case SketchEntityType::Rectangle:  return sketch::EntityType::Rectangle;
    case SketchEntityType::Circle:     return sketch::EntityType::Circle;
    case SketchEntityType::Arc:        return sketch::EntityType::Arc;
    case SketchEntityType::Spline:     return sketch::EntityType::Spline;
    case SketchEntityType::Polygon:    return sketch::EntityType::Polygon;
    case SketchEntityType::Slot:       return sketch::EntityType::Slot;
    case SketchEntityType::Ellipse:    return sketch::EntityType::Ellipse;
    case SketchEntityType::Text:       return sketch::EntityType::Text;
    default:                           return sketch::EntityType::Point;
    }
}

/// Convert library entity type to GUI entity type
inline SketchEntityType toGuiEntityType(sketch::EntityType libType)
{
    switch (libType) {
    case sketch::EntityType::Point:     return SketchEntityType::Point;
    case sketch::EntityType::Line:      return SketchEntityType::Line;
    case sketch::EntityType::Rectangle: return SketchEntityType::Rectangle;
    case sketch::EntityType::Circle:    return SketchEntityType::Circle;
    case sketch::EntityType::Arc:       return SketchEntityType::Arc;
    case sketch::EntityType::Spline:    return SketchEntityType::Spline;
    case sketch::EntityType::Polygon:   return SketchEntityType::Polygon;
    case sketch::EntityType::Slot:      return SketchEntityType::Slot;
    case sketch::EntityType::Ellipse:   return SketchEntityType::Ellipse;
    case sketch::EntityType::Text:      return SketchEntityType::Text;
    default:                            return SketchEntityType::Point;
    }
}

// =====================================================================
//  Entity Conversion
// =====================================================================

/// Convert GUI SketchEntity to library Entity
inline sketch::Entity toLibraryEntity(const SketchEntity& gui)
{
    sketch::Entity lib;
    lib.id = gui.id;
    lib.type = toLibraryEntityType(gui.type);
    lib.points = gui.points;
    lib.radius = gui.radius;
    lib.startAngle = gui.startAngle;
    lib.sweepAngle = gui.sweepAngle;
    lib.sides = gui.sides;
    lib.majorRadius = gui.majorRadius;
    lib.minorRadius = gui.minorRadius;
    lib.text = gui.text;
    lib.fontFamily = gui.fontFamily;
    lib.fontSize = gui.fontSize;
    lib.fontBold = gui.fontBold;
    lib.fontItalic = gui.fontItalic;
    lib.textRotation = gui.textRotation;
    lib.isConstruction = gui.isConstruction;
    lib.constrained = gui.constrained;
    return lib;
}

/// Convert library Entity to GUI SketchEntity
inline SketchEntity toGuiEntity(const sketch::Entity& lib)
{
    SketchEntity gui;
    gui.id = lib.id;
    gui.type = toGuiEntityType(lib.type);
    gui.points = lib.points;
    gui.radius = lib.radius;
    gui.startAngle = lib.startAngle;
    gui.sweepAngle = lib.sweepAngle;
    gui.sides = lib.sides;
    gui.majorRadius = lib.majorRadius;
    gui.minorRadius = lib.minorRadius;
    gui.text = lib.text;
    gui.fontFamily = lib.fontFamily;
    gui.fontSize = lib.fontSize;
    gui.fontBold = lib.fontBold;
    gui.fontItalic = lib.fontItalic;
    gui.textRotation = lib.textRotation;
    gui.isConstruction = lib.isConstruction;
    gui.constrained = lib.constrained;
    gui.selected = false;  // GUI-only state
    return gui;
}

/// Convert a vector of GUI entities to library entities
inline QVector<sketch::Entity> toLibraryEntities(const QVector<SketchEntity>& guiEntities)
{
    QVector<sketch::Entity> libEntities;
    libEntities.reserve(guiEntities.size());
    for (const SketchEntity& gui : guiEntities) {
        libEntities.append(toLibraryEntity(gui));
    }
    return libEntities;
}

/// Convert a vector of library entities to GUI entities
inline QVector<SketchEntity> toGuiEntities(const QVector<sketch::Entity>& libEntities)
{
    QVector<SketchEntity> guiEntities;
    guiEntities.reserve(libEntities.size());
    for (const sketch::Entity& lib : libEntities) {
        guiEntities.append(toGuiEntity(lib));
    }
    return guiEntities;
}

// =====================================================================
//  Profile Conversion
// =====================================================================

/// Convert library Profile to GUI SketchProfile
inline SketchProfile toGuiProfile(const sketch::Profile& lib)
{
    SketchProfile gui;
    gui.id = lib.id;
    gui.entityIds = lib.entityIds;
    gui.reversed = lib.reversed;
    gui.polygon = lib.polygon;
    gui.area = lib.area;
    gui.isOuter = lib.isOuter;
    return gui;
}

/// Convert a vector of library profiles to GUI profiles
inline QVector<SketchProfile> toGuiProfiles(const QVector<sketch::Profile>& libProfiles)
{
    QVector<SketchProfile> guiProfiles;
    guiProfiles.reserve(libProfiles.size());
    for (const sketch::Profile& lib : libProfiles) {
        guiProfiles.append(toGuiProfile(lib));
    }
    return guiProfiles;
}

// =====================================================================
//  Constraint Type Conversion (already same enum, but for completeness)
// =====================================================================

/// Both GUI and library use the same ConstraintType enum from project.h,
/// so no conversion is needed. These are here for API consistency.

inline sketch::ConstraintType toLibraryConstraintType(ConstraintType guiType)
{
    // The enums are identical - direct cast is safe
    return static_cast<sketch::ConstraintType>(static_cast<int>(guiType));
}

inline ConstraintType toGuiConstraintType(sketch::ConstraintType libType)
{
    return static_cast<ConstraintType>(static_cast<int>(libType));
}

// =====================================================================
//  Intersection Conversion
// =====================================================================

/// Convert library Intersection to GUI Intersection
inline SketchCanvas::Intersection toGuiIntersection(const sketch::Intersection& lib)
{
    return {lib.entityId1, lib.entityId2, lib.point, lib.param1, lib.param2};
}

/// Convert a vector of library intersections to GUI intersections
inline QVector<SketchCanvas::Intersection> toGuiIntersections(
    const QVector<sketch::Intersection>& libIntersections)
{
    QVector<SketchCanvas::Intersection> guiIntersections;
    guiIntersections.reserve(libIntersections.size());
    for (const sketch::Intersection& lib : libIntersections) {
        guiIntersections.append(toGuiIntersection(lib));
    }
    return guiIntersections;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_SKETCHUTILS_H
