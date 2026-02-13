// =====================================================================
//  src/hobbycad/gui/bindingeditrow.h — Single binding editor row
// =====================================================================
//
//  A widget for editing a single binding with three levels:
//    Level 1: Modifier checkboxes (Ctrl, Shift, Alt)
//    Level 2: Input selector (keyboard key OR mouse button+action)
//    Level 3: Clear button
//
//  Supports both keyboard and mouse bindings for any action.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BINDINGEDITROW_H
#define HOBBYCAD_BINDINGEDITROW_H

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QPushButton;
class QStackedWidget;

namespace hobbycad {

class BindingEditRow : public QWidget {
    Q_OBJECT

public:
    explicit BindingEditRow(QWidget* parent = nullptr);

    /// Get the binding as a string
    QString binding() const;

    /// Set the binding from a string
    void setBinding(const QString& binding);

    /// Clear the binding
    void clear();

    /// Check if a string represents a mouse binding
    static bool isMouseBinding(const QString& binding);

signals:
    void bindingChanged(const QString& binding);

private slots:
    void onInputTypeChanged(int index);
    void onModifierChanged();
    void onMouseComponentChanged();
    void onKeySequenceChanged();
    void onClearClicked();

private:
    void createLayout();
    void updateBinding();
    QString buildMouseBinding() const;
    QString buildKeyboardBinding() const;

    // Level 1: Modifiers
    QCheckBox*  m_ctrlCheck     = nullptr;
    QCheckBox*  m_shiftCheck    = nullptr;
    QCheckBox*  m_altCheck      = nullptr;

    // Input type selector (Keyboard / Mouse)
    QComboBox*  m_inputType     = nullptr;

    // Level 2: Input (stacked - keyboard or mouse)
    QStackedWidget* m_inputStack = nullptr;

    // Keyboard input (index 0)
    QKeySequenceEdit* m_keyEdit = nullptr;

    // Mouse input (index 1)
    QComboBox*  m_buttonCombo   = nullptr;
    QComboBox*  m_actionCombo   = nullptr;

    // Level 3: Clear
    QPushButton* m_clearBtn     = nullptr;

    bool m_updating = false;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_BINDINGEDITROW_H
