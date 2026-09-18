// =====================================================================
//  src/hobbycad/gui/arrangementstore.cpp — the arrangement this front end
//  shows
// =====================================================================
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include "arrangementstore.h"

#include "commandtext.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace hobbycad {

namespace {

constexpr const char* kFileName = "arrangement.json";
/// Set once the bindings that used to live in QSettings have been taken
/// over by the arrangement file.
constexpr const char* kAdoptedKey = "bindings/inArrangementFile";

}  // namespace

ArrangementStore& ArrangementStore::instance()
{
    static ArrangementStore store;
    return store;
}

ArrangementStore::ArrangementStore()
{
    reload();
    adoptSettingsBindings();
}

const layout::Arrangement& ArrangementStore::base() const
{
    return layout::defaultArrangement();
}

QString ArrangementStore::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QLatin1Char('/') + QLatin1String(kFileName);
}

void ArrangementStore::reload()
{
    m_customizations = layout::Customizations();
    m_problems.clear();
    QFile file(filePath());
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            m_problems << tr("The arrangement file cannot be read; HobbyCAD's own layout is "
                             "shown.");
        } else {
            const QByteArray text = file.read(
                static_cast<qint64>(layout::kMaxFileBytes) + 1);
            const layout::ReadResult read =
                layout::readCustomizations(std::string(text.constData(),
                                                       static_cast<std::size_t>(text.size())),
                                           base());
            for (const std::string& problem : read.problems) {
                m_problems << QString::fromStdString(problem);
            }
            if (read.usable) {
                m_customizations = read.customizations;
            } else {
                m_problems << tr("HobbyCAD's own layout is shown until the file is fixed.");
            }
        }
    }
    rebuild();
}

void ArrangementStore::rebuild()
{
    m_resolved = layout::resolve(base(), m_customizations);
    // The names the person gave things, so every widget shows them.
    QHash<QString, QString> labels;
    for (const layout::Element& element : m_resolved.elements()) {
        if (element.userLabel.empty()) continue;
        labels.insert(QString::fromStdString(element.id),
                      QString::fromStdString(element.userLabel));
    }
    commandtext::setUserLabels(labels);
}

void ArrangementStore::save()
{
    const std::string text = layout::writeCustomizations(m_customizations);
    const QString path = filePath();
    if (text.empty()) {
        // Nothing is customized any more, so nothing is stored.
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.commit();
}

void ArrangementStore::adoptSettingsBindings()
{
    QSettings settings;
    if (settings.value(QLatin1String(kAdoptedKey)).toBool()) return;
    settings.setValue(QLatin1String(kAdoptedKey), true);
    if (!m_customizations.bindings().empty()) return;

    // The keys a person set before the arrangement file existed.
    settings.beginGroup(QStringLiteral("bindings"));
    const QStringList keys = settings.childGroups();
    bool took = false;
    for (const QString& commandId : keys) {
        const std::string id = commandId.toStdString();
        bindings::Slots keys{};
        for (const auto& pair : base().defaultBindings()) {
            if (pair.first == id) keys = pair.second;
        }
        bool changedAny = false;
        for (int slot = 0; slot < bindings::kSlotCount; ++slot) {
            const QString key = commandId + QLatin1Char('/') + QString::number(slot + 1);
            if (!settings.contains(key)) continue;
            keys[static_cast<std::size_t>(slot)] = settings.value(key).toString().toStdString();
            changedAny = true;
        }
        if (!changedAny) continue;
        m_customizations.setBinding(id, keys);
        took = true;
    }
    settings.endGroup();
    if (!took) return;
    rebuild();
    save();
}

QString ArrangementStore::elementText(const layout::Element& element) const
{
    if (!element.userLabel.empty()) return QString::fromStdString(element.userLabel);
    if (const commands::Command* command = commands::findCommand(element.id)) {
        return commandtext::menuText(*command);
    }
    return QString();
}

bool ArrangementStore::move(const QString& key, const QString& container, int index)
{
    if (!m_customizations.move(m_resolved, key.toStdString(), container.toStdString(), index)) {
        return false;
    }
    rebuild();
    save();
    emit changed();
    return true;
}

void ArrangementStore::setHidden(const QString& key, bool hidden)
{
    // Showing something HobbyCAD shows anyway is not a customization.
    const layout::Element* element = base().element(key.toStdString());
    if (element && element->hidden == hidden) {
        m_customizations.clearHidden(key.toStdString());
    } else {
        m_customizations.setHidden(key.toStdString(), hidden);
    }
    rebuild();
    save();
    emit changed();
}

QString ArrangementStore::addSeparator(const QString& container, int index)
{
    const std::string key =
        m_customizations.addSeparator(m_resolved, container.toStdString(), index);
    if (key.empty()) return QString();
    rebuild();
    save();
    emit changed();
    return QString::fromStdString(key);
}

void ArrangementStore::setLabel(const QString& key, const QString& label)
{
    m_customizations.setLabel(key.toStdString(), label.toStdString());
    rebuild();
    save();
    emit changed();
}

namespace {

/// A command's default bindings, with any platform key ("std:Save")
/// spelled out as the dialog shows it, so "same as the default" compares
/// like with like.
bindings::Slots resolvedDefaults(const layout::Arrangement& base, const std::string& commandId)
{
    bindings::Slots keys{};
    for (const auto& pair : base.defaultBindings()) {
        if (pair.first != commandId) continue;
        for (int i = 0; i < bindings::kSlotCount; ++i) {
            const std::size_t at = static_cast<std::size_t>(i);
            keys[at] = commandtext::resolveDefault(pair.second[at]).toStdString();
        }
    }
    return keys;
}

}  // namespace

void ArrangementStore::setBinding(const QString& commandId, const bindings::Slots& keys)
{
    QHash<QString, bindings::Slots> one;
    one.insert(commandId, keys);
    setBindings(one);
}

void ArrangementStore::setBindings(const QHash<QString, bindings::Slots>& keys)
{
    bool any = false;
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        const std::string id = it.key().toStdString();
        if (it.value() == resolvedDefaults(base(), id)) {
            any = m_customizations.restoreBinding(id) || any;
        } else {
            m_customizations.setBinding(id, it.value());
            any = true;
        }
    }
    if (!any) return;
    rebuild();
    save();
    emit changed();
}

bool ArrangementStore::restoreElement(const QString& key)
{
    if (!m_customizations.restoreElement(key.toStdString())) return false;
    rebuild();
    save();
    emit changed();
    return true;
}

std::size_t ArrangementStore::restore(layout::RestoreArea area)
{
    const std::size_t gone = m_customizations.restoreArea(area, base());
    if (gone == 0) return 0;
    rebuild();
    save();
    emit changed();
    return gone;
}

bool ArrangementStore::exportTo(const QString& path, QString* error)
{
    const std::string text = layout::exportArrangement(m_resolved);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(text.data(), static_cast<qint64>(text.size()));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool ArrangementStore::importFrom(const QString& path, QStringList* problems)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (problems) *problems << tr("The file cannot be read.");
        return false;
    }
    const QByteArray text = file.read(static_cast<qint64>(layout::kMaxFileBytes) + 1);
    const layout::ReadResult read =
        layout::importArrangement(std::string(text.constData(),
                                              static_cast<std::size_t>(text.size())),
                                  base());
    if (problems) {
        for (const std::string& problem : read.problems) {
            *problems << QString::fromStdString(problem);
        }
    }
    if (!read.usable) return false;
    m_customizations = read.customizations;
    rebuild();
    save();
    emit changed();
    return true;
}

}  // namespace hobbycad
