// =====================================================================
//  src/libhobbycad/hobbycad/crashhandler.h — Graceful crash handling
// =====================================================================
//
//  Installs signal handlers for fatal signals (SIGABRT, SIGSEGV, etc.)
//  so the application can log a meaningful message, attempt emergency
//  auto-save, and exit cleanly instead of just crashing.
//
//  Usage:
//      hobbycad::CrashHandler::install();
//
//      // Optionally register a save callback (e.g., from the GUI):
//      hobbycad::CrashHandler::setEmergencySave([]() {
//          // Save current document...
//      });
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CRASHHANDLER_H
#define HOBBYCAD_CRASHHANDLER_H

#include "core.h"

#include <functional>

namespace hobbycad {

/// Graceful crash handler for fatal signals.
///
/// Catches SIGABRT, SIGSEGV, SIGFPE, and SIGBUS (on platforms that
/// support them) and performs a controlled shutdown rather than
/// producing a raw core dump.  On crash:
///
///   1. Writes a diagnostic message to stderr
///   2. Calls the emergency-save callback (if registered)
///   3. Exits with code 128 + signal number (Unix convention)
///
/// The handler is async-signal-safe: it uses only write() and _exit()
/// inside the signal handler, with the emergency-save callback called
/// via a best-effort approach.
class HOBBYCAD_EXPORT CrashHandler {
public:
    /// Install signal handlers for SIGABRT, SIGSEGV, SIGFPE, SIGBUS.
    /// Call once early in main(), after library initialization.
    /// Safe to call multiple times (subsequent calls are no-ops).
    static void install();

    /// Register a callback to be invoked on crash for emergency save.
    ///
    /// @warning This callback runs inside a signal handler context.
    ///          Keep it as simple as possible — avoid allocations,
    ///          Qt signals/slots, or complex I/O.  A simple fwrite()
    ///          of already-prepared data is ideal.
    ///
    /// @param callback  Function to call on crash, or nullptr to clear.
    static void setEmergencySave(std::function<void()> callback);

    /// Set the path where a crash log file will be written.
    /// If not set, crash information is only written to stderr.
    ///
    /// @param path  Absolute path to the crash log file.
    static void setCrashLogPath(const char* path);

private:
    CrashHandler() = delete;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CRASHHANDLER_H
