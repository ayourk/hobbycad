// =====================================================================
//  src/hobbycad/gui/aboutdialog.cpp — About HobbyCAD dialog
// =====================================================================

#include <hobbycad/sketch/solver.h>
#include "aboutdialog.h"

#include <hobbycad/core.h>

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QFontDatabase>
#include <QIcon>
#include <QPainter>
#include <QSvgRenderer>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace hobbycad {

// Helper: create a selectable, word-wrapping value label
static QLabel* valueLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

AboutDialog::AboutDialog(const OpenGLInfo& glInfo, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("AboutDialog"));
    setWindowTitle(tr("About HobbyCAD"));
    setMinimumWidth(500);

    auto* layout = new QVBoxLayout(this);

    // Title
    auto* titleLabel = new QLabel(
        QStringLiteral("<h2>HobbyCAD %1</h2>")
            .arg(QString::fromLatin1(hobbycad::version())));
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // Logo: render SVG at exact target size via QSvgRenderer
    auto* logoLabel = new QLabel;
    logoLabel->setAlignment(Qt::AlignCenter);
    QSvgRenderer svgRenderer(QStringLiteral(":/icons/hobbycad.svg"));
    if (svgRenderer.isValid()) {
        QPixmap logoPix(96, 96);
        logoPix.fill(Qt::transparent);
        QPainter painter(&logoPix);
        svgRenderer.render(&painter);
        painter.end();
        logoLabel->setPixmap(logoPix);
    }
    layout->addWidget(logoLabel);

    // Description
    auto* descLabel = new QLabel(
        tr("Open-source parametric 3D CAD for hobbyists.\n\n"
           "License: GPL 3.0 (only)\n"
           "https://github.com/ayourk/HobbyCAD"));
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // --- OpenGL Information ----------------------------------------------
    auto* glGroup = new QGroupBox(tr("OpenGL Information"));
    auto* glForm = new QFormLayout(glGroup);

    glForm->addRow(tr("OpenGL Version:"),
        valueLabel(glInfo.version.empty()
            ? QStringLiteral("N/A") : QString::fromStdString(glInfo.version)));
    glForm->addRow(tr("GLSL Version:"),
        valueLabel(glInfo.glslVersion.empty()
            ? QStringLiteral("N/A") : QString::fromStdString(glInfo.glslVersion)));
    glForm->addRow(tr("Renderer:"),
        valueLabel(glInfo.renderer.empty()
            ? QStringLiteral("N/A") : QString::fromStdString(glInfo.renderer)));
    glForm->addRow(tr("Vendor:"),
        valueLabel(glInfo.vendor.empty()
            ? QStringLiteral("N/A") : QString::fromStdString(glInfo.vendor)));

    QString status;
    if (glInfo.contextCreated) {
        status = QStringLiteral("success");
    } else {
        status = QStringLiteral("failed");
        if (!glInfo.errorMessage.empty())
            status += QStringLiteral(": ") + QString::fromStdString(glInfo.errorMessage);
    }
    glForm->addRow(tr("Context Creation:"), valueLabel(status));

    layout->addWidget(glGroup);

    // --- Dependencies ----------------------------------------------------
    auto* depGroup = new QGroupBox(tr("Dependencies and Platform"));
    auto* depForm = new QFormLayout(depGroup);

    depForm->addRow(tr("Qt:"),
        valueLabel(QStringLiteral("%1 (runtime %2)")
            .arg(QStringLiteral(QT_VERSION_STR),
                 QString::fromLatin1(qVersion()))));

#ifdef HOBBYCAD_OCCT_VERSION
    depForm->addRow(tr("OpenCASCADE:"),
        valueLabel(QStringLiteral(HOBBYCAD_OCCT_VERSION)));
#else
    depForm->addRow(tr("OpenCASCADE:"), valueLabel(tr("(unknown)")));
#endif

    // Constraint solver. The version alone is not the whole story: a stock
    // libslvs aborts the process on a kernel assertion, while a patched one
    // hands the fault back so the sketch survives. A bug report needs to say
    // which, so the capability is shown next to the version rather than left
    // for someone to infer from the number.
    // Listed only when the solver is actually linked in. A build without it
    // does not get a row saying "absent"; the rule is linked means listed,
    // not linked means not listed at all.
    if (sketch::Solver::isAvailable()) {
        QString solver = QString::fromLatin1(sketch::solverVersionString());
        if (sketch::solverCanRecoverFromFaults()) {
            solver += tr("  (recovers from solver faults)");
        } else if (sketch::solverFatalHandlerAvailable()) {
            solver += tr("  (reports solver faults, cannot recover)");
        } else {
            solver += tr("  (stock: a solver fault ends the process)");
        }
        depForm->addRow(tr("Solver (libslvs):"), valueLabel(solver));
    }

#ifdef HOBBYCAD_JSON_VERSION
    depForm->addRow(tr("nlohmann/json:"),
        valueLabel(QStringLiteral(HOBBYCAD_JSON_VERSION)));
#endif

#ifdef HOBBYCAD_WEBP_VERSION
    depForm->addRow(tr("libwebp:"),
        valueLabel(QStringLiteral(HOBBYCAD_WEBP_VERSION)));
#endif

#ifdef HOBBYCAD_STB_VERSION
    depForm->addRow(tr("stb_image:"),
        valueLabel(QStringLiteral(HOBBYCAD_STB_VERSION)));
#endif

#ifdef HOBBYCAD_EGL_VERSION
    depForm->addRow(tr("EGL:"),
        valueLabel(QStringLiteral(HOBBYCAD_EGL_VERSION)));
#endif

#ifdef HOBBYCAD_CMAKE_VERSION
    depForm->addRow(tr("CMake:"),
        valueLabel(QStringLiteral(HOBBYCAD_CMAKE_VERSION)));
#endif

#if defined(__GNUC__) && !defined(__clang__)
    depForm->addRow(tr("Compiler:"),
        valueLabel(QStringLiteral("GCC %1.%2.%3")
            .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__)));
#elif defined(__clang__)
    depForm->addRow(tr("Compiler:"),
        valueLabel(QStringLiteral("Clang %1.%2.%3")
            .arg(__clang_major__).arg(__clang_minor__)
            .arg(__clang_patchlevel__)));
#endif

    depForm->addRow(tr("OS:"),
        valueLabel(QStringLiteral("%1 (%2)")
            .arg(QSysInfo::prettyProductName(),
                 QSysInfo::currentCpuArchitecture())));

    depForm->addRow(tr("Host Info:"),
        valueLabel(QStringLiteral("%1 %2 %3 %4")
            .arg(QSysInfo::kernelType(),
                 QSysInfo::machineHostName(),
                 QSysInfo::kernelVersion(),
                 QSysInfo::currentCpuArchitecture())));

    layout->addWidget(depGroup);

    // Close button
    auto* closeBtn = new QPushButton(tr("Close"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    closeBtn->setFocus();
}

}  // namespace hobbycad

