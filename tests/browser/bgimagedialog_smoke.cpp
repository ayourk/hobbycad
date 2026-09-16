// =====================================================================
//  tests/browser/bgimagedialog_smoke.cpp — background image dialogs
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Two dialogs that were compiled into the binary but never instantiated
//  by anything. Wiring them up introduced one real hazard, which is what
//  this file guards.
//
//  The hazard: BackgroundImageDialog loaded images through
//  sketch::loadBackgroundImage(path, embed), which stores whatever path
//  it was handed. The properties panel it replaced used
//  sketch::updateBackgroundFromFile(path, projectDir), which stores a
//  project-RELATIVE path for a file inside the project. Swapping one for
//  the other without care would have written absolute paths into project
//  files: projects that load on the machine that made them and nowhere
//  else, with no error to say why.
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>

#include "backgroundimagedialog.h"

#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

/// A real PNG on disk; the dialog validates with QImageReader, so a
/// fabricated file would be rejected before any of this is reached.
static bool writePng(const QString& path) {
    QImage img(64, 32, QImage::Format_RGB32);
    img.fill(Qt::blue);
    return img.save(path, "PNG");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QTemporaryDir projectDir;
    QTemporaryDir elsewhere;
    if (!projectDir.isValid() || !elsewhere.isValid()) {
        std::printf("  [FAIL] could not create temp dirs\n");
        return 1;
    }

    const QString inside  = projectDir.path() + "/inside.png";
    const QString outside = elsewhere.path() + "/outside.png";
    ck(writePng(inside) && writePng(outside), "test images written");

    // ---- A file INSIDE the project is referenced, relatively -------------
    {
        BackgroundImageDialog dlg;
        dlg.setProjectDir(projectDir.path());
        dlg.loadImage(inside);

        const auto bg = dlg.backgroundImage();
        ck(bg.enabled, "an image inside the project loads");
        ck(bg.storage == sketch::BackgroundStorage::FilePath,
           "and is referenced, not embedded");

        const QString stored = QString::fromStdString(bg.filePath);
        ck(!QDir::isAbsolutePath(stored),
           "and its path is RELATIVE, so the project survives being moved");
        ck(stored.contains("inside.png"),
           "and still names the file it came from");
    }

    // ---- A file OUTSIDE the project is embedded --------------------------
    {
        BackgroundImageDialog dlg;
        dlg.setProjectDir(projectDir.path());
        dlg.loadImage(outside);

        const auto bg = dlg.backgroundImage();
        ck(bg.enabled, "an image outside the project loads");
        ck(bg.storage == sketch::BackgroundStorage::Embedded,
           "and is embedded, because a path outside the project is not "
           "portable");
        ck(!bg.imageData.empty(), "with the bytes actually carried along");
    }

    // ---- No project directory yet: embedding is the only safe answer -----
    {
        BackgroundImageDialog dlg;              // no setProjectDir()
        dlg.loadImage(inside);

        const auto bg = dlg.backgroundImage();
        ck(bg.enabled && bg.storage == sketch::BackgroundStorage::Embedded,
           "an unsaved project embeds, having no directory to be inside of");
    }

    // BackgroundCalibrationDialog is deliberately NOT covered here.
    // Its translation unit references SketchCanvas (signals, slots and
    // staticMetaObject), so linking it drags in the canvas and most of the
    // app behind it, which is more than a smoke test should carry. Its one
    // load-bearing property, that it must never be shown modally (the user
    // has to click the canvas while it is open), is a fact about the CALL
    // SITE rather than the dialog, and is documented at the declaration of
    // MainWindow::onCalibrateBackground().

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
