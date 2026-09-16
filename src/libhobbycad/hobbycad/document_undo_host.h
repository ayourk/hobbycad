// =====================================================================
//  src/libhobbycad/hobbycad/document_undo_host.h — undo seam for front ends
// =====================================================================
//
//  The CLI and the GUI must share ONE undo history. They do not share an
//  object: CliPanel builds its own CliEngine, and in --no-gui mode there is
//  no window at all. This interface is the seam between them.
//
//  A front end that owns a document implements it; CliEngine calls it. When
//  no host is attached (standalone --no-gui with nothing open), the CLI
//  says so plainly rather than reporting a success it did not perform.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_DOCUMENT_UNDO_HOST_H
#define HOBBYCAD_DOCUMENT_UNDO_HOST_H

#include "core.h"
#include "document_undo.h"

#include <string>

namespace hobbycad {

/// Implemented by whatever owns the document and its undo history.
class HOBBYCAD_EXPORT DocumentUndoHost {
public:
    virtual ~DocumentUndoHost() = default;

    /// Undo one step. Returns false if there was nothing to undo or the
    /// command no longer fits the document. On success `description` names
    /// what was undone, so the caller can report it.
    virtual bool undoDocument(std::string* description) = 0;

    /// Redo one step. Same contract.
    virtual bool redoDocument(std::string* description) = 0;

    /// What the next undo/redo would do; empty when unavailable.
    virtual std::string nextUndoDescription() const = 0;
    virtual std::string nextRedoDescription() const = 0;

    /// How many steps are available each way. For "undo 3" and for telling
    /// the user how deep the history goes.
    virtual int undoDepth() const = 0;
    virtual int redoDepth() const = 0;

    /// Record an operation so it can be undone.
    ///
    /// Without this the seam was one-way: the CLI could undo what the GUI
    /// had done, but nothing the CLI itself did was recorded. That was
    /// harmless while every CLI command only ADDED things and left nothing
    /// to lose. It stopped being harmless when "delete" arrived.
    ///
    /// Default does nothing, so a host that has no history is not forced to
    /// pretend it has one, but such a host must not be given destructive
    /// commands to run.
    virtual void pushDocumentCommand(const DocumentCommand& /*command*/) {}
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DOCUMENT_UNDO_HOST_H
