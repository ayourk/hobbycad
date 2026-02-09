// =====================================================================
//  src/libhobbycad/brep_io.cpp — BREP file read/write
// =====================================================================

#include "hobbycad/brep_io.h"

#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>

#include <QFile>

namespace hobbycad {
namespace brep_io {

QList<TopoDS_Shape> readBrep(const QString& path, QString* errorMsg)
{
    QList<TopoDS_Shape> result;

    if (!QFile::exists(path)) {
        if (errorMsg) *errorMsg = QStringLiteral("File not found: ") + path;
        return result;
    }

    BRep_Builder builder;
    TopoDS_Shape shape;

    std::string stdPath = path.toStdString();
    if (!BRepTools::Read(shape, stdPath.c_str(), builder)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to read BREP file: ") + path;
        return result;
    }

    // If the shape is a compound, extract its children individually.
    // Otherwise, return it as a single-element list.
    if (shape.ShapeType() == TopAbs_COMPOUND) {
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            result.append(exp.Current());
        }
        // If no solids found, try shells and faces
        if (result.isEmpty()) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                result.append(exp.Current());
            }
        }
        if (result.isEmpty()) {
            // Fall back to the compound itself
            result.append(shape);
        }
    } else {
        result.append(shape);
    }

    return result;
}

bool writeBrep(const QString& path, const QList<TopoDS_Shape>& shapes,
               QString* errorMsg)
{
    if (shapes.isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("No shapes to write");
        return false;
    }

    TopoDS_Shape toWrite;

    if (shapes.size() == 1) {
        toWrite = shapes.first();
    } else {
        // Multiple shapes: wrap in a compound
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const auto& shape : shapes) {
            builder.Add(compound, shape);
        }
        toWrite = compound;
    }

    std::string stdPath = path.toStdString();
    if (!BRepTools::Write(toWrite, stdPath.c_str())) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to write BREP file: ") + path;
        return false;
    }

    return true;
}

bool writeBrep(const QString& path, const TopoDS_Shape& shape,
               QString* errorMsg)
{
    return writeBrep(path, QList<TopoDS_Shape>{shape}, errorMsg);
}

}  // namespace brep_io
}  // namespace hobbycad

