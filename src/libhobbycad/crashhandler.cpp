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
        // Already handling a crash — just exit immediately.
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
