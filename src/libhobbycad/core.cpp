// =====================================================================
//  src/libhobbycad/core.cpp -- Library initialization
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/core.h"

// OCCT kernel headers
#include <Standard_Version.hxx>

namespace hobbycad {

const char* version()
{
    return "0.0.1";
}

bool initialize()
{
    // Phase 0: no library-wide state to set up yet.
    // Future phases will register OCCT XDE drivers, initialize
    // the plugin system, etc.
    return true;
}

void shutdown()
{
    // Phase 0: nothing to tear down.
}

}  // namespace hobbycad
