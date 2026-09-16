// =====================================================================
//  src/libhobbycad/hobbycad/sketch/align.h — aligning and distributing items
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#ifndef HOBBYCAD_SKETCH_ALIGN_H
#define HOBBYCAD_SKETCH_ALIGN_H

#include "../core.h"
#include "../types.h"
#include "../geometry/types.h"
#include "undo.h"   // AlignmentType

#include <vector>

namespace hobbycad {
namespace sketch {

/// One thing alignment moves as a unit: a whole group, or a lone entity.
struct AlignItem {
    std::vector<int> ids;          ///< the entities that move together
    geometry::BoundingBox bounds;  ///< their combined extent
};

/// The translation each item needs (parallel to `items`, zero for items
/// that stay). Left/Right/Top/Bottom bring every item's edge to the
/// selection's outermost such edge; the center modes bring centers to the
/// mean center; the distribute modes space the centers of the middle items
/// evenly between the two outermost, which stay. Fewer than two items (three
/// for distribution) yields all-zero offsets.
HOBBYCAD_EXPORT std::vector<Point2D> alignOffsets(const std::vector<AlignItem>& items,
                                                  AlignmentType type);

}  // namespace sketch
}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCH_ALIGN_H
