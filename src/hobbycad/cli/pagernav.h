// =====================================================================
//  src/hobbycad/cli/pagernav.h — where a pager keypress moves to
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Split out from the pager so the index arithmetic can be tested without
//  a terminal. Clamping at both ends, and landing in the right place for
//  G, is where paging bugs live, and "back a page" in particular has to
//  be right, because a user only reaches for it after seeing something go
//  past.
//
#ifndef HOBBYCAD_PAGERNAV_H
#define HOBBYCAD_PAGERNAV_H

#include <algorithm>

namespace hobbycad {

/// Sentinel returned when the key means "stop paging".
constexpr int kPagerQuit = -1;

/// New top line for a keypress, or kPagerQuit.
///
/// @param key    the byte read from the terminal
/// @param top    current first visible line, 0-based
/// @param rows   visible lines per screen
/// @param total  total lines
inline int pagerStep(int key, int top, int rows, int total)
{
    const bool atEnd = (top + rows) >= total;
    const int maxTop = std::max(0, total - rows);

    switch (key) {
    case 'q': case 'Q': case -1:
        return kPagerQuit;

    case ' ': case 'f':
        // At the end, space leaves: the same as less, and what a user
        // pressing space repeatedly expects to happen eventually.
        return atEnd ? kPagerQuit : std::min(top + rows, maxTop);

    case '\r': case '\n': case 'j':
        return atEnd ? kPagerQuit : std::min(top + 1, maxTop);

    case 'b':
        // Back a page. Clamped at 0 rather than wrapping: wrapping to the
        // end would look like the output had been reloaded.
        return std::max(0, top - rows);

    case 'k':
        return std::max(0, top - 1);

    case 'g':
        return 0;

    case 'G':
        // The LAST screenful, not the last line; landing with one line
        // visible and the rest blank is not what "go to end" means.
        return maxTop;

    default:
        return top;      // unknown keys do nothing, as in less
    }
}

}  // namespace hobbycad

#endif  // HOBBYCAD_PAGERNAV_H
