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

#include <QFile>
#include <QFileInfo>

namespace hobbycad {
namespace stl_io {

WriteResult writeStl(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    StlFormat format,
    const MeshQuality& quality)
{
    WriteResult result;

    if (shapes.isEmpty()) {
        result.errorMessage = QStringLiteral("No shapes to write");
        return result;
    }

    // Combine shapes into a compound if multiple
    TopoDS_Shape shapeToExport;

    if (shapes.size() == 1) {
        shapeToExport = shapes.first();
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
        result.errorMessage = QStringLiteral("No valid shapes to export");
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
            result.errorMessage = QStringLiteral("Failed to mesh shape");
            return result;
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during meshing");
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
        if (!writer.Write(shapeToExport, path.toUtf8().constData())) {
            result.errorMessage = QStringLiteral("Failed to write STL file");
            return result;
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during STL export");
        return result;
    }

    result.success = true;
    return result;
}

WriteResult writeStl(
    const QString& path,
    const TopoDS_Shape& shape,
    StlFormat format,
    const MeshQuality& quality)
{
    return writeStl(path, QList<TopoDS_Shape>{shape}, format, quality);
}

bool writeStl(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg)
{
    WriteResult result = writeStl(path, shapes, StlFormat::Binary, defaultQuality());

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.success;
}

bool isStlFile(const QString& path)
{
    QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".stl"));
}

QStringList stlExtensions()
{
    return {
        QStringLiteral("stl"),
        QStringLiteral("STL")
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

StlFormat detectStlFormat(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return StlFormat::Binary;  // Default assumption
    }

    // Read first 80 bytes (header) plus some content
    QByteArray header = file.read(256);
    file.close();

    if (header.size() < 6) {
        return StlFormat::Binary;
    }

    // ASCII STL starts with "solid" (case insensitive)
    // But binary files might coincidentally have "solid" in header
    // So we also check for valid ASCII characters throughout
    QByteArray lower = header.left(6).toLower();
    if (lower.startsWith("solid ") || lower.startsWith("solid\n") || lower.startsWith("solid\r")) {
        // Check if rest of header contains only printable ASCII
        bool isAscii = true;
        for (int i = 6; i < header.size() && isAscii; ++i) {
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

ReadResult readStl(const QString& path)
{
    ReadResult result;

    if (!QFile::exists(path)) {
        result.errorMessage = QStringLiteral("File not found: %1").arg(path);
        return result;
    }

    // Detect format
    result.detectedFormat = detectStlFormat(path);

    // Read using RWStl
    try {
        OSD_Path osdPath(path.toUtf8().constData());
        Handle(Poly_Triangulation) mesh;

        if (result.detectedFormat == StlFormat::Ascii) {
            mesh = RWStl::ReadAscii(osdPath, Message_ProgressRange());
        } else {
            mesh = RWStl::ReadBinary(osdPath, Message_ProgressRange());
        }

        if (mesh.IsNull()) {
            result.errorMessage = QStringLiteral("Failed to read STL mesh");
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
        result.errorMessage = QStringLiteral("OCCT exception: %1")
            .arg(e.GetMessageString());
    } catch (...) {
        result.errorMessage = QStringLiteral("Unknown exception during STL import");
    }

    return result;
}

TopoDS_Shape readStlAsShape(const QString& path, QString* errorMsg)
{
    ReadResult result = readStl(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shape;
}

Handle(Poly_Triangulation) readStlAsMesh(const QString& path, QString* errorMsg)
{
    ReadResult result = readStl(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.mesh;
}

}  // namespace stl_io
}  // namespace hobbycad
