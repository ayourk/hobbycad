// =====================================================================
//  src/libhobbycad/obj_io.cpp — OBJ file import/export utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/obj_io.h>

// OpenCASCADE OBJ I/O
#include <RWObj.hxx>
#include <RWObj_CafReader.hxx>
#include <RWObj_ObjWriterContext.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <TColStd_IndexedDataMapOfStringString.hxx>
#include <Message_ProgressRange.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hobbycad {
namespace obj_io {

// ---- Import functions ----

ReadResult readObj(const std::string& path)
{
    ReadResult result;

    if (!std::filesystem::exists(path)) {
        result.errorMessage = "File not found: " + path;
        return result;
    }

    try {
        // Use RWObj_CafReader for full OBJ support
        RWObj_CafReader reader;

        // Create a temporary document
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("BinXCAF", doc);

        // Read the OBJ file
        TCollection_AsciiString objPath(path.c_str());

        if (!reader.Perform(objPath, Message_ProgressRange())) {
            result.errorMessage = "Failed to read OBJ file";
            app->Close(doc);
            return result;
        }

        // Get the result shape
        TopoDS_Shape shape = reader.SingleShape();

        if (shape.IsNull()) {
            result.errorMessage = "No geometry found in OBJ file";
            app->Close(doc);
            return result;
        }

        result.shape = shape;
        result.shapes.push_back(shape);
        result.objectCount = 1;

        // Count faces and vertices
        for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Face& face = TopoDS::Face(exp.Current());
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (!tri.IsNull()) {
                result.faceCount += tri->NbTriangles();
                result.vertexCount += tri->NbNodes();
            }
        }

        result.success = true;
        app->Close(doc);

    } catch (const Standard_Failure& e) {
        result.errorMessage = std::string("OCCT exception: ") + e.GetMessageString();
    } catch (...) {
        result.errorMessage = "Unknown exception during OBJ import";
    }

    return result;
}

