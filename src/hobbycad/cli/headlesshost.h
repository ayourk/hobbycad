// =====================================================================
//  src/hobbycad/cli/headlesshost.h — a document with no GUI behind it
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  The `--no-gui` half of DocumentHost. It owns a Project outright and
//  has nothing to refresh, which is the whole difference between the two
//  implementations: the GUI one points at the project its windows are
//  showing and rebuilds views on change; this one just holds it.
//
//  Everything above the seam (every command, its parsing, its output)
//  is identical in both. That is the property worth protecting: a command
//  that works headless works in the GUI terminal without being written
//  twice.
//
#ifndef HOBBYCAD_HEADLESSHOST_H
#define HOBBYCAD_HEADLESSHOST_H

#include <hobbycad/document_host.h>
#include <hobbycad/document_undo.h>
#include <hobbycad/document_undo_host.h>
#include <hobbycad/project.h>
#include <hobbycad/project_session.h>

namespace hobbycad {

/// It is also the undo host.
///
/// `--no-gui` had no undo at all: only MainWindow implemented
/// DocumentUndoHost, so a headless session answered "no document" to every
/// undo. The history now lives in the host's ProjectSession, the same class
/// the GUI edits through, so undo behaves the same at either prompt and
/// neither host carries apply logic of its own.
class HeadlessDocumentHost : public DocumentHost, public DocumentUndoHost {
public:
    HeadlessDocumentHost() = default;
    // The session refers to this host's project; a copy would edit the original's.
    HeadlessDocumentHost(const HeadlessDocumentHost&) = delete;
    HeadlessDocumentHost& operator=(const HeadlessDocumentHost&) = delete;

    Project* hostProject() override { return &m_project; }
    const Project* hostProject() const override { return &m_project; }
    ProjectSession* hostSession() override { return &m_session; }

    // ---- DocumentUndoHost ------------------------------------------

    void pushDocumentCommand(const DocumentCommand& command) override
    {
        m_session.record(command);
    }
    bool undoDocument(std::string* description) override { return m_session.undo(description); }
    bool redoDocument(std::string* description) override { return m_session.redo(description); }

    std::string nextUndoDescription() const override { return m_session.history().undoDescription(); }
    std::string nextRedoDescription() const override { return m_session.history().redoDescription(); }
    int undoDepth() const override { return m_session.history().undoLevels(); }
    int redoDepth() const override { return m_session.history().redoLevels(); }

    // No views to rebuild, and no viewport to act on. Answering false for
    // the viewport lets zoom/pan/rotate say so plainly rather than
    // appearing to succeed against nothing.
    bool hostHasViewport() const override { return false; }

    Project& project() { return m_project; }

private:
    Project m_project;
    ProjectSession m_session{m_project};
};

}  // namespace hobbycad

#endif  // HOBBYCAD_HEADLESSHOST_H
