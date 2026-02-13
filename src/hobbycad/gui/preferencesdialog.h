// =====================================================================
//  src/hobbycad/gui/preferencesdialog.h — Application preferences
// =====================================================================
//
//  A multi-page preferences dialog using a QListWidget for page
//  navigation and a QStackedWidget for page content.  Pages are
//  added as categories; each category owns its own layout.
//
//  Currently implemented pages:
//    - Navigation   (mouse bindings, rotation defaults, animation)
//    - Bindings     (keyboard shortcuts and mouse bindings)
//    - General      (grid, startup behavior — placeholder)
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PREFERENCESDIALOG_H
#define HOBBYCAD_PREFERENCESDIALOG_H

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

namespace hobbycad {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

signals:
    /// Emitted when bindings are changed (forwarded from BindingsDialog).
    void bindingsChanged();

private slots:
    void apply();
    void accept() override;
    void openBindingsDialog();

private:
    void createPages();
    QWidget* createNavigationPage();
    QWidget* createBindingsPage();
    QWidget* createGeneralPage();

    void loadSettings();
    void saveSettings();

    // Page navigation
    QListWidget*    m_pageList   = nullptr;
    QStackedWidget* m_pageStack  = nullptr;

    // Navigation page controls
    QComboBox*      m_mousePreset      = nullptr;
    QComboBox*      m_defaultAxis      = nullptr;
    QSpinBox*       m_spinInterval     = nullptr;
    QSpinBox*       m_snapStepDeg      = nullptr;
    QSpinBox*       m_snapInterval     = nullptr;
    QSpinBox*       m_pgUpStepDeg      = nullptr;

    // General page controls
    QCheckBox*      m_showGridOnStart  = nullptr;
    QCheckBox*      m_restoreSession   = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PREFERENCESDIALOG_H
