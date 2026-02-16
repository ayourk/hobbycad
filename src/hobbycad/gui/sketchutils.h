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
