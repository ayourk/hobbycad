// =====================================================================
//  src/libhobbycad/hobbycad/stl_io.h — STL file import/export utilities
// =====================================================================
//
//  STL (stereolithography) import/export for 3D printing and mesh
//  visualization. Uses OpenCASCADE's RWStl and StlAPI classes.
//  Supports both binary and ASCII STL formats.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_STL_IO_H
#define HOBBYCAD_STL_IO_H

#include "core.h"

#include <TopoDS_Shape.hxx>
#include <Poly_Triangulation.hxx>

#include <string>
#include <vector>

namespace hobbycad {
namespace stl_io {

/// STL file format
enum class StlFormat {
    Binary,     ///< Binary STL (smaller, faster)
    Ascii       ///< ASCII STL (human-readable)
};

/// STL mesh quality settings for export
struct MeshQuality {
    double linearDeflection = 0.1;   ///< Max linear deviation (mm)
    double angularDeflection = 0.5;  ///< Max angular deviation (radians)
    bool relative = false;           ///< If true, linearDeflection is relative to shape size
};

/// Result of an STL read operation
struct ReadResult {
    bool success = false;
    std::string errorMessage;
    Handle(Poly_Triangulation) mesh;  ///< The triangulation data
    TopoDS_Shape shape;               ///< Shape constructed from mesh (if requested)
    int triangleCount = 0;            ///< Number of triangles read
    int nodeCount = 0;                ///< Number of vertices read
    StlFormat detectedFormat = StlFormat::Binary;  ///< Detected file format
};

/// Result of an STL write operation
struct WriteResult {
    bool success = false;
    std::string errorMessage;
    int triangleCount = 0;      ///< Number of triangles written
};

/// Write shapes to an STL file.
/// @param path Output file path
/// @param shapes Shapes to write (will be merged into single mesh)
/// @param format Binary or ASCII format
/// @param quality Mesh quality settings
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStl(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    StlFormat format = StlFormat::Binary,
    const MeshQuality& quality = MeshQuality{});

/// Write a single shape to an STL file.
/// @param path Output file path
/// @param shape Shape to write
/// @param format Binary or ASCII format
/// @param quality Mesh quality settings
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeStl(
    const std::string& path,
    const TopoDS_Shape& shape,
    StlFormat format = StlFormat::Binary,
    const MeshQuality& quality = MeshQuality{});

/// Write shapes to an STL file (legacy interface).
/// @param path Output file path
/// @param shapes Shapes to write
/// @param errorMsg Optional error message output
/// @return true on success
HOBBYCAD_EXPORT bool writeStl(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    std::string* errorMsg);

// ---- Import functions ----

/// Read an STL file (binary or ASCII, auto-detected).
/// @param path Path to the STL file
/// @return ReadResult with mesh/shape and status
HOBBYCAD_EXPORT ReadResult readStl(const std::string& path);

/// Read an STL file and convert to a TopoDS_Shape.
/// The result is a face with the triangulation attached.
/// @param path Path to the STL file
/// @param errorMsg Optional error message output
/// @return Shape (null on failure)
HOBBYCAD_EXPORT TopoDS_Shape readStlAsShape(
    const std::string& path,
    std::string* errorMsg = nullptr);

/// Read an STL file and return raw triangulation.
/// @param path Path to the STL file
/// @param errorMsg Optional error message output
/// @return Triangulation handle (null on failure)
HOBBYCAD_EXPORT Handle(Poly_Triangulation) readStlAsMesh(
    const std::string& path,
    std::string* errorMsg = nullptr);

/// Detect the format of an STL file (binary or ASCII).
/// @param path Path to the STL file
/// @return Detected format (defaults to Binary if detection fails)
HOBBYCAD_EXPORT StlFormat detectStlFormat(const std::string& path);

// ---- Utility functions ----

/// Check if a file appears to be an STL file (by extension).
HOBBYCAD_EXPORT bool isStlFile(const std::string& path);

/// Get supported STL file extensions.
HOBBYCAD_EXPORT std::vector<std::string> stlExtensions();

/// Get default mesh quality settings for different use cases.
HOBBYCAD_EXPORT MeshQuality defaultQuality();       ///< General purpose
HOBBYCAD_EXPORT MeshQuality highQuality();          ///< High detail
HOBBYCAD_EXPORT MeshQuality fastQuality();          ///< Fast export, lower detail

}  // namespace stl_io
}  // namespace hobbycad

#endif  // HOBBYCAD_STL_IO_H
