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

std::string Document::filePath() const { return m_filePath; }
bool Document::isNew() const       { return m_filePath.empty(); }
bool Document::isModified() const   { return m_modified; }
void Document::setModified(bool modified) { m_modified = modified; }

// ---- Shapes ---------------------------------------------------------

const std::vector<TopoDS_Shape>& Document::shapes() const
{
    return m_shapes;
}

void Document::addShape(const TopoDS_Shape& shape)
{
    m_shapes.push_back(shape);
    m_modified = true;
}

void Document::clear()
{
    m_shapes.clear();
    m_filePath.clear();
    m_modified = false;
}

// ---- File I/O -------------------------------------------------------

bool Document::loadBrep(const std::string& path)
{
    std::string err;
    auto shapes = brep_io::readBrep(path, &err);
    if (shapes.empty() && !err.empty()) {
        return false;
    }

    m_shapes   = shapes;
    m_filePath = path;
    m_modified = false;
    return true;
}

bool Document::saveBrep(const std::string& path)
{
    std::string savePath = path.empty() ? m_filePath : path;
    if (savePath.empty()) {
        return false;
    }

    std::string err;
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

    // Create a 20x20x20 mm cube sitting on the XY plane at the origin.
    // Bottom face centered on X/Y at Z=0, top face at Z=20.
    BRepPrimAPI_MakeBox boxMaker(
        gp_Pnt(-10.0, -10.0, 0.0),
        20.0, 20.0, 20.0
    );
    boxMaker.Build();

    if (boxMaker.IsDone()) {
        m_shapes.push_back(boxMaker.Shape());
    }

    m_filePath.clear();
    m_modified = false;
}

}  // namespace hobbycad
