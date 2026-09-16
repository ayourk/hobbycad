// =====================================================================
//  src/libhobbycad/document.cpp — Document model
// =====================================================================

#include "hobbycad/document.h"
#include "hobbycad/brep_io.h"


namespace hobbycad {

Document::Document() = default;
Document::~Document() = default;

// ---- File path ------------------------------------------------------

std::string Document::filePath() const { return m_filePath; }
bool Document::isNew() const       { return m_filePath.empty(); }
bool Document::isModified() const   { return m_modified; }
void Document::setModified(bool modified) { m_modified = modified; }

// ---- Bodies ---------------------------------------------------------

const std::vector<BodyData>& Document::bodies() const
{
    return m_bodies;
}

int Document::nextBodyId() const
{
    return nextBodyIdIn(m_bodies);
}

void Document::addBody(const BodyData& body)
{
    // An id that is already set is KEPT. That is the point of this
    // overload: a body loaded from a project arrives with its identity,
    // and the document must not reassign it, or the round trip back to the
    // project would rename every file.
    BodyData added = body;
    if (added.id < 0) {
        added.id = nextBodyId();
    }
    if (added.name.empty()) {
        added.name = "Body" + std::to_string(added.id);
    }
    if (added.designId <= 0) {
        added.designId = 1;
    }
    m_bodies.push_back(added);
    m_modified = true;
}

int Document::addShape(const TopoDS_Shape& shape)
{
    BodyData body;
    body.shape = shape;
    addBody(body);
    return m_bodies.back().id;
}

void Document::setBodies(const std::vector<BodyData>& bodies)
{
    m_bodies = bodies;
    m_modified = true;
}

void Document::clear()
{
    m_bodies.clear();
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

    setBodies({});
    for (const TopoDS_Shape& shape : shapes) {
        addShape(shape);
    }
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
    std::vector<TopoDS_Shape> shapes;
    shapes.reserve(m_bodies.size());
    for (const BodyData& b : m_bodies) {
        shapes.push_back(b.shape);
    }
    if (!brep_io::writeBrep(savePath, shapes, &err)) {
        return false;
    }

    m_filePath = savePath;
    m_modified = false;
    return true;
}

}  // namespace hobbycad
