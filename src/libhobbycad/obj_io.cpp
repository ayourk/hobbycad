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

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>

namespace hobbycad {
namespace obj_io {

// ---- Import functions ----

ReadResult readObj(const QString& path)
{
    ReadResult result;

    if (!QFile::exists(path)) {
        result.errorMessage = QStringLiteral("File not found: %1").arg(path);
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
        TCollection_AsciiString objPath(path.toUtf8().constData());

        if (!reader.Perform(objPath, Message_ProgressRange())) {
            result.errorMessage = QStringLiteral("Failed to read OBJ file");
            app->Close(doc);
            return result;
        }

        // Get the result shape
        TopoDS_Shape shape = reader.SingleShape();

        if (shape.IsNull()) {
            result.errorMessage = QStringLiteral("No geometry found in OBJ file");
            app->Close(doc);
            return result;
        }

        result.shape = shape;
        result.shapes.append(shape);
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
        result.errorMessage = QStringLiteral("OCCT exception: %1")
            .arg(e.GetMessageString());
    } catch (...) {
        result.errorMessage = QStringLiteral("Unknown exception during OBJ import");
    }

    return result;
}

TopoDS_Shape readObjAsShape(const QString& path, QString* errorMsg)
{
    ReadResult result = readObj(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shape;
}

QList<TopoDS_Shape> readObjAsShapes(const QString& path, QString* errorMsg)
{
    ReadResult result = readObj(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shapes;
}

// ---- Export functions ----

WriteResult writeObj(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    const WriteOptions& options)
{
    WriteResult result;

    if (shapes.isEmpty()) {
        result.errorMessage = QStringLiteral("No shapes to write");
        return result;
    }

    // Combine shapes into compound if needed
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
            options.linearDeflection,
            false,  // relative
            options.angularDeflection);

        if (!mesh.IsDone()) {
            result.errorMessage = QStringLiteral("Failed to mesh shape");
            return result;
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during meshing");
        return result;
    }

    // Write OBJ file manually (OCCT's writer is complex to use)
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        result.errorMessage = QStringLiteral("Cannot open file for writing: %1").arg(path);
        return result;
    }

    QTextStream out(&file);
    out.setRealNumberPrecision(6);
    out.setRealNumberNotation(QTextStream::FixedNotation);

    // Write header
    QFileInfo fi(path);
    QString objName = options.objectName.isEmpty() ? fi.baseName() : options.objectName;
    out << "# OBJ file exported by HobbyCAD\n";
    out << "# https://github.com/ayourk/hobbycad\n";
    out << "\n";

    // Write MTL reference if requested
    QString mtlPath;
    if (options.writeMtl) {
        mtlPath = fi.absolutePath() + "/" + fi.baseName() + ".mtl";
        out << "mtllib " << fi.baseName() << ".mtl\n\n";
    }

    out << "o " << objName << "\n\n";

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
            out << "v " << p.X() << " " << p.Y() << " " << p.Z() << "\n";
            result.vertexCount++;
        }

        // Write normals if requested
        if (options.writeNormals && tri->HasNormals()) {
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Dir n = tri->Normal(i);
                if (hasTransform) {
                    n.Transform(trsf);
                }
                out << "vn " << n.X() << " " << n.Y() << " " << n.Z() << "\n";
            }
        }

        out << "\n";

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
                out << "f " << n1 << "//" << nn1
                    << " " << n2 << "//" << nn2
                    << " " << n3 << "//" << nn3 << "\n";
            } else {
                out << "f " << n1 << " " << n2 << " " << n3 << "\n";
            }
            result.faceCount++;
        }

        globalVertexOffset += tri->NbNodes();
        if (options.writeNormals && tri->HasNormals()) {
            globalNormalOffset += tri->NbNodes();
        }

        out << "\n";
    }

    file.close();

    // Write MTL file if requested
    if (options.writeMtl && !mtlPath.isEmpty()) {
        QFile mtlFile(mtlPath);
        if (mtlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream mtlOut(&mtlFile);
            mtlOut << "# MTL file exported by HobbyCAD\n\n";
            mtlOut << "newmtl default\n";
            mtlOut << "Ka 0.2 0.2 0.2\n";
            mtlOut << "Kd 0.8 0.8 0.8\n";
            mtlOut << "Ks 1.0 1.0 1.0\n";
            mtlOut << "Ns 32.0\n";
            mtlOut << "d 1.0\n";
            mtlFile.close();
            result.mtlWritten = true;
        }
    }

    result.success = true;
    return result;
}

WriteResult writeObj(
    const QString& path,
    const TopoDS_Shape& shape,
    const WriteOptions& options)
{
    return writeObj(path, QList<TopoDS_Shape>{shape}, options);
}

bool writeObj(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg)
{
    WriteResult result = writeObj(path, shapes, defaultOptions());

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.success;
}

// ---- Utility functions ----

bool isObjFile(const QString& path)
{
    QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".obj"));
}

QStringList objExtensions()
{
    return {
        QStringLiteral("obj"),
        QStringLiteral("OBJ")
    };
}

WriteOptions defaultOptions()
{
    return WriteOptions{true, false, false, 0.1, 0.5, QString()};
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
