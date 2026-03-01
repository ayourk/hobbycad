// =====================================================================
//  src/libhobbycad/stl_io.cpp — STL file import/export utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/stl_io.h>

// OpenCASCADE STL I/O
#include <StlAPI_Writer.hxx>
#include <RWStl.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <OSD_Path.hxx>
#include <Message_ProgressRange.hxx>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace hobbycad {
namespace stl_io {

WriteResult writeStl(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    StlFormat format,
    const MeshQuality& quality)
{
    WriteResult result;

    if (shapes.empty()) {
        result.errorMessage = "No shapes to write";
        return result;
    }

    // Combine shapes into a compound if multiple
    TopoDS_Shape shapeToExport;

    if (shapes.size() == 1) {
        shapeToExport = shapes.front();
    } else {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);

        for (const TopoDS_Shape& shape : shapes) {
            if (!shape.IsNull()) {
                builder.Add(compound, shape);
            }
        }

        shapeToExport = compound;
    }

    if (shapeToExport.IsNull()) {
        result.errorMessage = "No valid shapes to export";
        return result;
    }

    // Mesh the shape
    try {
        BRepMesh_IncrementalMesh mesh(
            shapeToExport,
            quality.linearDeflection,
            quality.relative,
            quality.angularDeflection);

        if (!mesh.IsDone()) {
            result.errorMessage = "Failed to mesh shape";
            return result;
        }
    } catch (...) {
        result.errorMessage = "Exception during meshing";
        return result;
    }

    // Count triangles (approximate - count faces)
    for (TopExp_Explorer exp(shapeToExport, TopAbs_FACE); exp.More(); exp.Next()) {
        result.triangleCount++;
    }

    // Write STL file
    StlAPI_Writer writer;
    writer.ASCIIMode() = (format == StlFormat::Ascii);

    try {
        if (!writer.Write(shapeToExport, path.c_str())) {
            result.errorMessage = "Failed to write STL file";
            return result;
        }
    } catch (...) {
        result.errorMessage = "Exception during STL export";
        return result;
    }

    result.success = true;
    return result;
}

WriteResult writeStl(
    const std::string& path,
    const TopoDS_Shape& shape,
    StlFormat format,
    const MeshQuality& quality)
{
    return writeStl(path, std::vector<TopoDS_Shape>{shape}, format, quality);
}

bool writeStl(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    std::string* errorMsg)
{
    WriteResult result = writeStl(path, shapes, StlFormat::Binary, defaultQuality());

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.success;
}

bool isStlFile(const std::string& path)
{
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".stl";
}

std::vector<std::string> stlExtensions()
{
    return {
        "stl",
        "STL"
    };
}

MeshQuality defaultQuality()
{
    return MeshQuality{0.1, 0.5, false};
}

MeshQuality highQuality()
{
    return MeshQuality{0.01, 0.1, false};
}

MeshQuality fastQuality()
{
    return MeshQuality{0.5, 1.0, false};
}

// ---- Import functions ----

StlFormat detectStlFormat(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return StlFormat::Binary;  // Default assumption
    }

    // Read first 256 bytes (header + some content)
    char header[256];
    file.read(header, 256);
    auto bytesRead = file.gcount();
    file.close();

    if (bytesRead < 6) {
        return StlFormat::Binary;
    }

    // ASCII STL starts with "solid" (case insensitive)
    // But binary files might coincidentally have "solid" in header
    // So we also check for valid ASCII characters throughout
    std::string first6(header, 6);
    std::string lower6 = first6;
    std::transform(lower6.begin(), lower6.end(), lower6.begin(), ::tolower);

    if (lower6.substr(0, 6) == "solid " || lower6.substr(0, 6) == "solid\n" ||
        lower6.substr(0, 6) == "solid\r") {
        // Check if rest of header contains only printable ASCII
        bool isAscii = true;
        for (int i = 6; i < bytesRead && isAscii; ++i) {
            char c = header[i];
            if (c != '\n' && c != '\r' && c != '\t' && (c < 32 || c > 126)) {
                isAscii = false;
            }
        }
        if (isAscii) {
            return StlFormat::Ascii;
        }
    }

    return StlFormat::Binary;
}

ReadResult readStl(const std::string& path)
{
    ReadResult result;

    if (!std::filesystem::exists(path)) {
        result.errorMessage = "File not found: " + path;
        return result;
    }

    // Detect format
    result.detectedFormat = detectStlFormat(path);

    // Read using RWStl
    try {
        OSD_Path osdPath(path.c_str());
        Handle(Poly_Triangulation) mesh;

        if (result.detectedFormat == StlFormat::Ascii) {
            mesh = RWStl::ReadAscii(osdPath, Message_ProgressRange());
        } else {
            mesh = RWStl::ReadBinary(osdPath, Message_ProgressRange());
        }

        if (mesh.IsNull()) {
            result.errorMessage = "Failed to read STL mesh";
            return result;
        }

        result.mesh = mesh;
        result.triangleCount = mesh->NbTriangles();
        result.nodeCount = mesh->NbNodes();

        // Build a face from the triangulation
        TopoDS_Face face;
        BRep_Builder builder;
        builder.MakeFace(face);
        builder.UpdateFace(face, mesh);

        result.shape = face;
        result.success = true;

    } catch (const Standard_Failure& e) {
        result.errorMessage = std::string("OCCT exception: ") + e.GetMessageString();
    } catch (...) {
        result.errorMessage = "Unknown exception during STL import";
    }

    return result;
}

TopoDS_Shape readStlAsShape(const std::string& path, std::string* errorMsg)
{
    ReadResult result = readStl(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shape;
}

Handle(Poly_Triangulation) readStlAsMesh(const std::string& path, std::string* errorMsg)
{
    ReadResult result = readStl(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.mesh;
}

}  // namespace stl_io
}  // namespace hobbycad
