// =====================================================================
//  src/libhobbycad/step_io.cpp — STEP file read/write utilities
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/step_io.h>

// OpenCASCADE STEP I/O
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <Interface_Static.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <XSControl_WorkSession.hxx>
#include <Transfer_TransientProcess.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>

#include <QFileInfo>

namespace hobbycad {
namespace step_io {

ReadResult readStep(const QString& path)
{
    ReadResult result;

    // Check file exists
    QFileInfo fileInfo(path);
    if (!fileInfo.exists()) {
        result.errorMessage = QStringLiteral("File not found: %1").arg(path);
        return result;
    }

    STEPControl_Reader reader;

    // Read the file
    IFSelect_ReturnStatus status = reader.ReadFile(path.toUtf8().constData());

    if (status != IFSelect_RetDone) {
        switch (status) {
        case IFSelect_RetError:
            result.errorMessage = QStringLiteral("Error reading STEP file");
            break;
        case IFSelect_RetFail:
            result.errorMessage = QStringLiteral("Failed to read STEP file");
            break;
        case IFSelect_RetVoid:
            result.errorMessage = QStringLiteral("No data in STEP file");
            break;
        default:
            result.errorMessage = QStringLiteral("Unknown error reading STEP file");
            break;
        }
        return result;
    }

    // Get statistics
    result.rootCount = reader.NbRootsForTransfer();

    // Transfer all roots
    reader.TransferRoots();

    // Get shapes
    int numShapes = reader.NbShapes();
    result.shapeCount = numShapes;

    for (int i = 1; i <= numShapes; ++i) {
        TopoDS_Shape shape = reader.Shape(i);
        if (!shape.IsNull()) {
            result.shapes.append(shape);
        }
    }

    result.success = !result.shapes.isEmpty();
    if (!result.success && result.errorMessage.isEmpty()) {
        result.errorMessage = QStringLiteral("No valid shapes found in STEP file");
    }

    return result;
}

QList<TopoDS_Shape> readStep(const QString& path, QString* errorMsg)
{
    ReadResult result = readStep(path);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.shapes;
}

WriteResult writeStep(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    StepVersion version)
{
    WriteResult result;

    if (shapes.isEmpty()) {
        result.errorMessage = QStringLiteral("No shapes to write");
        return result;
    }

    STEPControl_Writer writer;

    // Set STEP version
    STEPControl_StepModelType modelType;
    switch (version) {
    case StepVersion::AP203:
        modelType = STEPControl_AsIs;  // AP203 is default
        Interface_Static::SetCVal("write.step.schema", "AP203");
        break;
    case StepVersion::AP214:
        modelType = STEPControl_AsIs;
        Interface_Static::SetCVal("write.step.schema", "AP214CD");
        break;
    case StepVersion::AP242:
        modelType = STEPControl_AsIs;
        Interface_Static::SetCVal("write.step.schema", "AP242DIS");
        break;
    }

    // Transfer shapes to the writer
    for (const TopoDS_Shape& shape : shapes) {
        if (shape.IsNull()) continue;

        IFSelect_ReturnStatus status = writer.Transfer(shape, modelType);
        if (status != IFSelect_RetDone) {
            result.errorMessage = QStringLiteral("Failed to transfer shape to STEP");
            return result;
        }
        result.shapeCount++;
    }

    // Write the file
    IFSelect_ReturnStatus writeStatus = writer.Write(path.toUtf8().constData());

    if (writeStatus != IFSelect_RetDone) {
        result.errorMessage = QStringLiteral("Failed to write STEP file");
        return result;
    }

    result.success = true;
    return result;
}

WriteResult writeStep(
    const QString& path,
    const TopoDS_Shape& shape,
    StepVersion version)
{
    return writeStep(path, QList<TopoDS_Shape>{shape}, version);
}

bool writeStep(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg)
{
    WriteResult result = writeStep(path, shapes, StepVersion::AP214);

    if (errorMsg && !result.success) {
        *errorMsg = result.errorMessage;
    }

    return result.success;
}

bool isStepFile(const QString& path)
{
    QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".step")) ||
           lower.endsWith(QLatin1String(".stp"));
}

QStringList stepExtensions()
{
    return {
        QStringLiteral("step"),
        QStringLiteral("stp"),
        QStringLiteral("STEP"),
        QStringLiteral("STP")
    };
}

}  // namespace step_io
}  // namespace hobbycad
