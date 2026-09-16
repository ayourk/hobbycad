// =====================================================================
//  src/libhobbycad/core.cpp — Library initialization
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "hobbycad/core.h"
#include <hobbycad/sketch/solver.h>

// OCCT kernel headers
#include <Standard_Version.hxx>

namespace hobbycad {

const char* version()
{
    return "0.0.1";
}

bool initialize()
{
    // Give the constraint solver somewhere to report a fatal condition.
    // Without this a fault inside libslvs ends the process with its
    // diagnostic written to a stderr the application never reads.
    sketch::installSolverFatalHandler();

    // Phase 0: no other library-wide state to set up yet.
    // Future phases will register OCCT XDE drivers, initialize
    // the plugin system, etc.
    return true;
}

void shutdown()
{
    // Phase 0: nothing to tear down.
}

bool hasConstraintSolver()
{
#ifdef HAVE_SLVS
    return true;
#else
    return false;
#endif
}

}  // namespace hobbycad