TopoDS_Shape readObjAsShape(const std::string& path, std::string* errorMsg)
{
    ReadResult result = readObj(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shape;
}

std::vector<TopoDS_Shape> readObjAsShapes(const std::string& path, std::string* errorMsg)
{
    ReadResult result = readObj(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shapes;
}

// ---- Export functions ----

WriteResult writeObj(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    const WriteOptions& options)
{
    WriteResult result;

    if (shapes.empty()) {
        result.errorMessage = "No shapes to write";
        return result;
    }

    // Combine shapes into compound if needed
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
            options.linearDeflection,
            false,  // relative
            options.angularDeflection);

        if (!mesh.IsDone()) {
            result.errorMessage = "Failed to mesh shape";
            return result;
        }
    } catch (...) {
        result.errorMessage = "Exception during meshing";
        return result;
    }

    // Write OBJ file manually (OCCT's writer is complex to use)
    std::ofstream file(path);
    if (!file.is_open()) {
        result.errorMessage = "Cannot open file for writing: " + path;
        return result;
    }

    file << std::fixed << std::setprecision(6);

    // Write header
    namespace fs = std::filesystem;
    fs::path filePath(path);
    std::string objName = options.objectName.empty() ? filePath.stem().string() : options.objectName;
    file << "# OBJ file exported by HobbyCAD\n";
    file << "# https://github.com/ayourk/hobbycad\n";
    file << "\n";

    // Write MTL reference if requested
    std::string mtlPath;
    if (options.writeMtl) {
        mtlPath = filePath.parent_path().string() + "/" + filePath.stem().string() + ".mtl";
        file << "mtllib " << filePath.stem().string() << ".mtl\n\n";
    }

    file << "o " << objName << "\n\n";

    // Collect all vertices, normals, and faces
    int globalVertexOffset = 0;
    int globalNormalOffset = 0;

    for (TopExp_Explorer faceExp(shapeToExport, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faceExp.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);

        if (tri.IsNull()) {
            continue;
        }

        const gp_Trsf& trsf = loc.Transformation();
        bool hasTransform = !loc.IsIdentity();

        // Write vertices
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            gp_Pnt p = tri->Node(i);
            if (hasTransform) {
                p.Transform(trsf);
            }
            file << "v " << p.X() << " " << p.Y() << " " << p.Z() << "\n";
            result.vertexCount++;
        }

        // Write normals if requested
        if (options.writeNormals && tri->HasNormals()) {
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Dir n = tri->Normal(i);
                if (hasTransform) {
                    n.Transform(trsf);
                }
                file << "vn " << n.X() << " " << n.Y() << " " << n.Z() << "\n";
            }
        }

        file << "\n";

        // Write faces
        bool reversed = (face.Orientation() == TopAbs_REVERSED);

        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            const Poly_Triangle& t = tri->Triangle(i);
            int n1, n2, n3;
            t.Get(n1, n2, n3);

            // Adjust for global offset (OBJ indices are 1-based)
            n1 += globalVertexOffset;
            n2 += globalVertexOffset;
            n3 += globalVertexOffset;

            // Reverse winding if face is reversed
            if (reversed) {
                std::swap(n2, n3);
            }

            if (options.writeNormals && tri->HasNormals()) {
                int nn1 = n1 - globalVertexOffset + globalNormalOffset;
                int nn2 = n2 - globalVertexOffset + globalNormalOffset;
                int nn3 = n3 - globalVertexOffset + globalNormalOffset;
                if (reversed) {
                    std::swap(nn2, nn3);
                }
                file << "f " << n1 << "//" << nn1
                     << " " << n2 << "//" << nn2
                     << " " << n3 << "//" << nn3 << "\n";
            } else {
                file << "f " << n1 << " " << n2 << " " << n3 << "\n";
            }
            result.faceCount++;
        }

        globalVertexOffset += tri->NbNodes();
        if (options.writeNormals && tri->HasNormals()) {
            globalNormalOffset += tri->NbNodes();
        }

        file << "\n";
    }

    file.close();

    // Write MTL file if requested
    if (options.writeMtl && !mtlPath.empty()) {
        std::ofstream mtlFile(mtlPath);
        if (mtlFile.is_open()) {
            mtlFile << "# MTL file exported by HobbyCAD\n\n";
            mtlFile << "newmtl default\n";
            mtlFile << "Ka 0.2 0.2 0.2\n";
            mtlFile << "Kd 0.8 0.8 0.8\n";
            mtlFile << "Ks 1.0 1.0 1.0\n";
            mtlFile << "Ns 32.0\n";
            mtlFile << "d 1.0\n";
            mtlFile.close();
            result.mtlWritten = true;
        }
    }

    result.success = true;
    return result;
}

WriteResult writeObj(
    const std::string& path,
    const TopoDS_Shape& shape,
    const WriteOptions& options)
{
    return writeObj(path, std::vector<TopoDS_Shape>{shape}, options);
}

bool writeObj(
    const std::string& path,
    const std::vector<TopoDS_Shape>& shapes,
    std::string* errorMsg)
{
    WriteResult result = writeObj(path, shapes, defaultOptions());

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.success;
}

// ---- Utility functions ----

bool isObjFile(const std::string& path)
{
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.size() >= 4 && lower.substr(lower.size() - 4) == ".obj";
}

std::vector<std::string> objExtensions()
{
    return {
        "obj",
        "OBJ"
    };
}

WriteOptions defaultOptions()
{
    return WriteOptions{true, false, false, 0.1, 0.5, {}};
}

WriteOptions highQualityOptions()
{
    WriteOptions opts;
    opts.writeNormals = true;
    opts.writeUVs = true;
    opts.writeMtl = true;
    opts.linearDeflection = 0.01;
    opts.angularDeflection = 0.1;
    return opts;
}

WriteOptions fastOptions()
{
    WriteOptions opts;
    opts.writeNormals = false;
    opts.writeUVs = false;
    opts.writeMtl = false;
    opts.linearDeflection = 0.5;
    opts.angularDeflection = 1.0;
    return opts;
}

}  // namespace obj_io
}  // namespace hobbycad
