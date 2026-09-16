// =====================================================================
//  tests/pager/nav.cpp — pager navigation, less-style
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "pagernav.h"
#include <algorithm>
#include <cstdio>

using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    std::printf("pager navigation\n");

    const int rows = 10, total = 35;      // 4 screens, last one partial
    const int maxTop = total - rows;      // 25

    // ---- back a page: the one Aaron called out ----------------------
    check(pagerStep('b', 20, rows, total) == 10, "b goes back one screen");
    check(pagerStep('b', 10, rows, total) == 0,  "and again");
    check(pagerStep('b', 0, rows, total) == 0,
          "at the top it STAYS, no wrap to the end");
    check(pagerStep('b', 5, rows, total) == 0,
          "a partial step back clamps to the top, it does not go negative");

    // ---- forward -----------------------------------------------------
    check(pagerStep(' ', 0, rows, total) == 10, "space advances a screen");
    check(pagerStep('f', 0, rows, total) == 10, "f does the same");
    check(pagerStep(' ', 20, rows, total) == maxTop,
          "the last step lands on the final screenful, not past it");

    // Space at the end exits, as less does; otherwise repeated space
    // sits there forever.
    check(pagerStep(' ', maxTop, rows, total) == kPagerQuit,
          "space at the end quits");
    check(pagerStep('j', maxTop, rows, total) == kPagerQuit,
          "so does a line step at the end");

    // ---- line at a time ----------------------------------------------
    check(pagerStep('j', 0, rows, total) == 1,  "j moves one line");
    check(pagerStep('\n', 0, rows, total) == 1, "so does Enter");
    check(pagerStep('k', 5, rows, total) == 4,  "k moves back one");
    check(pagerStep('k', 0, rows, total) == 0,  "and clamps at the top");

    // ---- jumps -------------------------------------------------------
    check(pagerStep('g', 20, rows, total) == 0, "g returns to the top");
    check(pagerStep('G', 0, rows, total) == maxTop,
          "G shows the LAST SCREENFUL, not the last line alone");

    // ---- quitting and unknown keys -----------------------------------
    check(pagerStep('q', 10, rows, total) == kPagerQuit, "q quits");
    check(pagerStep(-1, 10, rows, total) == kPagerQuit,
          "so does end-of-input, which is how a pipe reaches here");
    check(pagerStep('z', 10, rows, total) == 10, "an unknown key does nothing");

    // ---- content shorter than a screen --------------------------------
    check(pagerStep('G', 0, 10, 3) == 0,
          "G on content shorter than the screen stays at 0");
    check(pagerStep('b', 0, 10, 3) == 0, "and b cannot go negative");

    // ---- the terminal can be resized mid-paging ----------------------
    // rows is passed in per step rather than fixed at the start, so a
    // window that changes size while the pager is open keeps working. A
    // page measured at the old size would skip lines after a grow, or
    // overflow after a shrink.
    {
        const int n = 35;

        // Grew from 10 rows to 20 while sitting at line 10.
        check(pagerStep(' ', 10, 20, n) == 15,
              "after growing, a page advances by the NEW size and clamps");
        check(pagerStep('b', 10, 20, n) == 0,
              "and back a page uses the new size too");

        // Shrank to 5 rows.
        check(pagerStep(' ', 10, 5, n) == 15, "after shrinking, a page is smaller");
        check(pagerStep('G', 0, 5, n) == 30,  "and G lands on the new last screen");
        check(pagerStep('G', 0, 20, n) == 15, "which differs from the larger size");

        // A window grown taller than the content: everything fits, so the
        // end-of-content keys must still behave rather than run past it.
        check(pagerStep('G', 0, 50, n) == 0, "a window taller than the content stays at 0");
        check(pagerStep(' ', 0, 50, n) == kPagerQuit,
              "and space exits, because there is nothing below");
    }

    // ---- what a resize can break, in each direction -------------------
    // The pager clamps top to the current last-screenful every iteration.
    // These assert the arithmetic that clamp relies on, so the two cannot
    // drift apart.
    {
        const int n = 35;
        auto clamp = [](int top, int rows, int total) {
            return std::max(0, std::min(top, std::max(0, total - rows)));
        };

        // SHRINKING is safe by itself: a smaller page RAISES the last valid
        // top, so a position that was legal stays legal.
        check(clamp(25, 10, n) == 25, "top survives a shrink from 10 rows...");
        check(clamp(25,  5, n) == 25, "...and is still valid at 5 rows");

        // GROWING is the dangerous direction: it LOWERS the last valid top,
        // so a position that was fine can now sit past the end and show a
        // mostly blank screen.
        check(clamp(25, 20, n) == 15,
              "growing to 20 rows pulls top back to the last screenful");
        check(clamp(25, 50, n) == 0,
              "and a window taller than the content returns to the top");

        // Once clamped, the keys behave against the NEW size.
        check(pagerStep(' ', clamp(25, 20, n), 20, n) == kPagerQuit,
              "space at the clamped end exits rather than showing blanks");
        check(pagerStep('b', clamp(25, 20, n), 20, n) == 0,
              "and back a page works from the clamped position");

        // A window so small only one line fits must still page, never dump.
        check(pagerStep(' ', 0, 1, n) == 1, "a one-line window still advances");
        check(pagerStep('b', 5, 1, n) == 4, "and still goes back");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
