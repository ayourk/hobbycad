// =====================================================================
//  src/libhobbycad/hobbycad/sketch/export.h — Sketch export functions
// =====================================================================
//
//  Functions for exporting sketches to various formats.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SKETCH_EXPORT_H
#define HOBBYCAD_SKETCH_EXPORT_H

#include "entity.h"
#include "constraint.h"
#include "../core.h"
#include "../types.h"

#include <string>
#include <vector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  SVG Export
// =====================================================================

/// Options for SVG export
struct SVGExportOptions {
    double strokeWidth = 0.5;                   ///< Stroke width in mm
    std::string strokeColor = "#000000";        ///< Stroke color (hex)
    std::string fillColor = "none";             ///< Fill color (hex or "none")
    std::string constructionColor = "#0000FF";  ///< Color for construction geometry
    bool includeConstraints = false;            ///< Show constraint annotations
    bool includeDimensions = true;              ///< Show dimension values
    double margin = 5.0;                        ///< Margin around sketch in mm
    double scale = 1.0;                         ///< Scale factor (1.0 = 1mm per SVG unit)
};

/// Export sketch to SVG string
/// @param entities Sketch entities
/// @param constraints Sketch constraints (optional, for dimension display)
/// @param options Export options
/// @return SVG document as string
HOBBYCAD_EXPORT std::string sketchToSVG(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints = {},
    const SVGExportOptions& options = {});

/// Export sketch to SVG file
/// @param entities Sketch entities
/// @param constraints Sketch constraints
/// @param filePath Output file path
/// @param options Export options
/// @return True on success
HOBBYCAD_EXPORT bool exportSketchToSVG(
    const std::vector<Entity>& entities,
    const std::vector<Constraint>& constraints,
    const std::string& filePath,
    const SVGExportOptions& options = {});

// =====================================================================
//  DXF Export
// =====================================================================

/// Options for DXF export
struct DXFExportOptions {
    std::string layerName = "0";                      ///< Default layer name
    std::string constructionLayer = "CONSTRUCTION";   ///< Layer for construction geometry
    int colorIndex = 7;                               ///< DXF color index (7 = white/black)
    int constructionColorIndex = 5;                   ///< Color index for construction
    bool usePolylines = true;                         ///< Use LWPOLYLINE for complex shapes
};

/// Export sketch to DXF string
/// @param entities Sketch entities
/// @param options Export options
/// @return DXF document as string
HOBBYCAD_EXPORT std::string sketchToDXF(
    const std::vector<Entity>& entities,
    const DXFExportOptions& options = {});

/// Export sketch to DXF file
/// @param entities Sketch entities
/// @param filePath Output file path
/// @param options Export options
/// @return True on success
HOBBYCAD_EXPORT bool exportSketchToDXF(
    const std::vector<Entity>& entities,
    const std::string& filePath,
    const DXFExportOptions& options = {});

// =====================================================================
//  SVG Import
// =====================================================================

/// Options for SVG import
struct SVGImportOptions {
    double scale = 1.0;                ///< Scale factor (1.0 = 1 SVG unit = 1mm)
    bool flipY = true;                 ///< Flip Y axis (SVG Y grows down)
    double tolerance = 0.1;            ///< Curve approximation tolerance
    bool convertArcsToLines = false;   ///< Convert arcs to polylines
    Point2D offset;                    ///< Offset to apply to all points
};

/// Result of SVG import
struct SVGImportResult {
    bool success = false;
    std::vector<Entity> entities;
    std::string errorMessage;
    int entityCount = 0;               ///< Number of entities created
    geometry::BoundingBox bounds;      ///< Bounds of imported geometry
};

/// Import entities from SVG path data (the "d" attribute)
/// @param svgPathData SVG path "d" attribute content
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGPath(
    const std::string& svgPathData,
    int startId = 1,
    const SVGImportOptions& options = {});

/// Import entities from SVG file
/// @param filePath Path to SVG file
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGFile(
    const std::string& filePath,
    int startId = 1,
    const SVGImportOptions& options = {});

/// Import entities from SVG string content
/// @param svgContent SVG document as string
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGString(
    const std::string& svgContent,
    int startId = 1,
    const SVGImportOptions& options = {});

// =====================================================================
//  DXF Import
// =====================================================================

/// Options for DXF import
struct DXFImportOptions {
    double scale = 1.0;                      ///< Scale factor (1.0 = 1 DXF unit = 1mm)
    Point2D offset;                          ///< Offset to apply to all points
    bool importBlocks = true;                ///< Import block references (INSERT)
    bool importHatch = false;                ///< Import hatch boundaries
    std::vector<std::string> layerFilter;    ///< Only import these layers (empty = all)
    bool ignoreConstructionLayers = false;    ///< Skip layers named "CONSTRUCTION", "DEFPOINTS", etc.
    double splineTolerance = 0.1;            ///< Tolerance for spline approximation
};

/// Result of DXF import
struct DXFImportResult {
    bool success = false;
    std::vector<Entity> entities;
    std::string errorMessage;
    int entityCount = 0;                     ///< Number of entities created
    geometry::BoundingBox bounds;            ///< Bounds of imported geometry
    std::vector<std::string> layers;         ///< Layers found in file
    std::vector<std::string> blocks;         ///< Block names found in file
};

/// Import entities from DXF file
/// @param filePath Path to DXF file
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT DXFImportResult importDXFFile(
    const std::string& filePath,
    int startId = 1,
    const DXFImportOptions& options = {});

/// Import entities from DXF string content
/// @param dxfContent DXF document as string
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT DXFImportResult importDXFString(
    const std::string& dxfContent,
    int startId = 1,
    const DXFImportOptions& options = {});

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_EXPORT_H
