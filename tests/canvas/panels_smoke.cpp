// =====================================================================
//  tests/canvas/panels_smoke.cpp — the transform form, the timeline and
//  the project browser on their library models
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Driven through the widgets:
//    - a point picked after the reference keeps a relative target relative
//      (it used to become the point's position, read as an offset);
//    - the first mirror-line point asks for the second;
//    - the timeline offers Suppress for a rolled-back feature and
//      Unsuppress for a suppressed one, and refuses a move past a
//      dependency;
//    - the project browser keeps a .gitignore's comments when it adds a
//      line, and a glob in it grays the files it matches.
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>

#include "projectbrowserwidget.h"
#include "sketchcanvas.h"
#include "sketchpropertieswidget.h"
#include "timelinewidget.h"

#include <hobbycad/project.h>

#include <cmath>
#include <cstdio>

using namespace hobbycad;
static int fails = 0;
static void ck(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

/// The field on the form row labeled `label`.
static QWidget* fieldFor(QWidget* root, const QString& label)
{
    for (QFormLayout* form : root->findChildren<QFormLayout*>()) {
        for (int r = 0; r < form->rowCount(); ++r) {
            QLayoutItem* l = form->itemAt(r, QFormLayout::LabelRole);
            QLayoutItem* f = form->itemAt(r, QFormLayout::FieldRole);
            auto* text = l ? qobject_cast<QLabel*>(l->widget()) : nullptr;
            if (text && f && text->text() == label) return f->widget();
        }
    }
    return nullptr;
}

static QComboBox* comboWith(QWidget* root, const QString& item)
{
    for (QComboBox* c : root->findChildren<QComboBox*>()) {
        if (c->findText(item) >= 0) return c;
    }
    return nullptr;
}

/// The texts of the context menu the timeline shows for `index`.
static QStringList timelineMenu(TimelineWidget& timeline, int index)
{
    QStringList texts;
    QTimer::singleShot(0, [&texts] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : menu->actions()) {
                if (!a->isSeparator()) texts << a->text();
            }
            menu->close();
        }
    });
    timeline.showItemContextMenu(index, QPoint(10, 10));
    return texts;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::printf("panels\n");

    // ---- the transform form -------------------------------------------------------
    {
        SketchCanvas canvas;
        canvas.resize(600, 400);
        canvas.setSketchContents({SketchEntity(sketch::createLine(1, {0, 0}, {10, 0}))}, {}, {});
        canvas.selectEntity(1);
        SketchPropertiesWidget props;
        props.setSketchCanvas(&canvas);
        props.show();
        props.updateForSelection();
        props.onTransformSectionRequested(static_cast<int>(sketch::TransformType::Move));

        using P = SketchCanvas::TransformPick;
        QComboBox* moveType = comboWith(&props, QStringLiteral("Point to position"));
        QComboBox* targetMode = comboWith(&props, QStringLiteral("Relative to reference"));
        ck(moveType && targetMode, "the form's combo boxes are there");
        if (!moveType || !targetMode) return 1;
        moveType->setCurrentIndex(moveType->findText(QStringLiteral("Point to position")));
        emit canvas.transformPickCompleted(int(P::PointOnSelection), QPointF(10, 0));
        auto* targetX =
            qobject_cast<QDoubleSpinBox*>(fieldFor(&props, QStringLiteral("Target X:")));
        ck(targetX && std::fabs(targetX->value() - 10.0) < 1e-9, "the target starts at the point");

        targetMode->setCurrentIndex(1);
        emit canvas.transformPickCompleted(int(P::ReferencePoint), QPointF(4, 0));
        auto* offsetX =
            qobject_cast<QDoubleSpinBox*>(fieldFor(&props, QStringLiteral("Offset X:")));
        ck(offsetX && std::fabs(offsetX->value() - 6.0) < 1e-9,
           "the offset starts where the point is, from the reference");
        emit canvas.transformPickCompleted(int(P::PointOnSelection), QPointF(0, 0));
        ck(offsetX && std::fabs(offsetX->value() + 4.0) < 1e-9,
           "a point picked after the reference keeps the offset relative");
        if (offsetX) offsetX->setValue(1.0);
        props.applyTransform();
        const auto& moved = canvas.entities()[0].points;
        ck(std::fabs(moved[0].x - 5.0) < 1e-9 && std::fabs(moved[1].x - 15.0) < 1e-9,
           "applying puts the point at the reference plus the offset");

        moveType->setCurrentIndex(moveType->findText(QStringLiteral("Mirror")));
        QComboBox* axis = comboWith(&props, QStringLiteral("Picked line"));
        if (axis) axis->setCurrentIndex(axis->findText(QStringLiteral("Picked line")));
        emit canvas.transformPickCompleted(int(P::MirrorA), QPointF(0, -5));
        QPushButton* pickB = nullptr;
        for (QPushButton* b : props.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("Pick line point B")) pickB = b;
        }
        ck(pickB && pickB->isChecked(), "the first mirror-line point asks for the second");
        emit canvas.transformPickCompleted(int(P::MirrorB), QPointF(0, 5));
        props.applyTransform();
        const auto& mirrored = canvas.entities()[0].points;
        ck(std::fabs(mirrored[0].x + 5.0) < 1e-9 && std::fabs(mirrored[1].x + 15.0) < 1e-9
               && std::fabs(mirrored[0].y) < 1e-9,
           "then it mirrors across the picked line");
    }

    // ---- the timeline -------------------------------------------------------------
    {
        TimelineWidget timeline;
        timeline.addItem(TimelineFeature::Origin, QStringLiteral("Origin"));
        timeline.addItem(TimelineFeature::Sketch, QStringLiteral("S"));
        timeline.addItem(TimelineFeature::Extrude, QStringLiteral("E"));
        timeline.addItem(TimelineFeature::Extrude, QStringLiteral("F"));
        for (int i = 0; i < 4; ++i) timeline.setFeatureId(i, i + 10);
        timeline.setDependencies(2, {11});
        ck(!timeline.canMoveItem(2, 1) && !timeline.canMoveItem(1, 2),
           "a move past a dependency is refused");
        ck(timeline.canMoveItem(3, 1) && !timeline.canMoveItem(3, 0),
           "other moves are allowed, but not ahead of the Origin");

        timeline.show();
        timeline.setRollbackPosition(1);
        const QStringList rolledBack = timelineMenu(timeline, 3);
        ck(rolledBack.contains(QStringLiteral("Suppress"))
               && !rolledBack.contains(QStringLiteral("Unsuppress")),
           "a rolled-back feature offers Suppress");
        timeline.setRollbackPosition(-1);
        timeline.setFeatureSuppressed(3, true);
        const QStringList suppressed = timelineMenu(timeline, 3);
        ck(suppressed.contains(QStringLiteral("Unsuppress")),
           "a suppressed feature offers Unsuppress");
        const QStringList sketchMenu = timelineMenu(timeline, 1);
        ck(sketchMenu.contains(QStringLiteral("Export as DXF...")), "a sketch offers export");
    }

    // ---- the project browser ---------------------------------------------------------
    {
        QTemporaryDir dir;
        const QString root = dir.path() + QStringLiteral("/proj");
        Project project;
        std::string error;
        ck(project.save(root.toStdString(), &error), "a project folder to browse");
        const QString written = QStringLiteral("# kept by hand\n\n*.log\n");
        {
            QFile f(root + QStringLiteral("/.gitignore"));
            f.open(QIODevice::WriteOnly);
            f.write(written.toUtf8());
            QFile log(root + QStringLiteral("/run.log"));
            log.open(QIODevice::WriteOnly);
            QFile notes(root + QStringLiteral("/notes.txt"));
            notes.open(QIODevice::WriteOnly);
        }

        ProjectBrowserWidget browser;
        browser.setProject(&project);
        auto* tree = browser.findChild<QTreeView*>();
        ck(tree != nullptr, "the browser has its tree");
        if (!tree) return 1;
        auto* model = qobject_cast<QFileSystemModel*>(tree->model());
        const QModelIndex log = model->index(root + QStringLiteral("/run.log"));
        const QModelIndex notes = model->index(root + QStringLiteral("/notes.txt"));
        ck(model->data(log, Qt::ToolTipRole).toString().contains(QStringLiteral("[Git Ignored]")),
           "a file a glob matches shows as ignored");
        ck(model->data(notes, Qt::ToolTipRole)
               .toString()
               .contains(QStringLiteral("[Untracked (not in manifest)]")),
           "another shows as untracked");

        tree->setCurrentIndex(notes);
        QAction* toggle = nullptr;
        for (QAction* a : browser.findChildren<QAction*>()) {
            if (a->toolTip() == QStringLiteral("Toggle .gitignore status")) toggle = a;
        }
        ck(toggle && toggle->isEnabled(), "the .gitignore toggle is offered");
        if (toggle) toggle->trigger();
        QFile f(root + QStringLiteral("/.gitignore"));
        f.open(QIODevice::ReadOnly);
        const QString after = QString::fromUtf8(f.readAll());
        ck(after == written + QStringLiteral("/notes.txt\n"),
           "adding a line keeps what was written by hand");
        ck(model->data(notes, Qt::ToolTipRole).toString().contains(QStringLiteral("[Git Ignored]")),
           "and the file shows as ignored");
    }

    std::printf("\n%s (%d failure(s))\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
