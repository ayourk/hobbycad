// =====================================================================
//  src/hobbycad/gui/sketchutils.h — Sketch utility functions for GUI
// =====================================================================
//
//  Conversion utilities between GUI sketch types and library types.
//  This bridges the SketchCanvas GUI structs with libhobbycad operations.
//
//  Since SketchEntity now inherits from sketch::Entity, most conversions
//  are trivial. This file provides helpers for batch conversions and
//  backwards compatibility.
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
//  Entity Type Conversion (now identity - types are aliased)
// =====================================================================

/// Convert GUI entity type to library entity type (identity - same type)
inline sketch::EntityType toLibraryEntityType(SketchEntityType guiType)
{
    return guiType;  // SketchEntityType is aliased to sketch::EntityType
}

/// Convert library entity type to GUI entity type (identity - same type)
inline SketchEntityType toGuiEntityType(sketch::EntityType libType)
{
    return libType;  // SketchEntityType is aliased to sketch::EntityType
}

// =====================================================================
//  Entity Conversion
// =====================================================================

/// Convert GUI SketchEntity to library Entity
/// Since SketchEntity inherits from sketch::Entity, this is a simple slice
inline const sketch::Entity& toLibraryEntity(const SketchEntity& gui)
{
    return static_cast<const sketch::Entity&>(gui);
}

/// Convert library Entity to GUI SketchEntity
inline SketchEntity toGuiEntity(const sketch::Entity& lib)
{
    return SketchEntity(lib);  // Uses SketchEntity(const Entity&) constructor
}

/// Convert a vector of GUI entities to library entities
inline std::vector<sketch::Entity> toLibraryEntities(const QVector<SketchEntity>& guiEntities)
{
    std::vector<sketch::Entity> libEntities;
    libEntities.reserve(static_cast<size_t>(guiEntities.size()));
    for (const SketchEntity& gui : guiEntities) {
        libEntities.push_back(toLibraryEntity(gui));
    }
    return libEntities;
}

/// Convert a vector of library entities to GUI entities
inline QVector<SketchEntity> toGuiEntities(const std::vector<sketch::Entity>& libEntities)
{
    QVector<SketchEntity> guiEntities;
    guiEntities.reserve(static_cast<int>(libEntities.size()));
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
    gui.entityIds = QVector<int>(lib.entityIds.begin(), lib.entityIds.end());
    gui.reversed = QVector<bool>(lib.reversed.begin(), lib.reversed.end());
    // Convert std::vector<Point2D> to QPolygonF
    gui.polygon.reserve(static_cast<int>(lib.polygon.size()));
    for (const auto& pt : lib.polygon) {
        gui.polygon.append(QPointF(pt.x, pt.y));
    }
    gui.area = lib.area;
    gui.isOuter = lib.isOuter;
    return gui;
}

/// Convert a vector of library profiles to GUI profiles
inline QVector<SketchProfile> toGuiProfiles(const std::vector<sketch::Profile>& libProfiles)
{
    QVector<SketchProfile> guiProfiles;
    guiProfiles.reserve(static_cast<int>(libProfiles.size()));
    for (const sketch::Profile& lib : libProfiles) {
        guiProfiles.append(toGuiProfile(lib));
    }
    return guiProfiles;
}

// =====================================================================
//  Constraint Conversion
// =====================================================================

/// ConstraintType is now a using alias for sketch::ConstraintType
/// (unified in project.h), so no type conversion is needed.

/// Convert GUI SketchConstraint to library Constraint
/// Since SketchConstraint inherits from sketch::Constraint, this is a simple slice
inline const sketch::Constraint& toLibraryConstraint(const SketchConstraint& gui)
{
    return static_cast<const sketch::Constraint&>(gui);
}

/// Convert library Constraint to GUI SketchConstraint
inline SketchConstraint toGuiConstraint(const sketch::Constraint& lib)
{
    return SketchConstraint(lib);
}

/// Convert a vector of GUI constraints to library constraints
inline std::vector<sketch::Constraint> toLibraryConstraints(const QVector<SketchConstraint>& guiConstraints)
{
    std::vector<sketch::Constraint> libConstraints;
    libConstraints.reserve(static_cast<size_t>(guiConstraints.size()));
    for (const SketchConstraint& gui : guiConstraints) {
        libConstraints.push_back(toLibraryConstraint(gui));
    }
    return libConstraints;
}

// =====================================================================
//  Intersection Conversion
// =====================================================================

/// Convert library Intersection to GUI Intersection
inline SketchCanvas::Intersection toGuiIntersection(const sketch::Intersection& lib)
{
    return {lib.entityId1, lib.entityId2, QPointF(lib.point.x, lib.point.y), lib.param1, lib.param2};
}

/// Convert a vector of library intersections to GUI intersections
inline QVector<SketchCanvas::Intersection> toGuiIntersections(
    const std::vector<sketch::Intersection>& libIntersections)
{
    QVector<SketchCanvas::Intersection> guiIntersections;
    guiIntersections.reserve(static_cast<int>(libIntersections.size()));
    for (const sketch::Intersection& lib : libIntersections) {
        guiIntersections.append(toGuiIntersection(lib));
    }
    return guiIntersections;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_GUI_SKETCHUTILS_H
