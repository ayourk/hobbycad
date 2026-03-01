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

    // ---- Shapes -----------------------------------------------------

    /// All shapes in the document.
    const std::vector<TopoDS_Shape>& shapes() const;

    /// Add a shape to the document.  Marks document as modified.
    void addShape(const TopoDS_Shape& shape);

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

    /// Create a default test solid (a box) for initial display.
    /// Clears existing shapes and adds a single box.
    void createTestSolid();

private:
    std::string       m_filePath;
    bool              m_modified = false;
    std::vector<TopoDS_Shape> m_shapes;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DOCUMENT_H
