// =====================================================================
//  src/libhobbycad/crashhandler.cpp — Graceful crash handling
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/crashhandler.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <ctime>
#include <string>

// execinfo.h is a glibc extension. musl, MSVC and macOS-without-it fall
// back to a message rather than a silent empty trace: a report that just
// omits the stack looks like the capture succeeded and found nothing.
#if defined(__has_include)
#  if __has_include(<execinfo.h>)
#    define HOBBYCAD_HAS_EXECINFO 1
#  endif
#endif
#ifdef HOBBYCAD_HAS_EXECINFO
#  include <execinfo.h>
#endif

#ifdef _WIN32
#include <io.h>
#define WRITE_FD _write
#else
#include <unistd.h>
#define WRITE_FD ::write
#endif

namespace {

// ---- Async-signal-safe write helpers -----------------------------------
//
// Inside a signal handler we cannot use printf, std::cerr, qWarning, etc.
// Only write() and _exit() are safe.  These helpers format minimal
// diagnostic output using only stack buffers and write().

void safeWrite(const char* msg)
{
    if (!msg) return;
    // STDERR_FILENO is 2 on all POSIX systems
    WRITE_FD(2, msg, static_cast<unsigned int>(std::strlen(msg)));
}

void safeWriteInt(int value)
{
    char buf[16];
    int pos = 0;
    if (value < 0) {
        buf[pos++] = '-';
        value = -value;
    }
    // Convert digits in reverse
    char digits[12];
    int ndigits = 0;
    if (value == 0) {
        digits[ndigits++] = '0';
    } else {
        while (value > 0 && ndigits < 11) {
            digits[ndigits++] = '0' + (value % 10);
            value /= 10;
        }
    }
    for (int i = ndigits - 1; i >= 0; --i) {
        buf[pos++] = digits[i];
    }
    WRITE_FD(2, buf, pos);
}

const char* signalName(int sig)
{
    switch (sig) {
    case SIGABRT: return "SIGABRT (abort)";
    case SIGSEGV: return "SIGSEGV (segmentation fault)";
    case SIGFPE:  return "SIGFPE (floating-point exception)";
#ifdef SIGBUS
    case SIGBUS:  return "SIGBUS (bus error)";
#endif
    default:      return "unknown signal";
    }
}

// ---- Global state (async-signal-safe) ----------------------------------

std::atomic<bool> g_installed{false};
std::atomic<bool> g_handling{false};  // re-entrancy guard

// The emergency-save callback.  Not async-signal-safe by nature, but
// called as a best-effort measure before exit.
std::function<void()> g_emergencySave;

// Crash log path (static buffer, set before any crash).
char g_crashLogPath[1024] = {};

// ---- Signal handler ----------------------------------------------------

void crashSignalHandler(int sig)
{
    // Guard against re-entrancy (e.g., crash inside the handler).
    bool expected = false;
    if (!g_handling.compare_exchange_strong(expected, true)) {
        // Already handling a crash; just exit immediately.
        _exit(128 + sig);
    }

    safeWrite("\n");
    safeWrite("=====================================================\n");
    safeWrite("  HobbyCAD received fatal signal: ");
    safeWrite(signalName(sig));
    safeWrite(" (");
    safeWriteInt(sig);
    safeWrite(")\n");
    safeWrite("=====================================================\n");
    safeWrite("\n");
    safeWrite("This is a bug.  Please report it at:\n");
    safeWrite("  https://github.com/ayourk/hobbycad/issues\n");
    safeWrite("\n");

    // Write crash info to log file if configured.
    if (g_crashLogPath[0] != '\0') {
        // Open with raw file descriptor for async-signal safety.
        FILE* logFile = std::fopen(g_crashLogPath, "a");
        if (logFile) {
            std::fprintf(logFile, "HobbyCAD crash: signal %d (%s)\n",
                         sig, signalName(sig));
            std::fclose(logFile);
        }
    }

    // Best-effort emergency save.
    if (g_emergencySave) {
        safeWrite("Attempting emergency save...\n");
        // This is technically unsafe in a signal handler, but it's our
        // best chance to save the user's work.  If it crashes again,
        // the re-entrancy guard above will catch it.
        g_emergencySave();
        safeWrite("Emergency save completed.\n");
    }

    safeWrite("Exiting.\n");

    // Restore default handler and re-raise to get a core dump if enabled.
    std::signal(sig, SIG_DFL);
    std::raise(sig);

    // If we somehow get here, force exit.
    _exit(128 + sig);
}

}  // anonymous namespace

