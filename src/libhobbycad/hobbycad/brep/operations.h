// =====================================================================
//  src/libhobbycad/hobbycad/brep/operations.h — 3D BREP operations
// =====================================================================
//
//  Functions for creating 3D shapes from 2D sketches and performing
//  shape operations.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BREP_OPERATIONS_H
#define HOBBYCAD_BREP_OPERATIONS_H

#include "../core.h"
#include "../sketch/profiles.h"

#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>
#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <QVector>
#include <QString>

namespace hobbycad {
namespace brep {

// =====================================================================
//  Operation Results
// =====================================================================

/// Result of a BREP operation
struct OperationResult {
    bool success = false;
    TopoDS_Shape shape;
    QString errorMessage;
};

// =====================================================================
//  Sketch to 3D Operations
// =====================================================================

/// Extrude a profile along a direction
/// @param profile Profile to extrude (from sketch::detectProfiles)
/// @param entities Entities for building the profile wire
/// @param direction Extrusion direction (unit vector)
/// @param distance Extrusion distance (positive = along direction)
/// @return Result with extruded solid
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult extrudeProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Dir& direction,
    double distance);

/// Extrude with symmetric option
/// @param profile Profile to extrude
/// @param entities Entities for building the profile wire
/// @param direction Extrusion direction
/// @param distance Total distance
/// @param symmetric If true, extrude distance/2 in both directions
/// @return Result with extruded solid
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult extrudeProfileSymmetric(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Dir& direction,
    double distance,
    bool symmetric = true);

/// Revolve a profile around an axis
/// @param profile Profile to revolve
/// @param entities Entities for building the profile wire
/// @param axis Revolution axis
/// @param angleDegrees Revolution angle in degrees (360 for full revolution)
/// @return Result with revolved solid
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult revolveProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Ax1& axis,
    double angleDegrees);

/// Sweep a profile along a path
/// @param profile Profile to sweep
/// @param entities Entities for building the profile wire
/// @param pathEntities Entities defining the sweep path
/// @return Result with swept solid
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult sweepProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const QVector<sketch::Entity>& pathEntities);

/// Loft between multiple profiles
/// @param profiles Profiles to loft between (in order)
/// @param entities All entities
/// @param solid If true, create solid; if false, create shell
/// @return Result with lofted shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult loftProfiles(
    const QVector<sketch::Profile>& profiles,
    const QVector<sketch::Entity>& entities,
    bool solid = true);

// =====================================================================
//  Boolean Operations
// =====================================================================

/// Fuse (union) two shapes
/// @param shape1 First shape
/// @param shape2 Second shape
/// @return Result with fused shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult fuseShapes(
    const TopoDS_Shape& shape1,
    const TopoDS_Shape& shape2);

/// Cut (difference) one shape from another
/// @param shape Main shape
/// @param tool Shape to subtract
/// @return Result with cut shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult cutShape(
    const TopoDS_Shape& shape,
    const TopoDS_Shape& tool);

/// Intersect two shapes
/// @param shape1 First shape
/// @param shape2 Second shape
/// @return Result with intersection
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult intersectShapes(
    const TopoDS_Shape& shape1,
    const TopoDS_Shape& shape2);

// =====================================================================
//  Shape Modification
// =====================================================================

/// Apply fillet to edges
/// @param shape Shape to fillet
/// @param radius Fillet radius
/// @param edgeIndices Indices of edges to fillet (empty = all edges)
/// @return Result with filleted shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult filletShape(
    const TopoDS_Shape& shape,
    double radius,
    const QVector<int>& edgeIndices = {});

/// Apply chamfer to edges
/// @param shape Shape to chamfer
/// @param distance Chamfer distance
/// @param edgeIndices Indices of edges to chamfer (empty = all edges)
/// @return Result with chamfered shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult chamferShape(
    const TopoDS_Shape& shape,
    double distance,
    const QVector<int>& edgeIndices = {});

/// Shell a solid (hollow it out)
/// @param shape Solid to shell
/// @param thickness Wall thickness
/// @param facesToRemove Indices of faces to remove (openings)
/// @return Result with shelled shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult shellShape(
    const TopoDS_Shape& shape,
    double thickness,
    const QVector<int>& facesToRemove = {});

/// Offset a shape
/// @param shape Shape to offset
/// @param distance Offset distance (positive = outward)
/// @return Result with offset shape
/// @note TODO: Not yet implemented
HOBBYCAD_EXPORT OperationResult offsetShape(
    const TopoDS_Shape& shape,
    double distance);

// =====================================================================
//  Shape Queries
// =====================================================================

/// Calculate volume of a solid shape
/// @param shape The shape
/// @return Volume in cubic mm (0 if not a solid)
HOBBYCAD_EXPORT double shapeVolume(const TopoDS_Shape& shape);

/// Calculate surface area of a shape
/// @param shape The shape
/// @return Surface area in square mm
HOBBYCAD_EXPORT double shapeSurfaceArea(const TopoDS_Shape& shape);

/// Get bounding box of a shape
/// @param shape The shape
/// @param minPt Output: minimum corner
/// @param maxPt Output: maximum corner
/// @return True if bounding box is valid
HOBBYCAD_EXPORT bool shapeBounds(
    const TopoDS_Shape& shape,
    gp_Pnt& minPt,
    gp_Pnt& maxPt);

/// Get center of mass of a shape
/// @param shape The shape
/// @return Center of mass point
HOBBYCAD_EXPORT gp_Pnt shapeCenterOfMass(const TopoDS_Shape& shape);

/// Get all faces of a shape
/// @param shape The shape
/// @return Vector of faces
HOBBYCAD_EXPORT QVector<TopoDS_Face> shapeFaces(const TopoDS_Shape& shape);

/// Count faces in a shape
HOBBYCAD_EXPORT int faceCount(const TopoDS_Shape& shape);

/// Count edges in a shape
HOBBYCAD_EXPORT int edgeCount(const TopoDS_Shape& shape);

/// Count vertices in a shape
HOBBYCAD_EXPORT int vertexCount(const TopoDS_Shape& shape);

}  // namespace brep
}  // namespace hobbycad

#endif  // HOBBYCAD_BREP_OPERATIONS_H
