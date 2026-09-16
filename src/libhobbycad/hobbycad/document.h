// =====================================================================
//  src/libhobbycad/hobbycad/document.h — Document model
// =====================================================================
//
//  A Document holds one or more TopoDS_Shape objects and tracks the
//  file path.  It provides the model layer between file I/O and the
//  GUI / CLI.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_DOCUMENT_H
#define HOBBYCAD_DOCUMENT_H

#include "core.h"

#include <TopoDS_Shape.hxx>

#include <string>

#include "body.h"
#include <vector>

namespace hobbycad {

class HOBBYCAD_EXPORT Document {
public:
    Document();
    ~Document();

    // ---- File path --------------------------------------------------

    /// Path to the file on disk, or empty if unsaved.
    std::string filePath() const;

    /// True if the document has never been saved.
    bool isNew() const;

    /// True if the document has been modified since the last save.
    bool isModified() const;

    /// Mark the document as modified.
    void setModified(bool modified = true);

    // ---- Bodies -----------------------------------------------------

    /// All bodies in the document, with their identity intact.
    const std::vector<BodyData>& bodies() const;

    /// Add a body, preserving the id, name and design it already carries.
    /// An id of -1 gets the next free one. Marks the document modified.
    void addBody(const BodyData& body);

    /// Add a bare shape, assigning it a fresh id and a default name.
    /// Marks the document modified.
    int addShape(const TopoDS_Shape& shape);

    /// Replace every body. Marks the document modified.
    void setBodies(const std::vector<BodyData>& bodies);

    /// Lowest unused body id.
    int nextBodyId() const;

    /// Remove all shapes.  Marks document as modified.
    void clear();

    // ---- File I/O ---------------------------------------------------

    /// Load a BREP file.  Replaces current shapes.
    /// Returns true on success.
    bool loadBrep(const std::string& path);

    /// Save shapes to a BREP file.
    /// If path is empty, uses the current filePath().
    /// Returns true on success.
    bool saveBrep(const std::string& path = {});

private:
    std::string       m_filePath;
    bool              m_modified = false;
    std::vector<BodyData> m_bodies;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DOCUMENT_H
