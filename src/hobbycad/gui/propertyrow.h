// SPDX-License-Identifier: GPL-3.0-only
// HobbyCAD - propertyrow.h
// One way to add a row to a properties tree. The panels used to spell out
// new item / setText(0) / setText(1) (/ flags / roles) at every row.

#pragma once

#include <hobbycad/sketch/property_schema.h>
#include <hobbycad/sketch/undo.h>

#include <QCoreApplication>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace hobbycad {

/// A read-only "label: value" row under `parent`.
inline QTreeWidgetItem* addPropertyRow(QTreeWidgetItem* parent, const QString& label, const QString& value)
{
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, label);
    item->setText(1, value);
    return item;
}

/// A read-only top-level row of `tree`.
inline QTreeWidgetItem* addPropertyRow(QTreeWidget* tree, const QString& label, const QString& value)
{
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, label);
    item->setText(1, value);
    return item;
}

/// An editable row whose edit is routed by (entityId, key) through the
/// UserRole / UserRole + 1 data of column 0.
inline QTreeWidgetItem* addEditablePropertyRow(QTreeWidgetItem* parent, const QString& label,
                                               const QString& value, int entityId, const QString& key)
{
    QTreeWidgetItem* item = addPropertyRow(parent, label, value);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setData(0, Qt::UserRole, entityId);
    item->setData(0, Qt::UserRole + 1, key);
    return item;
}

/// A property label from the library, translated, with its number filled in.
inline QString propertyLabelText(const sketch::FieldLabel& label)
{
    const QString text = QCoreApplication::translate(sketch::propertyLabelContext(), label.source);
    return label.number > 0 ? text.arg(label.number) : text;
}

/// An entity type's name, translated.
inline QString entityTypeText(sketch::EntityType type)
{
    return QCoreApplication::translate(sketch::entityTypeContext(), sketch::entityTypeName(type));
}

}  // namespace hobbycad
