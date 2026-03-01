// =====================================================================
//  src/libhobbycad/brep_io.cpp — BREP file read/write
// =====================================================================

#include "hobbycad/brep_io.h"

#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>

#include <filesystem>

namespace hobbycad {
namespace brep_io {

std::vector<TopoDS_Shape> readBrep(const std::string& path, std::string* errorMsg)
{
    std::vector<TopoDS_Shape> result;

    if (!std::filesystem::exists(path)) {
        if (errorMsg) *errorMsg = "File not found: " + path;
        return result;
    }

    BRep_Builder builder;
    TopoDS_Shape shape;

    if (!BRepTools::Read(shape, path.c_str(), builder)) {
        if (errorMsg) *errorMsg = "Failed to read BREP file: " + path;
        return result;
    }

    // If the shape is a compound, extract its children individually.
    // Otherwise, return it as a single-element list.
    if (shape.ShapeType() == TopAbs_COMPOUND) {
        for (TopExp_Explorer exp(shape, TopAbs_SOLID); exp.More(); exp.Next()) {
            result.push_back(exp.Current());
        }
        // If no solids found, try shells and faces
        if (result.empty()) {
            for (TopExp_Explorer exp(shape, TopAbs_SHELL); exp.More(); exp.Next()) {
                result.push_back(exp.Current());
            }
        }
        if (result.empty()) {
            // Fall back to the compound itself
            result.push_back(shape);
        }
    } else {
        result.push_back(shape);
    }

    return result;
}

bool writeBrep(const std::string& path, const std::vector<TopoDS_Shape>& shapes,
               std::string* errorMsg)
{
    if (shapes.empty()) {
        if (errorMsg) *errorMsg = "No shapes to write";
        return false;
    }

    TopoDS_Shape toWrite;

    if (shapes.size() == 1) {
        toWrite = shapes.front();
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

    if (!BRepTools::Write(toWrite, path.c_str())) {
        if (errorMsg) *errorMsg = "Failed to write BREP file: " + path;
        return false;
    }

    return true;
}

bool writeBrep(const std::string& path, const TopoDS_Shape& shape,
               std::string* errorMsg)
{
    return writeBrep(path, std::vector<TopoDS_Shape>{shape}, errorMsg);
}

}  // namespace brep_io
}  // namespace hobbycad
