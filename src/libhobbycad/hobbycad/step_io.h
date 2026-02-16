// =====================================================================
//  src/libhobbycad/hobbycad/step_io.h — STEP file read/write utilities
// =====================================================================
//
//  STEP (ISO 10303-21) import/export functions for CAD interchange.
//  Uses OpenCASCADE's STEPControl_Reader and STEPControl_Writer.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_STEP_IO_H
#define HOBBYCAD_STEP_IO_H

#include "core.h"

#include <TopoDS_Shape.hxx>
#include <QString>
#include <QList>

namespace hobbycad {
namespace step_io {

/// STEP file format version for export
enum class StepVersion {
    AP203,  ///< AP203 - Configuration controlled 3D design (most compatible)
    AP214,  ///< AP214 - Automotive design
    AP242   ///< AP242 - Managed model-based 3D engineering (newest)
};

/// Result of a STEP read operation
struct ReadResult {
    bool success = false;
    QList<TopoDS_Shape> shapes;
    QString errorMessage;
    int shapeCount = 0;         ///< Number of shapes read
    int rootCount = 0;          ///< Number of root entities in file
};

/// Result of a STEP write operation
struct WriteResult {
    bool success = false;
    QString errorMessage;
    int shapeCount = 0;         ///< Number of shapes written
};

/// Read a STEP file and return all shapes.
/// @param path Path to the STEP file (.step, .stp)
/// @return ReadResult with shapes and status
HOBBYCAD_EXPORT ReadResult readStep(const QString& path);

/// Read a STEP file (legacy interface).
/// @param path Path to the STEP file
/// @param errorMsg Optional error message output
/// @return List of shapes (empty on failure)
HOBBYCAD_EXPORT QList<TopoDS_Shape> readStep(
    const QString& path,
    QString* errorMsg);

/// Write shapes to a STEP file.
/// @param path Output file path
/// @param shapes Shapes to write
/// @param version STEP version to use (default: AP214)
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStep(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    StepVersion version = StepVersion::AP214);

/// Write a single shape to a STEP file.
/// @param path Output file path
/// @param shape Shape to write
/// @param version STEP version to use
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStep(
    const QString& path,
    const TopoDS_Shape& shape,
    StepVersion version = StepVersion::AP214);

/// Write shapes to a STEP file (legacy interface).
/// @param path Output file path
/// @param shapes Shapes to write
/// @param errorMsg Optional error message output
/// @return true on success
HOBBYCAD_EXPORT bool writeStep(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg);

/// Check if a file appears to be a STEP file (by extension).
HOBBYCAD_EXPORT bool isStepFile(const QString& path);

/// Get supported STEP file extensions.
HOBBYCAD_EXPORT QStringList stepExtensions();

}  // namespace step_io
}  // namespace hobbycad

#endif  // HOBBYCAD_STEP_IO_H
