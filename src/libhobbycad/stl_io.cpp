// =====================================================================
//  src/libhobbycad/stl_io.cpp — STL file export utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/stl_io.h>

// OpenCASCADE STL I/O
#include <StlAPI_Writer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

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

}  // namespace stl_io
}  // namespace hobbycad