namespace hobbycad {

void CrashHandler::install()
{
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) {
        return;  // Already installed.
    }

    std::signal(SIGABRT, crashSignalHandler);
    std::signal(SIGSEGV, crashSignalHandler);
    std::signal(SIGFPE,  crashSignalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS,  crashSignalHandler);
#endif
}

void CrashHandler::setEmergencySave(std::function<void()> callback)
{
    g_emergencySave = std::move(callback);
}

std::string CrashHandler::captureBacktrace(int skipFrames)
{
#ifdef HOBBYCAD_HAS_EXECINFO
    void*  frames[64];
    const int n = ::backtrace(frames, 64);
    if (n <= 0) {
        return "  (backtrace unavailable: no frames captured)\n";
    }
    char** symbols = ::backtrace_symbols(frames, n);
    if (!symbols) {
        return "  (backtrace unavailable: symbolization failed)\n";
    }

    std::string out;
    for (int i = (skipFrames > 0 ? skipFrames : 0); i < n; ++i) {
        out += "  ";
        out += symbols[i] ? symbols[i] : "???";
        out += '\n';
    }
    ::free(symbols);   // backtrace_symbols() mallocs the whole block
    if (out.empty()) {
        out = "  (backtrace unavailable: every frame was skipped)\n";
    }
    return out;
#else
    (void)skipFrames;
    return "  (backtrace unavailable: no execinfo.h on this platform)\n";
#endif
}

void CrashHandler::reportException(const char* context, const char* what)
{
    // +1 to drop this function's own frame.
    const std::string trace = captureBacktrace(2);

    char when[32] = "unknown time";
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);

    std::string report;
    report += "\n=== handled exception ===\n";
    report += "time    : "; report += when;                       report += '\n';
    report += "context : "; report += (context ? context : "?");  report += '\n';
    report += "message : "; report += (what ? what : "(none)");   report += '\n';
    report += "stack   : (caught here, NOT the throw site; the stack has\n"
              "           already unwound by the time a catch block runs)\n";
    report += trace;

    std::fputs(report.c_str(), stderr);

    // Same log the signal handler and reportLibraryFatal() use, so one file
    // holds every kind of failure in the order they happened.
    if (g_crashLogPath[0] != '\0') {
        if (FILE* logFile = std::fopen(g_crashLogPath, "a")) {
            std::fputs(report.c_str(), logFile);
            std::fflush(logFile);
            std::fclose(logFile);
        }
    }
}

void CrashHandler::reportLibraryFatal(const char* source, const char* message)
{
    // Not a signal handler: the process is still healthy here, so ordinary
    // I/O is safe.  Still guard against re-entry, since the emergency save
    // could itself fault.
    static bool reporting = false;
    if (reporting) return;
    reporting = true;

    if (g_crashLogPath[0] != '\0') {
        FILE* logFile = std::fopen(g_crashLogPath, "a");
        if (logFile) {
            std::fprintf(logFile, "HobbyCAD: fatal error reported by %s:\n%s\n",
                         source ? source : "(unknown)",
                         message ? message : "(no message)");
            std::fclose(logFile);
        }
    }

    std::fprintf(stderr, "\nHobbyCAD: %s reported a fatal error:\n  %s\n",
                 source ? source : "(unknown)",
                 message ? message : "(no message)");

    if (g_emergencySave) {
        std::fprintf(stderr, "HobbyCAD: attempting emergency save...\n");
        g_emergencySave();
        std::fprintf(stderr, "HobbyCAD: emergency save completed.\n");
    }

    reporting = false;
}

void CrashHandler::setCrashLogPath(const char* path)
{
    if (path) {
        std::strncpy(g_crashLogPath, path, sizeof(g_crashLogPath) - 1);
        g_crashLogPath[sizeof(g_crashLogPath) - 1] = '\0';
    } else {
        g_crashLogPath[0] = '\0';
    }
}

}  // namespace hobbycad
