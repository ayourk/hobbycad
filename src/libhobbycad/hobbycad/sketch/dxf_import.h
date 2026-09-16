// =====================================================================
//  src/libhobbycad/hobbycad/sketch/dxf_import.h — DXF import (parser)
// =====================================================================
//
//  A purpose-built, spec-aware ASCII-DXF reader for 2D sketch geometry.
//  Replaces the ad-hoc line-pair scanner that used to live in export.cpp.
//
//  What it does that the old scanner did not:
//   - a robust group-code reader with typed, NON-THROWING accessors: one
//     malformed value is a warning, never a crash;
//   - a HEADER pass, so drawing units ($INSUNITS) are honored;
//   - OCS: the entity extrusion vector (210/220/230) is applied through the
//     AutoCAD Arbitrary Axis Algorithm, so plane-placed geometry lands
//     correctly, including the classic negative-Z-extrusion X mirror;
//   - old-style POLYLINE as well as LWPOLYLINE;
//   - a warnings list, so the many places the format is under-specified or a
//     construct is unsupported are REPORTED rather than silently dropped.
//
//  ASCII DXF only (R12..R2018+). Binary DXF is rejected with a clear error.
//  BLOCKS/INSERT ARE expanded: the BLOCKS table is read, then each INSERT is
//  instantiated (translation, scale, rotation, MINSERT arrays and nested block
//  references), rebuilding the block geometry under the instance transform so
//  arcs/circles keep correct radii and angles. A nesting-depth limit and a
//  total-entity cap bound a hostile file (a huge MINSERT array, a block cycle).
//  The full TABLES/layer table is still deferred; layers are tracked/filterable.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_DXF_IMPORT_H
#define HOBBYCAD_SKETCH_DXF_IMPORT_H

#include "entity.h"
#include "../core.h"
#include "../types.h"
#include "../geometry/types.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

/// Options for DXF import.
struct DXFImportOptions {
    double scale = 1.0;                       ///< Extra scale on every coordinate (applied AFTER unit conversion)
    Point2D offset;                           ///< Offset added to every point (in mm, after scaling)
    bool importBlocks = true;                 ///< Expand INSERT references (BLOCKS/INSERT); false = record names only
    bool importHatch = false;                 ///< Reserved (HATCH not yet decoded)
    std::vector<std::string> layerFilter;     ///< Only import these layers (empty = all)
    bool ignoreConstructionLayers = false;    ///< Skip DEFPOINTS / CONSTRUCTION* layers
    double splineTolerance = 0.1;             ///< Reserved for spline refinement
    bool autoUnitScale = true;                ///< Convert $INSUNITS to millimeters when the header names a unit
};

/// Result of a DXF import.
struct DXFImportResult {
    bool success = false;
    std::vector<Entity> entities;
    std::string errorMessage;
    int entityCount = 0;
    geometry::BoundingBox bounds;
    std::vector<std::string> layers;          ///< Layers seen in the file
    std::vector<std::string> blocks;          ///< Block names referenced by INSERT
    std::vector<std::string> warnings;        ///< Under-specified/unsupported constructs, non-fatal
};

/// Import entities from an ASCII-DXF string.
/// @param dxfContent  the whole DXF document
/// @param startId     first entity id to assign
/// @param options     import options
HOBBYCAD_EXPORT DXFImportResult importDXFString(
    const std::string& dxfContent,
    int startId = 1,
    const DXFImportOptions& options = {});

/// Import entities from an ASCII-DXF file on disk.
HOBBYCAD_EXPORT DXFImportResult importDXFFile(
    const std::string& filePath,
    int startId = 1,
    const DXFImportOptions& options = {});

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_DXF_IMPORT_H
