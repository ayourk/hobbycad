// =====================================================================
//  src/hobbycad/softcrash.h — survive a mid-session exception
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Aaron, 2026-08-27: "It would be great if we could do a 'soft' crash
//  instead of a hard crash."
//
//  A hard crash is std::terminate: no save, no message, a core dump if
//  the user is lucky and nothing if not. Everything unsaved is gone and
//  the only evidence is an exit code.
//
//  A soft crash keeps the process in charge of its own ending:
//    1. log the exception with a backtrace,
//    2. try to keep running with less: drop the 3D viewport and carry
//       on in Reduced Mode, since the 2D workspace needs no OCCT,
//    3. if that is not possible, or it keeps happening, emergency-save
//       and exit cleanly with a message.
//
//  Where this catches matters. Qt does not allow exceptions to propagate
//  through the event loop (doing so is undefined), so the catch has to
//  be inside notify(), which is the last frame Qt controls before an
//  event handler runs. That is "where it happens" in the only sense the
//  event loop permits.
//
#ifndef HOBBYCAD_SOFTCRASH_H
#define HOBBYCAD_SOFTCRASH_H

#include <QApplication>

#include <functional>

namespace hobbycad {

/// QApplication that turns an escaped exception into a soft crash.
class SoftCrashApplication : public QApplication {
public:
    SoftCrashApplication(int& argc, char** argv)
        : QApplication(argc, argv) {}

    /// Called when an event handler threw and the viewport should be
    /// abandoned. Return true if the application kept running with the 3D
    /// viewport removed; false if it could not, and the app should stop.
    void setDegradeHandler(std::function<bool()> handler)
    { m_degrade = std::move(handler); }

    /// Called just before a clean exit, to persist whatever can be saved.
    void setEmergencySave(std::function<void()> handler)
    { m_save = std::move(handler); }

    /// How many failures to absorb before giving up. One retry is enough
    /// to distinguish a one-off from a viewport that is now failing on
    /// every repaint, and a loop of "degraded" popups is its own bug.
    void setFailureBudget(int budget) { m_budget = budget; }

    bool notify(QObject* receiver, QEvent* event) override;

private:
    /// Log, then degrade or exit. Never rethrows.
    void handleEscapedException(const char* what, QObject* receiver,
                                QEvent* event);

    std::function<bool()> m_degrade;
    std::function<void()> m_save;
    int  m_budget    = 1;
    int  m_failures  = 0;
    bool m_degraded  = false;
    bool m_stopping  = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SOFTCRASH_H
