// =====================================================================
//  src/libhobbycad/hobbycad/document_host.h — who owns the document
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  Aaron, 2026-08-27: "CLI should be able to drive the gui and should be
//  able to run without a gui at all."
//
//  One command set, two owners. The CLI does not hold a document: in
//  the GUI it must act on the SAME project the windows are showing, and
//  headless there has to be one at all. This is the seam between them,
//  and it is deliberately the narrowest thing that works:
//
//      parse text        -> src/hobbycad/cli   (Qt strings, completion)
//      act on the model  -> through THIS       (GUI-backed or headless)
//      the model itself  -> libhobbycad        (Project, sketch, brep)
//
//  Putting the seam here rather than in the CLI is what lets a second
//  front end drive the same commands: a wxWidgets or headless owner
//  implements this and inherits every command for free. It follows
//  DocumentUndoHost, which solved the same problem for undo.
//
#ifndef HOBBYCAD_DOCUMENT_HOST_H
#define HOBBYCAD_DOCUMENT_HOST_H

namespace hobbycad {

class Project;
class ProjectSession;

/// Something that owns a Project and can be driven by commands.
class DocumentHost {
public:
    virtual ~DocumentHost() = default;

    /// The document commands act on, or null when nothing is open.
    ///
    /// Null is a normal state, not an error: a freshly started CLI has no
    /// project. Commands report "no document" rather than creating one
    /// behind the user's back.
    virtual Project* hostProject() = 0;
    virtual const Project* hostProject() const = 0;

    /// The session every edit to that document goes through, or null when
    /// nothing is open. The GUI window and the headless CLI each own one,
    /// and the commands that change the model use it, so every front end
    /// makes the same edits into the same history.
    virtual ProjectSession* hostSession() { return nullptr; }

    /// Called after a command changes the model.
    ///
    /// A GUI owner rebuilds its views here. A headless owner does nothing,
    /// which is why this has a default rather than being pure: headless
    /// is the simpler case and should not have to write an empty override.
    virtual void hostDocumentChanged() {}

    /// Called after a command replaced the project outright ("new", "open").
    ///
    /// A GUI owner resets its views and leaves any sketch it had open; the
    /// default treats it as an ordinary change.
    virtual void hostProjectReplaced() { hostDocumentChanged(); }

    /// Whether the front end holds work the project does not show yet, such
    /// as a sketch open on a canvas. "new" and "open" refuse to drop it
    /// unless told to.
    virtual bool hostHasOpenWork() const { return false; }

    /// Whether a viewport exists to act on.
    ///
    /// View commands (zoom, pan, rotate) are meaningful only with one.
    /// Headless answers false, so those commands can say so plainly
    /// instead of appearing to work.
    virtual bool hostHasViewport() const { return false; }
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DOCUMENT_HOST_H
