// =====================================================================
//  src/libhobbycad/document.cpp — Document model
// =====================================================================

#include "hobbycad/document.h"
#include "hobbycad/brep_io.h"

#include <BRepPrimAPI_MakeBox.hxx>

namespace hobbycad {

Document::Document() = default;
Document::~Document() = default;

// ---- File path ------------------------------------------------------

QString Document::filePath() const { return m_filePath; }
bool Document::isNew() const       { return m_filePath.isEmpty(); }
bool Document::isModified() const   { return m_modified; }
void Document::setModified(bool modified) { m_modified = modified; }

// ---- Shapes ---------------------------------------------------------

const QList<TopoDS_Shape>& Document::shapes() const
{
    return m_shapes;
}

void Document::addShape(const TopoDS_Shape& shape)
{
    m_shapes.append(shape);
    m_modified = true;
}

void Document::clear()
{
    m_shapes.clear();
    m_modified = true;
}

// ---- File I/O -------------------------------------------------------

bool Document::loadBrep(const QString& path)
{
    QString err;
    auto shapes = brep_io::readBrep(path, &err);
    if (shapes.isEmpty() && !err.isEmpty()) {
        return false;
    }

    m_shapes   = shapes;
    m_filePath = path;
    m_modified = false;
    return true;
}

bool Document::saveBrep(const QString& path)
{
    QString savePath = path.isEmpty() ? m_filePath : path;
    if (savePath.isEmpty()) {
        return false;
    }

    QString err;
    if (!brep_io::writeBrep(savePath, m_shapes, &err)) {
        return false;
    }

    m_filePath = savePath;
    m_modified = false;
    return true;
}

void Document::createTestSolid()
{
    m_shapes.clear();

    // Create a 50x30x20 mm box centered at the origin
    BRepPrimAPI_MakeBox boxMaker(
        gp_Pnt(-25.0, -15.0, -10.0),
        50.0, 30.0, 20.0
    );
    boxMaker.Build();

    if (boxMaker.IsDone()) {
        m_shapes.append(boxMaker.Shape());
    }

    m_filePath.clear();
    m_modified = false;
}

}  // namespace hobbycad

