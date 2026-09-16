// =====================================================================
//  src/libhobbycad/occt_viewer_probe.cpp — ask OCCT directly
// =====================================================================
//
//  The OpenGL version number is a *proxy* for the question that actually
//  matters: "can OCCT bring up a 3D viewport on this machine?" A proxy can
//  be wrong in both directions.
//
//    - Too strict: a driver that refuses an explicit 3.3 Core request but
//      supplies a usable compatibility context at a higher version. That
//      sends capable hardware to Reduced Mode.
//    - Too lenient: a stack that reports 3.3+ where OCCT still cannot
//      initialize: no GLX on a remote display, a container missing the GL
//      libraries, a driver advertising more than it delivers. That picks
//      Full Mode and fails later, further from the cause.
//
//  So ask the component that actually has to succeed. The version string is
//  what the driver *says*; InitContext() is what OCCT can *do*.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/opengl_info.h"

#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Standard_Failure.hxx>
#include "hobbycad/occt_failure.h"

#include <string>

namespace hobbycad {

void probeOcctViewer(OpenGLInfo& info)
{
    info.occtViewerProbed = false;
    info.occtViewerWorks  = false;

    try {
        // `false` = do not initialize immediately. InitContext() below is the
        // call that actually creates a GL context, and its return value is
        // what we want rather than a throw out of the constructor.
        Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver =
            new OpenGl_GraphicDriver(display, false);

        info.occtViewerProbed = true;
        info.occtViewerWorks  = (driver->InitContext() == true);

        if (!info.occtViewerWorks && info.errorMessage.empty()) {
            info.errorMessage =
                "OCCT could not initialize an OpenGL context on this display";
        }
    } catch (const Standard_Failure& e) {
        // OCCT throws its own exception type. OCCT 8.0 gives Standard_Failure
        // an std::exception base; 7.x does not, so catch it by its own name to
        // work with both.
        info.occtViewerProbed = true;
        info.occtViewerWorks  = false;
        info.errorMessage =
            std::string("OCCT viewer probe failed: ") + occtFailureMessage(e);
    } catch (...) {
        // A probe must never be the thing that takes the program down; its
        // whole purpose is to ask about a machine that may be misconfigured.
        // An unknown failure is simply a "no".
        info.occtViewerProbed = true;
        info.occtViewerWorks  = false;
        info.errorMessage     = "OCCT viewer probe failed with an unknown error";
    }
}

}  // namespace hobbycad
