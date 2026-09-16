// =====================================================================
//  tests/opengl/gate.cpp — the 3D viewport gate
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Aaron, 2026-08-27: "If OCCT won't work, that gives the best answer."
//  These assert that nothing else can override it.
#include <hobbycad/opengl_info.h>
#include <cstdio>

using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static OpenGLInfo gl(int major, int minor) {
    OpenGLInfo i;
    i.contextCreated = true;
    i.majorVersion = major;
    i.minorVersion = minor;
    return i;
}

int main() {
    std::printf("3D viewport gate\n");

    // OCCT succeeded: the viewport runs, whatever the version says.
    {
        OpenGLInfo i = gl(3, 1);          // BELOW the old 3.3 minimum
        i.occtViewerProbed = true;
        i.occtViewerWorks  = true;
        check(!i.meetsMinimum(), "the reported version is below 3.3");
        check(i.canRunViewport(),
              "but OCCT succeeded, so the viewport runs anyway");
    }

    // OCCT failed: no viewport, however good the driver claims to be.
    {
        OpenGLInfo i = gl(4, 6);
        i.occtViewerProbed = true;
        i.occtViewerWorks  = false;
        check(i.meetsMinimum(), "the driver reports 4.6");
        check(!i.canRunViewport(),
              "but OCCT failed, so a high version does NOT override it");
    }

    // An exception during the probe is a definitive no: probeOcctViewer()
    // records probed=true, works=false in both catch blocks, and that must
    // not fall back to the version test.
    {
        OpenGLInfo i = gl(4, 6);
        i.occtViewerProbed = true;        // as both catch blocks leave it
        i.occtViewerWorks  = false;
        i.errorMessage = "OCCT viewer probe failed: ...";
        check(!i.canRunViewport(),
              "a thrown probe means Reduced Mode, not a version fallback");
    }

    // Never probed: answer no rather than guessing from a version string.
    // Degrading to Reduced Mode is the safe direction: the 2D workspace
    // works, while a viewport that cannot initialize does not.
    {
        OpenGLInfo i = gl(4, 6);
        check(!i.occtViewerProbed, "the probe was not run");
        check(!i.canRunViewport(),
              "so the gate says no, even though the version would pass");
    }

    // No context at all.
    {
        OpenGLInfo i;
        check(!i.canRunViewport(), "a blank info cannot run the viewport");
        check(!i.meetsMinimum(), "and does not meet the version minimum");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
