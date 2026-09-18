// =====================================================================
//  src/hobbycad/gui/bindingsdialog.h — Keyboard and mouse bindings
// =====================================================================
//
//  A dialog for customizing keyboard shortcuts and mouse bindings.
//  Each action can have up to 3 bindings (any combination of keys
//  and mouse buttons).
//
//  Accessed from Preferences > Bindings.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BINDINGSDIALOG_H
#define HOBBYCAD_BINDINGSDIALOG_H

#include <hobbycad/bindings.h>

#include <QDialog>
#include <QHash>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;

namespace hobbycad {

class BindingEditRow;

// ---------------------------------------------------------------------
//  ActionBinding — Stores up to 3 bindings for a single action.
// ---------------------------------------------------------------------

struct ActionBinding {
    QString actionId;       // Unique ID (e.g., "file.new", "view.reset")
    QString displayName;    // Human-readable name (e.g., "New Document")
    QString category;       // Category for grouping (e.g., "File", "View")

    // Up to 3 bindings; each can be a key sequence or mouse binding.
    // Empty string means slot is unused.
    QString binding1;
    QString binding2;
    QString binding3;

    // Default bindings (used for "Restore Defaults")
    QString default1;
    QString default2;
    QString default3;

    ActionBinding() = default;
    ActionBinding(const QString& id, const QString& name,
                  const QString& cat,
                  const QString& def1 = QString(),
                  const QString& def2 = QString(),
                  const QString& def3 = QString())
        : actionId(id), displayName(name), category(cat)
        , binding1(def1), binding2(def2), binding3(def3)
        , default1(def1), default2(def2), default3(def3)
    {}
};

// ---------------------------------------------------------------------
//  BindingsDialog — Main dialog for editing bindings.
// ---------------------------------------------------------------------

class BindingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit BindingsDialog(QWidget* parent = nullptr);

    /// Load all bindings: the defaults with the person's own applied
    /// (ArrangementStore, arrangement.json).
    static QHash<QString, ActionBinding> loadBindings();

    /// Store the bindings that differ from the defaults, in the same file
    /// as the rest of the person's arrangement.
    static void saveBindings(const QHash<QString, ActionBinding>& bindings);

    /// Get the default action definitions (built-in bindings).
    static QHash<QString, ActionBinding> defaultBindings();

    /// The saved bindings as a table the library's rules work on.
    static bindings::Table loadTable();

    /// Any set of bindings as such a table.
    static bindings::Table toTable(const QHash<QString, ActionBinding>& actions);

    /// Check if a binding conflicts with another action heard in the same
    /// place. Returns the conflicting action ID, or empty string if no
    /// conflict. A global or window-wide key conflicts with everything; keys
    /// heard by different views (the sketch canvas, the 3D view) do not.
    QString checkConflict(const QString& actionId,
                          const QString& binding) const;

    /// Get the binding context of an action: its command's context, or the
    /// id's first component for an unknown id ("sketch.line" -> "sketch").
    static QString getActionContext(const QString& actionId);

signals:
    /// Emitted when bindings are saved (Apply or OK clicked).
    void bindingsChanged();

private slots:
    void onSelectionChanged();
    void onBinding1Changed(const QString& binding);
    void onBinding2Changed(const QString& binding);
    void onBinding3Changed(const QString& binding);
    void onRestoreDefaults();
    void apply();
    void accept() override;

private:
    void createLayout();
    void populateActions();
    void updateBindingEditors();
    void updateTreeForAction(const QString& actionId);
    bool confirmConflict(const QString& conflictingActionId,
                         const QString& binding);
    void handleBindingChange(int slot, const QString& binding);

    QTreeWidget*      m_actionTree     = nullptr;

    // Three binding editors (one for each slot)
    BindingEditRow*   m_bindingRow1    = nullptr;
    BindingEditRow*   m_bindingRow2    = nullptr;
    BindingEditRow*   m_bindingRow3    = nullptr;
    QLabel*           m_actionLabel    = nullptr;  // Shows which action is selected
    QLabel*           m_conflictLabel  = nullptr;
    QPushButton*      m_restoreBtn     = nullptr;
    QPushButton*      m_applyBtn       = nullptr;

    // Action ID -> binding data
    QHash<QString, ActionBinding> m_bindings;
    QHash<QString, ActionBinding> m_originalBindings;  // For change detection

    // Currently selected action
    QString m_selectedAction;

    void updateApplyButton();
    bool hasChanges() const;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_BINDINGSDIALOG_H
