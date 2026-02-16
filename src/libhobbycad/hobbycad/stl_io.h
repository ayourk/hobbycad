// =====================================================================
//  src/libhobbycad/hobbycad/stl_io.h — STL file export utilities
// =====================================================================
//
//  STL (stereolithography) export for 3D printing and mesh visualization.
//  Uses OpenCASCADE's StlAPI_Writer.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_STL_IO_H
#define HOBBYCAD_STL_IO_H

#include "core.h"

#include <TopoDS_Shape.hxx>
#include <QString>
#include <QList>

namespace hobbycad {
namespace stl_io {

/// STL export format
enum class StlFormat {
    Binary,     ///< Binary STL (smaller, faster)
    Ascii       ///< ASCII STL (human-readable)
};

/// STL mesh quality settings
struct MeshQuality {
    double linearDeflection = 0.1;   ///< Max linear deviation (mm)
    double angularDeflection = 0.5;  ///< Max angular deviation (radians)
    bool relative = false;           ///< If true, linearDeflection is relative to shape size
};

/// Result of an STL write operation
struct WriteResult {
    bool success = false;
    QString errorMessage;
    int triangleCount = 0;      ///< Number of triangles written
};

/// Write shapes to an STL file.
/// @param path Output file path
/// @param shapes Shapes to write (will be merged into single mesh)
/// @param format Binary or ASCII format
/// @param quality Mesh quality settings
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStl(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    StlFormat format = StlFormat::Binary,
    const MeshQuality& quality = MeshQuality{});

/// Write a single shape to an STL file.
/// @param path Output file path
/// @param shape Shape to write
/// @param format Binary or ASCII format
/// @param quality Mesh quality settings
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStl(
    const QString& path,
    const TopoDS_Shape& shape,
    StlFormat format = StlFormat::Binary,
    const MeshQuality& quality = MeshQuality{});

/// Write shapes to an STL file (legacy interface).
/// @param path Output file path
/// @param shapes Shapes to write
/// @param errorMsg Optional error message output
/// @return true on success
HOBBYCAD_EXPORT bool writeStl(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg);

/// Check if a file appears to be an STL file (by extension).
HOBBYCAD_EXPORT bool isStlFile(const QString& path);

/// Get supported STL file extensions.
HOBBYCAD_EXPORT QStringList stlExtensions();

/// Get default mesh quality settings for different use cases.
HOBBYCAD_EXPORT MeshQuality defaultQuality();       ///< General purpose
HOBBYCAD_EXPORT MeshQuality highQuality();          ///< High detail
HOBBYCAD_EXPORT MeshQuality fastQuality();          ///< Fast export, lower detail

}  // namespace stl_io
}  // namespace hobbycad

#endif  // HOBBYCAD_STL_IO_H
