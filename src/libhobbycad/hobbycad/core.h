// =====================================================================
//  src/libhobbycad/hobbycad/core.h — Library initialization and export macros
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CORE_H
#define HOBBYCAD_CORE_H

// ---- Export macro ----------------------------------------------------
//
// When building libhobbycad as a shared library, HOBBYCAD_SHARED and
// HOBBYCAD_BUILDING are defined.  Consumers linking against the shared
// library only see HOBBYCAD_SHARED (set as a PUBLIC compile definition).

#if defined(HOBBYCAD_SHARED)
  #if defined(HOBBYCAD_BUILDING)
    #if defined(_WIN32)
      #define HOBBYCAD_EXPORT __declspec(dllexport)
    #else
      #define HOBBYCAD_EXPORT __attribute__((visibility("default")))
    #endif
  #else
    #if defined(_WIN32)
      #define HOBBYCAD_EXPORT __declspec(dllimport)
    #else
      #define HOBBYCAD_EXPORT
    #endif
  #endif
#else
  #define HOBBYCAD_EXPORT
#endif

#include <string>

namespace hobbycad {

/// Library version string (e.g., "0.0.1").
HOBBYCAD_EXPORT const char* version();

/// Initialize the OCCT kernel and any other library-wide state.
/// Call once at application startup before using other functions.
/// Returns true on success.
HOBBYCAD_EXPORT bool initialize();

/// Shut down the library and release resources.
/// Call once at application exit.
HOBBYCAD_EXPORT void shutdown();

// =====================================================================
//  Library Capabilities
// =====================================================================

/// Check if constraint solving is available.
/// Returns true if this library was built with libslvs support.
///
/// Downstream developers should use this to determine if parametric
/// constraint solving is available before using sketch::Solver.
///
/// Example:
/// @code
///     if (hobbycad::hasConstraintSolver()) {
///         // Enable constraint tools in UI
///         sketch::Solver solver;
///         solver.solve(entities, constraints);
///     } else {
///         // Disable constraint UI or show warning
///     }
/// @endcode
HOBBYCAD_EXPORT bool hasConstraintSolver();

}  // namespace hobbycad

#endif  // HOBBYCAD_CORE_H

