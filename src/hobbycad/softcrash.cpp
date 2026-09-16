// =====================================================================
//  src/hobbycad/softcrash.cpp — survive a mid-session exception
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "softcrash.h"

#include <hobbycad/crashhandler.h>

#include <QEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <Standard_Failure.hxx>
#include "hobbycad/occt_failure.h"

#include <exception>
#include <string>

namespace hobbycad {

bool SoftCrashApplication::notify(QObject* receiver, QEvent* event)
{
    try {
        return QApplication::notify(receiver, event);
    } catch (const Standard_Failure& e) {
        // OCCT's own type. 7.x does not derive it from std::exception, so
        // it has to be named to be caught at all on that version.
        handleEscapedException(hobbycad::occtFailureMessage(e), receiver, event);
    } catch (const std::exception& e) {
        handleEscapedException(e.what(), receiver, event);
    } catch (...) {
        handleEscapedException("unknown exception", receiver, event);
    }

    // Swallowing the event is the point: returning normally lets the event
    // loop continue instead of unwinding through it, which Qt does not
    // define. The event itself is lost, which is the price of surviving.
    return false;
}

void SoftCrashApplication::handleEscapedException(const char* what,
                                                  QObject* receiver,
                                                  QEvent* event)
{
    // Already on the way out: log and return, or a failure raised while
    // shutting down turns into a loop.
    if (m_stopping) {
        CrashHandler::reportException("SoftCrashApplication (shutting down)",
                                      what);
        return;
    }

    // Name the receiver and event: the backtrace shows the catch site, not
    // the throw site, so this is often the most useful line in the report.
    std::string context = "QApplication::notify";
    if (receiver) {
        context += " receiver=";
        context += receiver->metaObject()->className();
        if (!receiver->objectName().isEmpty()) {
            context += "(";
            context += receiver->objectName().toStdString();
            context += ")";
        }
    }
    if (event) {
        context += " event=" + std::to_string(static_cast<int>(event->type()));
    }
    CrashHandler::reportException(context.c_str(), what);

    ++m_failures;

    // First failure: try to keep going with less. The 2D workspace needs no
    // OCCT, so dropping the viewport is a real option rather than a token
    // gesture.
    if (!m_degraded && m_failures <= m_budget && m_degrade) {
        if (m_degrade()) {
            m_degraded = true;
            QMessageBox::warning(
                nullptr, tr("3D viewport stopped"),
                tr("The 3D viewport failed and has been switched off.\n\n"
                   "Sketching and file operations continue to work. Your "
                   "work has not been lost.\n\nDetails were written to the "
                   "crash log."));
            return;
        }
    }

    // Could not degrade, or it failed again after degrading. Stop, but stop
    // on our own terms: save first, say why, and exit cleanly rather than
    // letting the next throw reach std::terminate.
    m_stopping = true;
    if (m_save) {
        m_save();
    }

    QMessageBox::critical(
        nullptr, tr("HobbyCAD must close"),
        tr("A failure occurred that HobbyCAD could not recover from.\n\n"
           "An emergency save was attempted and details were written to the "
           "crash log.\n\nError: %1").arg(QString::fromUtf8(what)));

    // Queued: unwinding to the event loop first lets Qt finish the current
    // dispatch, so the exit runs from a clean stack rather than from inside
    // a half-processed event.
    QTimer::singleShot(0, this, [this]() { this->exit(70); });
}

}  // namespace hobbycad
