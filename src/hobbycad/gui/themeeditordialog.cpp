// =====================================================================
//  src/hobbycad/gui/themeeditordialog.cpp
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "themeeditordialog.h"
#include "colorpicker.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <map>

namespace hobbycad {

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent) : QDialog(parent)
{
    // Edit whatever theme is on screen now, so every change is visible live.
    const QVariant flag = qApp ? qApp->property("hobbycad_dark_theme") : QVariant();
    m_dark = flag.isValid() ? flag.toBool() : false;

    setWindowTitle(m_dark ? tr("Theme Editor (Dark)")
                          : tr("Theme Editor (Light)"));
    resize(560, 460);

    auto* root = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({ tr("Role"), QString() });
    m_tree->setColumnWidth(0, 230);
    m_tree->setRootIsDecorated(true);
    row->addWidget(m_tree, 1);

    auto* right = new QVBoxLayout;
    m_fieldLabel = new QLabel(tr("Select a color role"), this);
    m_fieldLabel->setWordWrap(true);
    right->addWidget(m_fieldLabel);
    m_picker = new ColorPicker(this);
    m_picker->setEnabled(false);
    right->addWidget(m_picker);
    m_resetFieldBtn = new QPushButton(tr("Reset this role"), this);
    m_resetFieldBtn->setEnabled(false);
    right->addWidget(m_resetFieldBtn);
    right->addStretch(1);
    row->addLayout(right, 0);

    root->addLayout(row, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_restoreBtn = buttons->addButton(tr("Restore Defaults"),
                                      QDialogButtonBox::ResetRole);
    root->addWidget(buttons);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ThemeEditorDialog::onSelectionChanged);
    connect(m_picker, &ColorPicker::colorChanged, this,
            &ThemeEditorDialog::onColorChanged);
    connect(m_resetFieldBtn, &QPushButton::clicked, this,
            &ThemeEditorDialog::onResetField);
    connect(m_restoreBtn, &QPushButton::clicked, this,
            &ThemeEditorDialog::onRestoreDefaults);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    populate();
}

SketchTheme ThemeEditorDialog::effectiveTheme() const
{
    SketchTheme t = m_dark ? SketchTheme::dark() : SketchTheme::light();
    applySketchThemeOverrides(t, m_dark);
    return t;
}

const SketchThemeField* ThemeEditorDialog::fieldByKey(const QString& key) const
{
    for (const SketchThemeField& f : sketchThemeFields())
        if (key == QString::fromLatin1(f.field)) return &f;
    return nullptr;
}

static QIcon swatch(const QColor& c)
{
    QPixmap pm(24, 16);
    pm.fill(c);
    return QIcon(pm);
}

void ThemeEditorDialog::populate()
{
    m_tree->clear();
    const SketchTheme t = effectiveTheme();
    std::map<QString, QTreeWidgetItem*> cats;
    for (const SketchThemeField& f : sketchThemeFields()) {
        const QString cat = QString::fromLatin1(f.category);
        auto it = cats.find(cat);
        QTreeWidgetItem* parent;
        if (it == cats.end()) {
            parent = new QTreeWidgetItem(m_tree, { cat });
            parent->setFlags(parent->flags() & ~Qt::ItemIsSelectable);
            parent->setExpanded(true);
            cats[cat] = parent;
        } else {
            parent = it->second;
        }
        auto* leaf = new QTreeWidgetItem(parent, { QString::fromLatin1(f.display) });
        leaf->setData(0, Qt::UserRole, QString::fromLatin1(f.field));
        leaf->setIcon(0, swatch(t.*(f.member)));
    }
}

void ThemeEditorDialog::refreshSwatches()
{
    const SketchTheme t = effectiveTheme();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = m_tree->topLevelItem(i);
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* leaf = cat->child(j);
            const QString key = leaf->data(0, Qt::UserRole).toString();
            if (const SketchThemeField* f = fieldByKey(key))
                leaf->setIcon(0, swatch(t.*(f->member)));
        }
    }
}

void ThemeEditorDialog::onSelectionChanged()
{
    auto items = m_tree->selectedItems();
    if (items.isEmpty() || items.first()->childCount() > 0) {
        m_selectedKey.clear();
        m_picker->setEnabled(false);
        m_resetFieldBtn->setEnabled(false);
        m_fieldLabel->setText(tr("Select a color role"));
        return;
    }
    m_selectedKey = items.first()->data(0, Qt::UserRole).toString();
    const SketchThemeField* f = fieldByKey(m_selectedKey);
    if (!f) return;
    m_fieldLabel->setText(tr("%1: %2")
                              .arg(QString::fromLatin1(f->category),
                                   QString::fromLatin1(f->display)));
    m_picker->setEnabled(true);
    m_resetFieldBtn->setEnabled(
        QSettings().value(sketchThemeOverrideKey(m_dark, f->field)).isValid());
    m_updating = true;
    m_picker->setColor(effectiveTheme().*(f->member));
    m_picker->anchorPrevious();
    m_updating = false;
}

void ThemeEditorDialog::onColorChanged(const QColor& c)
{
    if (m_updating || m_selectedKey.isEmpty()) return;
    const SketchThemeField* f = fieldByKey(m_selectedKey);
    if (!f) return;
    QSettings().setValue(sketchThemeOverrideKey(m_dark, f->field),
                         c.name(QColor::HexRgb));
    m_resetFieldBtn->setEnabled(true);
    refreshSwatches();
    emit themeApplied();   // hot swap: canvases re-apply immediately
}

void ThemeEditorDialog::onResetField()
{
    if (m_selectedKey.isEmpty()) return;
    const SketchThemeField* f = fieldByKey(m_selectedKey);
    if (!f) return;
    QSettings().remove(sketchThemeOverrideKey(m_dark, f->field));
    m_resetFieldBtn->setEnabled(false);
    m_updating = true;
    m_picker->setColor(effectiveTheme().*(f->member));
    m_updating = false;
    refreshSwatches();
    emit themeApplied();
}

void ThemeEditorDialog::onRestoreDefaults()
{
    QSettings s;
    for (const SketchThemeField& f : sketchThemeFields())
        s.remove(sketchThemeOverrideKey(m_dark, f.field));
    refreshSwatches();
    if (!m_selectedKey.isEmpty()) {
        if (const SketchThemeField* f = fieldByKey(m_selectedKey)) {
            m_updating = true;
            m_picker->setColor(effectiveTheme().*(f->member));
            m_updating = false;
            m_resetFieldBtn->setEnabled(false);
        }
    }
    emit themeApplied();
}

}  // namespace hobbycad
