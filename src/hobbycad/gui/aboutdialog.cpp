// =====================================================================
//  src/hobbycad/gui/aboutdialog.cpp — About HobbyCAD dialog
// =====================================================================

#include "aboutdialog.h"

#include <hobbycad/core.h>

#include <QLabel>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace hobbycad {

AboutDialog::AboutDialog(const OpenGLInfo& glInfo, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("AboutDialog"));
    setWindowTitle(tr("About HobbyCAD"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);

    // Title
    auto* titleLabel = new QLabel(
        QStringLiteral("<h2>HobbyCAD %1</h2>")
            .arg(QString::fromLatin1(hobbycad::version())));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // Description
    auto* descLabel = new QLabel(
        tr("Open-source parametric 3D CAD for hobbyists.\n\n"
           "License: GPL 3.0 (only)\n"
           "https://github.com/ayourk/HobbyCAD"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // OpenGL info
    auto* glLabel = new QLabel(tr("<b>OpenGL Information:</b>"));
    layout->addWidget(glLabel);

    auto* glText = new QPlainTextEdit();
    glText->setReadOnly(true);
    glText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    glText->setPlainText(glInfo.summary());
    glText->setMaximumHeight(120);
    glText->setFocusPolicy(Qt::ClickFocus);
    layout->addWidget(glText);

    // Close button
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    closeBtn->setFocus();
}

}  // namespace hobbycad

