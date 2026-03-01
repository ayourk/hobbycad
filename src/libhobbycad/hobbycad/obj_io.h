// =====================================================================
//  src/libhobbycad/hobbycad/obj_io.h — OBJ file import/export utilities
// =====================================================================
//
//  Wavefront OBJ import/export for mesh interchange and visualization.
//  Uses OpenCASCADE's RWObj classes for robust OBJ handling.
//  Supports vertex colors, normals, texture coordinates, and materials.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_OBJ_IO_H
#define HOBBYCAD_OBJ_IO_H

#include "core.h"
#include "types.h"

#include <TopoDS_Shape.hxx>
#include <Poly_Triangulation.hxx>

#include <map>
#include <string>
#include <vector>

namespace hobbycad {
namespace obj_io {

/// Material definition from MTL file
struct ObjMaterial {
    std::string name;
    Vec3 ambient{0.2f, 0.2f, 0.2f};    ///< Ka
    Vec3 diffuse{0.8f, 0.8f, 0.8f};    ///< Kd
    Vec3 specular{1.0f, 1.0f, 1.0f};   ///< Ks
    float shininess = 32.0f;                 ///< Ns
    float opacity = 1.0f;                    ///< d or Tr
    std::string diffuseMap;                  ///< map_Kd texture path
    std::string normalMap;                   ///< map_Bump or bump texture path
};

/// Result of an OBJ read operation
struct ReadResult {
    bool success = false;
    std::string errorMessage;
    TopoDS_Shape shape;                              ///< Combined shape from all objects
    std::vector<TopoDS_Shape> shapes;                ///< Individual objects/groups
    std::vector<std::string> objectNames;            ///< Names of objects/groups
    std::map<std::string, ObjMaterial> materials;    ///< Materials from MTL file
    int vertexCount = 0;                             ///< Total vertices
    int faceCount = 0;                               ///< Total faces
    int objectCount = 0;                             ///< Number of objects/groups
};

/// Result of an OBJ write operation
struct WriteResult {
    bool success = false;
    std::string errorMessage;
    int vertexCount = 0;                     ///< Number of vertices written
    int faceCount = 0;                       ///< Number of faces written
    bool mtlWritten = false;                 ///< Whether MTL file was written
};

/// Options for OBJ export
struct WriteOptions {
    bool writeNormals = true;                ///< Include vertex normals
    bool writeUVs = false;                   ///< Include texture coordinates
    bool writeMtl = false;                   ///< Write accompanying MTL file
    double linearDeflection = 0.1;           ///< Mesh quality (mm)
    double angularDeflection = 0.5;          ///< Mesh quality (radians)
    std::string objectName;                  ///< Object name (default: filename)
};

// ---- Import functions ----

/// Read an OBJ file.
/// @param path Path to the OBJ file
/// @return ReadResult with shapes and status
HOBBYCAD_EXPORT ReadResult readObj(const std::string& path);

/// Read an OBJ file and return as a single shape.
/// @param path Path to the OBJ file
/// @param errorMsg Optional error message output
/// @return Combined shape (null on failure)
HOBBYCAD_EXPORT TopoDS_Shape readObjAsShape(
    const std::string& path,
    std::string* errorMsg = nullptr);

/// Read an OBJ file and return individual shapes.
/// @param path Path to the OBJ file
/// @param errorMsg Optional error message output
/// @return List of shapes (empty on failure)
HOBBYCAD_EXPORT std::vector<TopoDS_Shape> readObjAsShapes(
    const std::string& path,
    std::string* errorMsg = nullptr);

// ---- Export functions ----

/// Write shapes to an OBJ file.
/// @param path Output file path
/// @param shapes Shapes to write
/// @param options Export options
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeObj(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    const WriteOptions& options = WriteOptions{});

/// Write a single shape to an OBJ file.
/// @param path Output file path
/// @param shape Shape to write
/// @param options Export options
/// @return WriteResult with status
HOBBYCAD_EXPORT WriteResult writeObj(
    const std::string& path,
    const TopoDS_Shape& shape,
    const WriteOptions& options = WriteOptions{});

/// Write shapes to an OBJ file (legacy interface).
/// @param path Output file path
/// @param shapes Shapes to write
/// @param errorMsg Optional error message output
/// @return true on success
HOBBYCAD_EXPORT bool writeObj(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    std::string* errorMsg);

// ---- Utility functions ----

/// Check if a file appears to be an OBJ file (by extension).
HOBBYCAD_EXPORT bool isObjFile(const std::string& path);

/// Get supported OBJ file extensions.
HOBBYCAD_EXPORT std::vector<std::string> objExtensions();

/// Get default write options for different use cases.
HOBBYCAD_EXPORT WriteOptions defaultOptions();     ///< General purpose
HOBBYCAD_EXPORT WriteOptions highQualityOptions(); ///< High detail
HOBBYCAD_EXPORT WriteOptions fastOptions();        ///< Fast export, lower detail

}  // namespace obj_io
}  // namespace hobbycad

#endif  // HOBBYCAD_OBJ_IO_H
