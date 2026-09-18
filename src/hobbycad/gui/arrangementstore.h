// =====================================================================
//  src/hobbycad/gui/arrangementstore.h — the arrangement this front end
//  shows, and the file the person's own changes live in
// =====================================================================
//
//  HobbyCAD's built-in arrangement (hobbycad/layout/arrangement.h) with
//  the customizations from arrangement.json applied
//  (hobbycad/layout/customization.h). The file sits in the user's
//  configuration directory, does not exist until something is customized,
//  and is removed again when the last customization goes, so a fresh
//  HobbyCAD has nothing to read and shows the default.
//
//  Everything that builds menus, toolbars or a bindings editor asks this
//  for the arrangement, so one change reaches all of them: changed() is
//  emitted, the main window rebuilds what it shows, and the file is
//  written.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_ARRANGEMENTSTORE_H
#define HOBBYCAD_ARRANGEMENTSTORE_H

#include <hobbycad/layout/customization.h>

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace hobbycad {

class ArrangementStore : public QObject {
    Q_OBJECT

public:
    /// The one store the front end uses.
    static ArrangementStore& instance();

    /// The arrangement to show: the default plus the person's changes.
    const layout::Arrangement& current() const { return m_resolved; }
    /// HobbyCAD's own, for "restore defaults" and for comparison.
    const layout::Arrangement& base() const;
    const layout::Customizations& customizations() const { return m_customizations; }
    bool isCustomized() const { return !m_customizations.empty(); }

    /// Where arrangement.json is, whether or not it exists.
    QString filePath() const;
    /// What the last read had to skip, ready to show; empty when the file
    /// was good or absent.
    QStringList problems() const { return m_problems; }

    /// Read the file again (at startup, and after an import).
    void reload();

    /// The text an element shows: the person's own name when it has one,
    /// shown exactly as typed and never translated.
    QString elementText(const layout::Element& element) const;

    // ---- changes, each written to the file at once -------------------

    /// Put an element at `index` in `container`. One entry is written and
    /// no other element is renumbered.
    bool move(const QString& key, const QString& container, int index);
    void setHidden(const QString& key, bool hidden);
    /// Add a separator to `container` at `index`. Returns its key, which
    /// is the person's own (layout::kAddedPrefix), or "" on refusal.
    QString addSeparator(const QString& container, int index);
    /// An empty name gives the translated default back.
    void setLabel(const QString& key, const QString& label);
    void setBinding(const QString& commandId, const bindings::Slots& keys);
    /// Every command's bindings at once (the bindings dialog's Apply).
    void setBindings(const QHash<QString, bindings::Slots>& keys);

    /// Drop one element's changes, or a whole area's. Returns what went.
    bool restoreElement(const QString& key);
    std::size_t restore(layout::RestoreArea area);

    /// Write the whole resolved arrangement to `path`, for sharing.
    bool exportTo(const QString& path, QString* error);
    /// Read a complete arrangement and keep what differs from the default.
    bool importFrom(const QString& path, QStringList* problems);

signals:
    /// The arrangement changed: rebuild menus, toolbars and bindings.
    void changed();

private:
    ArrangementStore();

    void save();
    void rebuild();
    /// Bindings used to live in QSettings; the first run after the move
    /// brings them across so nobody loses their keys.
    void adoptSettingsBindings();

    layout::Customizations m_customizations;
    layout::Arrangement m_resolved;
    QStringList m_problems;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ARRANGEMENTSTORE_H
