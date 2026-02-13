// =====================================================================
//  src/hobbycad/gui/bindingeditrow.cpp — Single binding editor row
// =====================================================================

#include "bindingeditrow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace hobbycad {

BindingEditRow::BindingEditRow(QWidget* parent)
    : QWidget(parent)
{
    createLayout();
}

void BindingEditRow::createLayout()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // Level 1: Modifier checkboxes
    auto* level1 = new QHBoxLayout;
    level1->setSpacing(12);

    m_ctrlCheck = new QCheckBox(tr("Ctrl"));
    m_shiftCheck = new QCheckBox(tr("Shift"));
    m_altCheck = new QCheckBox(tr("Alt"));

    level1->addWidget(m_ctrlCheck);
    level1->addWidget(m_shiftCheck);
    level1->addWidget(m_altCheck);

    connect(m_ctrlCheck, &QCheckBox::toggled,
            this, &BindingEditRow::onModifierChanged);
    connect(m_shiftCheck, &QCheckBox::toggled,
            this, &BindingEditRow::onModifierChanged);
    connect(m_altCheck, &QCheckBox::toggled,
            this, &BindingEditRow::onModifierChanged);

    mainLayout->addLayout(level1);

    // Level 2: Mouse button / Drag OR Keyboard
    auto* level2 = new QHBoxLayout;
    level2->setSpacing(8);

    // Input type selector
    m_inputType = new QComboBox;
    m_inputType->addItem(tr("Keyboard"), QStringLiteral("key"));
    m_inputType->addItem(tr("Mouse"), QStringLiteral("mouse"));
    level2->addWidget(m_inputType);

    connect(m_inputType, &QComboBox::currentIndexChanged,
            this, &BindingEditRow::onInputTypeChanged);

    // Input stack (keyboard or mouse controls)
    m_inputStack = new QStackedWidget;

    // Index 0: Keyboard input
    m_keyEdit = new QKeySequenceEdit;
    m_inputStack->addWidget(m_keyEdit);

    connect(m_keyEdit, &QKeySequenceEdit::keySequenceChanged,
            this, &BindingEditRow::onKeySequenceChanged);

    // Index 1: Mouse input (button + action)
    auto* mouseWidget = new QWidget;
    auto* mouseLayout = new QHBoxLayout(mouseWidget);
    mouseLayout->setContentsMargins(0, 0, 0, 0);
    mouseLayout->setSpacing(8);

    m_buttonCombo = new QComboBox;
    m_buttonCombo->addItem(tr("Left Button"), QStringLiteral("LeftButton"));
    m_buttonCombo->addItem(tr("Middle Button"), QStringLiteral("MiddleButton"));
    m_buttonCombo->addItem(tr("Right Button"), QStringLiteral("RightButton"));
    m_buttonCombo->addItem(tr("(None)"), QString());
    mouseLayout->addWidget(m_buttonCombo);

    m_actionCombo = new QComboBox;
    m_actionCombo->addItem(tr("Click"), QStringLiteral("Click"));
    m_actionCombo->addItem(tr("Drag"), QStringLiteral("Drag"));
    m_actionCombo->addItem(tr("Wheel"), QStringLiteral("Wheel"));
    mouseLayout->addWidget(m_actionCombo);

    m_inputStack->addWidget(mouseWidget);

    connect(m_buttonCombo, &QComboBox::currentIndexChanged,
            this, &BindingEditRow::onMouseComponentChanged);
    connect(m_actionCombo, &QComboBox::currentIndexChanged,
            this, &BindingEditRow::onMouseComponentChanged);

    level2->addWidget(m_inputStack);

    mainLayout->addLayout(level2);

    // Level 3: Clear button
    auto* level3 = new QHBoxLayout;

    m_clearBtn = new QPushButton(tr("Clear"));
    level3->addWidget(m_clearBtn);

    connect(m_clearBtn, &QPushButton::clicked,
            this, &BindingEditRow::onClearClicked);

    mainLayout->addLayout(level3);
}

