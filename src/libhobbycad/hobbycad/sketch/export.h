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

#include <QString>
#include <QVector>

namespace hobbycad {
namespace sketch {

// =====================================================================
//  SVG Export
// =====================================================================

/// Options for SVG export
struct SVGExportOptions {
    double strokeWidth = 0.5;          ///< Stroke width in mm
    QString strokeColor = "#000000";   ///< Stroke color (hex)
    QString fillColor = "none";        ///< Fill color (hex or "none")
    QString constructionColor = "#0000FF";  ///< Color for construction geometry
    bool includeConstraints = false;   ///< Show constraint annotations
    bool includeDimensions = true;     ///< Show dimension values
    double margin = 5.0;               ///< Margin around sketch in mm
    double scale = 1.0;                ///< Scale factor (1.0 = 1mm per SVG unit)
};

/// Export sketch to SVG string
/// @param entities Sketch entities
/// @param constraints Sketch constraints (optional, for dimension display)
/// @param options Export options
/// @return SVG document as string
HOBBYCAD_EXPORT QString sketchToSVG(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints = {},
    const SVGExportOptions& options = {});

/// Export sketch to SVG file
/// @param entities Sketch entities
/// @param constraints Sketch constraints
/// @param filePath Output file path
/// @param options Export options
/// @return True on success
HOBBYCAD_EXPORT bool exportSketchToSVG(
    const QVector<Entity>& entities,
    const QVector<Constraint>& constraints,
    const QString& filePath,
    const SVGExportOptions& options = {});

// =====================================================================
//  DXF Export
// =====================================================================

/// Options for DXF export
struct DXFExportOptions {
    QString layerName = "0";           ///< Default layer name
    QString constructionLayer = "CONSTRUCTION";  ///< Layer for construction geometry
    int colorIndex = 7;                ///< DXF color index (7 = white/black)
    int constructionColorIndex = 5;    ///< Color index for construction
    bool usePolylines = true;          ///< Use LWPOLYLINE for complex shapes
};

/// Export sketch to DXF string
/// @param entities Sketch entities
/// @param options Export options
/// @return DXF document as string
HOBBYCAD_EXPORT QString sketchToDXF(
    const QVector<Entity>& entities,
    const DXFExportOptions& options = {});

/// Export sketch to DXF file
/// @param entities Sketch entities
/// @param filePath Output file path
/// @param options Export options
/// @return True on success
HOBBYCAD_EXPORT bool exportSketchToDXF(
    const QVector<Entity>& entities,
    const QString& filePath,
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
    QPointF offset = QPointF(0, 0);    ///< Offset to apply to all points
};

/// Result of SVG import
struct SVGImportResult {
    bool success = false;
    QVector<Entity> entities;
    QString errorMessage;
    int entityCount = 0;               ///< Number of entities created
    geometry::BoundingBox bounds;      ///< Bounds of imported geometry
};

/// Import entities from SVG path data (the "d" attribute)
/// @param svgPathData SVG path "d" attribute content
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGPath(
    const QString& svgPathData,
    int startId = 1,
    const SVGImportOptions& options = {});

/// Import entities from SVG file
/// @param filePath Path to SVG file
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGFile(
    const QString& filePath,
    int startId = 1,
    const SVGImportOptions& options = {});

/// Import entities from SVG string content
/// @param svgContent SVG document as string
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT SVGImportResult importSVGString(
    const QString& svgContent,
    int startId = 1,
    const SVGImportOptions& options = {});

// =====================================================================
//  DXF Import
// =====================================================================

/// Options for DXF import
struct DXFImportOptions {
    double scale = 1.0;                ///< Scale factor (1.0 = 1 DXF unit = 1mm)
    QPointF offset = QPointF(0, 0);    ///< Offset to apply to all points
    bool importBlocks = true;          ///< Import block references (INSERT)
    bool importHatch = false;          ///< Import hatch boundaries
    QStringList layerFilter;           ///< Only import these layers (empty = all)
    bool ignoreConstructionLayers = false;  ///< Skip layers named "CONSTRUCTION", "DEFPOINTS", etc.
    double splineTolerance = 0.1;      ///< Tolerance for spline approximation
};

/// Result of DXF import
struct DXFImportResult {
    bool success = false;
    QVector<Entity> entities;
    QString errorMessage;
    int entityCount = 0;               ///< Number of entities created
    geometry::BoundingBox bounds;      ///< Bounds of imported geometry
    QStringList layers;                ///< Layers found in file
    QStringList blocks;                ///< Block names found in file
};

/// Import entities from DXF file
/// @param filePath Path to DXF file
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT DXFImportResult importDXFFile(
    const QString& filePath,
    int startId = 1,
    const DXFImportOptions& options = {});

/// Import entities from DXF string content
/// @param dxfContent DXF document as string
/// @param startId Starting ID for created entities
/// @param options Import options
/// @return Import result with entities
HOBBYCAD_EXPORT DXFImportResult importDXFString(
    const QString& dxfContent,
    int startId = 1,
    const DXFImportOptions& options = {});

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_EXPORT_H
