// =====================================================================
//  src/hobbycad/gui/themeeditordialog.h — edit the sketch canvas theme
// =====================================================================
//
//  A theme editor modeled on the keybinding editor: a categorized tree of
//  every canvas color role on the left, a color picker on the right. It
//  edits the CURRENTLY ACTIVE theme (light or dark), so every change hot-
//  swaps live onto the canvas, the way a language switch does, and is
//  persisted as a per-role override in QSettings.
//
//  Opened from View > Theme > Edit...
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================
#ifndef HOBBYCAD_THEMEEDITORDIALOG_H
#define HOBBYCAD_THEMEEDITORDIALOG_H

#include "sketchtheme.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

namespace hobbycad {

class ColorPicker;

class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(QWidget* parent = nullptr);

signals:
    /// A color changed and was persisted; listeners should re-apply the theme
    /// to every open canvas (hot swap).
    void themeApplied();

private:
    void populate();
    void refreshSwatches();
    SketchTheme effectiveTheme() const;               // base(mode) + overrides
    const SketchThemeField* fieldByKey(const QString& key) const;
    void onSelectionChanged();
    void onColorChanged(const QColor& c);
    void onResetField();
    void onRestoreDefaults();

    bool m_dark = false;                              // the mode being edited (= active context)
    QTreeWidget* m_tree = nullptr;
    ColorPicker* m_picker = nullptr;
    QLabel* m_fieldLabel = nullptr;
    QPushButton* m_resetFieldBtn = nullptr;
    QPushButton* m_restoreBtn = nullptr;
    QString m_selectedKey;                            // field key of the selected row
    bool m_updating = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_THEMEEDITORDIALOG_H