bool BindingEditRow::isMouseBinding(const QString& binding)
{
    if (binding.isEmpty()) return false;

    return binding.contains(QStringLiteral("Button"), Qt::CaseInsensitive) ||
           binding.contains(QStringLiteral("Wheel"), Qt::CaseInsensitive) ||
           binding.contains(QStringLiteral("Drag"), Qt::CaseInsensitive) ||
           binding.contains(QStringLiteral("Click"), Qt::CaseInsensitive);
}

QString BindingEditRow::binding() const
{
    if (m_inputType->currentIndex() == 0) {
        return buildKeyboardBinding();
    } else {
        return buildMouseBinding();
    }
}

QString BindingEditRow::buildKeyboardBinding() const
{
    QKeySequence seq = m_keyEdit->keySequence();
    if (seq.isEmpty()) return QString();

    // The QKeySequence already includes modifiers from the key capture,
    // but we also allow additional modifiers via checkboxes for flexibility.
    // For keyboard bindings, we rely primarily on QKeySequenceEdit.
    return seq.toString();
}

QString BindingEditRow::buildMouseBinding() const
{
    QString button = m_buttonCombo->currentData().toString();
    QString action = m_actionCombo->currentData().toString();

    // Wheel doesn't require a button
    if (button.isEmpty() && action != QStringLiteral("Wheel")) {
        return QString();
    }

    QStringList parts;

    if (m_ctrlCheck->isChecked())
        parts << QStringLiteral("Ctrl");
    if (m_shiftCheck->isChecked())
        parts << QStringLiteral("Shift");
    if (m_altCheck->isChecked())
        parts << QStringLiteral("Alt");

    if (!button.isEmpty()) {
        parts << button;
    }

    parts << action;

    return parts.join(QStringLiteral("+"));
}

void BindingEditRow::setBinding(const QString& binding)
{
    m_updating = true;

    // Reset everything
    m_ctrlCheck->setChecked(false);
    m_shiftCheck->setChecked(false);
    m_altCheck->setChecked(false);
    m_keyEdit->clear();
    m_buttonCombo->setCurrentIndex(0);
    m_actionCombo->setCurrentIndex(0);

    if (binding.isEmpty()) {
        m_inputType->setCurrentIndex(0);  // Default to keyboard
        m_inputStack->setCurrentIndex(0);
        // Checkboxes disabled for keyboard mode (display only)
        m_ctrlCheck->setEnabled(false);
        m_shiftCheck->setEnabled(false);
        m_altCheck->setEnabled(false);
        m_updating = false;
        return;
    }

    if (isMouseBinding(binding)) {
        // Parse mouse binding
        m_inputType->setCurrentIndex(1);
        m_inputStack->setCurrentIndex(1);
        // Checkboxes editable for mouse mode
        m_ctrlCheck->setEnabled(true);
        m_shiftCheck->setEnabled(true);
        m_altCheck->setEnabled(true);

        QStringList parts = binding.split(QStringLiteral("+"));

        for (const QString& part : parts) {
            QString p = part.trimmed();

            if (p.compare(QStringLiteral("Ctrl"), Qt::CaseInsensitive) == 0) {
                m_ctrlCheck->setChecked(true);
            } else if (p.compare(QStringLiteral("Shift"), Qt::CaseInsensitive) == 0) {
                m_shiftCheck->setChecked(true);
            } else if (p.compare(QStringLiteral("Alt"), Qt::CaseInsensitive) == 0) {
                m_altCheck->setChecked(true);
            } else if (p.compare(QStringLiteral("LeftButton"), Qt::CaseInsensitive) == 0) {
                m_buttonCombo->setCurrentIndex(0);
            } else if (p.compare(QStringLiteral("MiddleButton"), Qt::CaseInsensitive) == 0) {
                m_buttonCombo->setCurrentIndex(1);
            } else if (p.compare(QStringLiteral("RightButton"), Qt::CaseInsensitive) == 0) {
                m_buttonCombo->setCurrentIndex(2);
            } else if (p.compare(QStringLiteral("Click"), Qt::CaseInsensitive) == 0) {
                m_actionCombo->setCurrentIndex(0);
            } else if (p.compare(QStringLiteral("Drag"), Qt::CaseInsensitive) == 0) {
                m_actionCombo->setCurrentIndex(1);
            } else if (p.compare(QStringLiteral("Wheel"), Qt::CaseInsensitive) == 0) {
                m_actionCombo->setCurrentIndex(2);
                m_buttonCombo->setCurrentIndex(3);  // (None)
            }
        }
    } else {
        // Keyboard binding
        m_inputType->setCurrentIndex(0);
        m_inputStack->setCurrentIndex(0);
        // Checkboxes disabled for keyboard mode (display only)
        m_ctrlCheck->setEnabled(false);
        m_shiftCheck->setEnabled(false);
        m_altCheck->setEnabled(false);

        QKeySequence seq(binding);
        m_keyEdit->setKeySequence(seq);

        // Parse the key sequence to check the modifier boxes (display only)
        if (!seq.isEmpty()) {
            // Get the key combination (first key in sequence)
            QKeyCombination combo = seq[0];
            Qt::KeyboardModifiers mods = combo.keyboardModifiers();

            m_ctrlCheck->setChecked(mods & Qt::ControlModifier);
            m_shiftCheck->setChecked(mods & Qt::ShiftModifier);
            m_altCheck->setChecked(mods & Qt::AltModifier);
        }
    }

    m_updating = false;
}

