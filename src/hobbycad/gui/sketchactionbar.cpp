// =====================================================================
//  src/hobbycad/gui/sketchactionbar.cpp — Sketch action bar
// =====================================================================

#include "sketchactionbar.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace hobbycad {

SketchActionBar::SketchActionBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SketchActionBar"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 8, 4, 4);
    mainLayout->setSpacing(8);

    // Finish Sketch button (prominent, full width)
    m_finishButton = new QPushButton(this);
    m_finishButton->setText(tr("Finish Sketch"));
    m_finishButton->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_finishButton->setToolTip(tr("Finish editing sketch and choose to save or discard"));

    // Style the finish button to be prominent
    m_finishButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #0078D4;"
        "  color: white;"
        "  border: none;"
        "  padding: 8px 16px;"
        "  border-radius: 3px;"
        "  font-weight: bold;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #1084D8;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #006CBD;"
        "}"
    ));
    mainLayout->addWidget(m_finishButton);

    // Save/Discard buttons (hidden until Finish is clicked)
    m_saveDiscardWidget = new QWidget(this);
    auto* buttonLayout = new QHBoxLayout(m_saveDiscardWidget);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);

    // Discard button (left side)
    m_discardButton = new QPushButton(m_saveDiscardWidget);
    m_discardButton->setText(tr("Discard"));
    m_discardButton->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_discardButton->setToolTip(tr("Discard changes and exit sketch (Escape)"));
    buttonLayout->addWidget(m_discardButton);

    // Stretch in the middle
    buttonLayout->addStretch();

    // Save button (right side, emphasized)
    m_saveButton = new QPushButton(m_saveDiscardWidget);
    m_saveButton->setText(tr("Save"));
    m_saveButton->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_saveButton->setToolTip(tr("Save sketch and exit"));
    m_saveButton->setDefault(true);

    // Style the save button to be more prominent
    m_saveButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background-color: #107C10;"
        "  color: white;"
        "  border: none;"
        "  padding: 6px 16px;"
        "  border-radius: 3px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #0E8C0E;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #0C6C0C;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #666666;"
        "  color: #999999;"
        "}"
    ));
    buttonLayout->addWidget(m_saveButton);

    m_saveDiscardWidget->setVisible(false);  // Hidden until Finish is clicked
    mainLayout->addWidget(m_saveDiscardWidget);

    // Connect signals
    connect(m_finishButton, &QPushButton::clicked,
            this, &SketchActionBar::onFinishClicked);
    connect(m_saveButton, &QPushButton::clicked,
            this, &SketchActionBar::saveClicked);
    connect(m_discardButton, &QPushButton::clicked,
            this, &SketchActionBar::discardClicked);
}

void SketchActionBar::onFinishClicked()
{
    // Show Save/Discard buttons, hide Finish button
    m_finishButton->setVisible(false);
    m_saveDiscardWidget->setVisible(true);
}

void SketchActionBar::reset()
{
    // Reset to initial state (Finish button visible, Save/Discard hidden)
    m_finishButton->setVisible(true);
    m_saveDiscardWidget->setVisible(false);
}

void SketchActionBar::setSaveEnabled(bool enabled)
{
    m_saveButton->setEnabled(enabled);
}

void SketchActionBar::setModified(bool modified)
{
    m_modified = modified;
}

}  // namespace hobbycad
