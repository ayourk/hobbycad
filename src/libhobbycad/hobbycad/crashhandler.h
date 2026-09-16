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
#include <string>

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
    ///          Keep it as simple as possible: avoid allocations,
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

    /// Report a fatal condition detected inside a third-party library.
    ///
    /// Unlike the signal handler, this is called while the process is still
    /// running normally, so the diagnostic the library produced can be
    /// recorded and the user's work saved before anything terminates.
    /// Logs the message and runs the emergency-save callback, then returns
    /// so the caller decides what happens next.
    ///
    /// @param source  Short name of the library, e.g. "libslvs".
    /// @param message The library's own diagnostic text.
    static void reportLibraryFatal(const char* source, const char* message);

    /// A symbolized backtrace of the CURRENT stack, newest frame first.
    ///
    /// @warning When called from a `catch` block the stack has already
    /// unwound, so this shows where the exception was CAUGHT, not where it
    /// was thrown. That is still worth logging (it names the event and
    /// the widget that was being serviced), but do not read it as the
    /// throw site. Capturing that needs interposing on `__cxa_throw`, which
    /// is deliberately not done here.
    ///
    /// Returns a short explanatory string where the platform has no
    /// backtrace facility, never an empty one.
    static std::string captureBacktrace(int skipFrames = 1);

    /// Record a C++ exception that was caught and handled, with a
    /// backtrace, without terminating or saving.
    ///
    /// For failures the program intends to survive, unlike
    /// reportLibraryFatal(), which is for a library that has already
    /// decided the process is finished. The caller decides what happens
    /// next; this only writes the record.
    ///
    /// @param context  Where it was caught, e.g. "QApplication::notify".
    /// @param what     The exception's message.
    static void reportException(const char* context, const char* what);

private:
    CrashHandler() = delete;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_CRASHHANDLER_H