void BindingEditRow::clear()
{
    m_updating = true;
    m_ctrlCheck->setChecked(false);
    m_shiftCheck->setChecked(false);
    m_altCheck->setChecked(false);
    m_keyEdit->clear();
    m_buttonCombo->setCurrentIndex(0);
    m_actionCombo->setCurrentIndex(0);
    m_updating = false;

    emit bindingChanged(QString());
}

void BindingEditRow::onInputTypeChanged(int index)
{
    m_inputStack->setCurrentIndex(index);

    // Checkboxes are only editable for mouse bindings.
    // For keyboard, they just display the modifiers from QKeySequenceEdit.
    bool isMouse = (index == 1);
    m_ctrlCheck->setEnabled(isMouse);
    m_shiftCheck->setEnabled(isMouse);
    m_altCheck->setEnabled(isMouse);

    if (!m_updating) {
        updateBinding();
    }
}

void BindingEditRow::onModifierChanged()
{
    if (!m_updating && m_inputType->currentIndex() == 1) {
        // Only emit for mouse bindings; keyboard handles its own modifiers
        updateBinding();
    }
}

void BindingEditRow::onMouseComponentChanged()
{
    if (!m_updating) {
        updateBinding();
    }
}

void BindingEditRow::onKeySequenceChanged()
{
    if (!m_updating) {
        // Sync checkboxes to reflect the captured modifiers
        QKeySequence seq = m_keyEdit->keySequence();
        m_updating = true;
        if (!seq.isEmpty()) {
            QKeyCombination combo = seq[0];
            Qt::KeyboardModifiers mods = combo.keyboardModifiers();
            m_ctrlCheck->setChecked(mods & Qt::ControlModifier);
            m_shiftCheck->setChecked(mods & Qt::ShiftModifier);
            m_altCheck->setChecked(mods & Qt::AltModifier);
        } else {
            m_ctrlCheck->setChecked(false);
            m_shiftCheck->setChecked(false);
            m_altCheck->setChecked(false);
        }
        m_updating = false;

        updateBinding();
    }
}

void BindingEditRow::onClearClicked()
{
    clear();
}

void BindingEditRow::updateBinding()
{
    emit bindingChanged(binding());
}

}  // namespace hobbycad
