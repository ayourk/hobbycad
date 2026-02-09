// =====================================================================
//  src/hobbycad/gui/reduced/diagnosticdialog.cpp — GPU diagnostic dialog
// =====================================================================

#include "diagnosticdialog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace hobbycad {

DiagnosticDialog::DiagnosticDialog(const OpenGLInfo& glInfo,
                                   QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("DiagnosticDialog"));
    setWindowTitle(tr("OpenGL Diagnostic — Reduced Mode"));
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);

    // Explanation
    auto* explainLabel = new QLabel(
        tr("<b>OpenGL 3.3 or higher is required for the 3D viewport "
           "but was not detected.</b><br><br>"
           "HobbyCAD is running in Reduced Mode. File operations, "
           "geometry tools, and scripting work normally, but the "
           "3D viewport is disabled."));
    explainLabel->setWordWrap(true);
    layout->addWidget(explainLabel);

    // GPU info (read-only, copyable)
    auto* infoLabel = new QLabel(tr("<b>Detected Graphics Information:</b>"));
    layout->addWidget(infoLabel);

    auto* infoText = new QPlainTextEdit();
    infoText->setReadOnly(true);
    infoText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    infoText->setPlainText(glInfo.summary());
    infoText->setMaximumHeight(130);
    infoText->setFocusPolicy(Qt::ClickFocus);  // don't grab tab focus
    layout->addWidget(infoText);

    // Copy to Clipboard button
    auto* copyBtn = new QPushButton(tr("Copy to Clipboard"));
    connect(copyBtn, &QPushButton::clicked, this, [=]() {
        QGuiApplication::clipboard()->setText(infoText->toPlainText());
        copyBtn->setText(tr("Copied!"));
    });
    layout->addWidget(copyBtn, 0, Qt::AlignLeft);

    // Vendor-specific guidance
    auto* guidanceLabel = new QLabel(buildGuidanceText(glInfo));
    guidanceLabel->setWordWrap(true);
    layout->addWidget(guidanceLabel);

    // Driver check reminder
    auto* driverLabel = new QLabel(
        tr("<i>Tip: Check that your GPU drivers are up to date. "
           "On Linux, verify with: glxinfo | grep \"OpenGL version\"</i>"));
    driverLabel->setWordWrap(true);
    layout->addWidget(driverLabel);

    layout->addSpacing(8);

    // Bottom row: checkbox + buttons
    auto* bottomLayout = new QHBoxLayout();

    m_dontShowCheck = new QCheckBox(tr("Don't show this again"));
    bottomLayout->addWidget(m_dontShowCheck);

    bottomLayout->addStretch();

    auto* continueBtn = new QPushButton(tr("Continue in Reduced Mode"));
    continueBtn->setDefault(true);
    connect(continueBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(continueBtn);

    auto* exitBtn = new QPushButton(tr("Exit"));
    connect(exitBtn, &QPushButton::clicked, this, [this]() {
        done(2);  // Distinct from accept (1) and reject (0)
    });
    bottomLayout->addWidget(exitBtn);

    layout->addLayout(bottomLayout);

    // Ensure the Continue button has focus so Enter/Space work
    // immediately when the dialog appears.
    continueBtn->setFocus();
}

bool DiagnosticDialog::dontShowAgain() const
{
    return m_dontShowCheck->isChecked();
}

void DiagnosticDialog::reject()
{
    done(2);  // ESC = Exit the application
}

QString DiagnosticDialog::buildGuidanceText(const OpenGLInfo& glInfo) const
{
    QString text = QStringLiteral("<b>") +
                   tr("GPU Upgrade Guidance:") +
                   QStringLiteral("</b><br>");

    QString vendorLower = glInfo.vendor.toLower();

    if (vendorLower.contains(QLatin1String("nvidia"))) {
        text += tr("NVIDIA GPU detected. OpenGL 3.3 is supported by "
                   "GeForce 8000 series and newer. Update to the latest "
                   "NVIDIA proprietary driver for best results.");
    }
    else if (vendorLower.contains(QLatin1String("amd")) ||
             vendorLower.contains(QLatin1String("ati"))) {
        text += tr("AMD GPU detected. OpenGL 3.3 is supported by "
                   "Radeon HD 2000 series and newer. On Linux, both "
                   "the Mesa (radeonsi) and AMDGPU-PRO drivers "
                   "support OpenGL 3.3+.");
    }
    else if (vendorLower.contains(QLatin1String("intel"))) {
        text += tr("Intel GPU detected. OpenGL 3.3 is supported by "
                   "HD Graphics 4000 (Ivy Bridge, 2012) and newer. "
                   "On Linux, ensure the Mesa i965 or iris driver "
                   "is active.");
    }
    else {
        text += tr("GPU vendor not recognized. OpenGL 3.3 requires "
                   "a GPU from approximately 2008 or newer. Please "
                   "check your GPU specifications and driver version.");
    }

    return text;
}

}  // namespace hobbycad

