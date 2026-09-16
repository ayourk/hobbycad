// =====================================================================
//  src/libhobbycad/hobbycad/body.h — solid body record
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Extracted from project.h so Document can hold bodies too. Both the
//  live document and the saved project describe the same thing, and
//  when only one of them had identity, the sync between them had to
//  carry id, name and design across BY POSITION, which is wrong the
//  moment a body is deleted or reordered.
//
#ifndef HOBBYCAD_BODY_H
#define HOBBYCAD_BODY_H

#include <string>

#include <TopoDS_Shape.hxx>

namespace hobbycad {

/// One solid body in a design.
///
/// Bodies were once a bare vector of TopoDS_Shape, which meant they had no
/// identity: no id to name their file, no name to show in the browser, and
/// nowhere to record which design they belong to. Everything about a body
/// was its position in a list, with the same consequences the sketches had
/// before they carried feature ids.
struct BodyData {
    int id = -1;              ///< Stable identity; names the file
    std::string name;         ///< User-visible, renameable
    int designId = 0;         ///< 0 = unassigned; filled in when added
    TopoDS_Shape shape;
};

/// Lowest unused body id in a collection of bodies.
template <typename Bodies>
int nextBodyIdIn(const Bodies& bodies)
{
    int next = 1;
    for (const BodyData& b : bodies) {
        if (b.id >= next) next = b.id + 1;
    }
    return next;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_BODY_H
