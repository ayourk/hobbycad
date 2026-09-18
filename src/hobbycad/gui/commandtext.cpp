// =====================================================================
//  src/hobbycad/gui/commandtext.cpp — Qt text, icons and keys for commands
// =====================================================================

#include "commandtext.h"

#include <QCoreApplication>
#include <QMetaEnum>
#include <QStyle>

namespace hobbycad {
namespace commandtext {

const commands::Command* find(const QString& id)
{
    return commands::findCommand(id.toStdString());
}

QString translate(const commands::Text& text)
{
    if (text.empty()) return QString();
    return QCoreApplication::translate(commands::commandContext(), text.source,
                                       text.disambiguation);
}

namespace {
/// The names the person gave things. Not translated, by design.
QHash<QString, QString>& userLabels()
{
    static QHash<QString, QString> labels;
    return labels;
}
}  // namespace

void setUserLabels(const QHash<QString, QString>& labels)
{
    userLabels() = labels;
}

QString userLabel(const std::string& commandId)
{
    return userLabels().value(QString::fromStdString(commandId));
}

QString label(const commands::Command& command)
{
    const QString own = userLabel(command.id);
    return own.isEmpty() ? translate(command.label) : own;
}

QString menuText(const commands::Command& command)
{
    if (!userLabel(command.id).isEmpty()) return label(command);
    return command.menuText.empty() ? label(command) : translate(command.menuText);
}

QString toolbarText(const commands::Command& command)
{
    if (!userLabel(command.id).isEmpty()) return label(command);
    return command.toolbarText.empty() ? label(command) : translate(command.toolbarText);
}

QString tooltip(const commands::Command& command, const bindings::Table* keys)
{
    const QString own = userLabel(command.id);
    const QString tip = !own.isEmpty() ? own
                      : command.tooltip.empty() ? label(command) : translate(command.tooltip);
    // A key reserved for a command that does not work yet does nothing, so
    // it is not advertised.
    const bool usable = !(command.needs & commands::NotImplemented);
    const QString key = (keys && usable) ? keyText(command.id, *keys) : QString();
    if (key.isEmpty()) return tip;
    //: A tooltip followed by the key that runs the command: "Draw line (L)".
    return QCoreApplication::translate("hobbycad::Commands", "%1 (%2)", "tooltip.withKey")
        .arg(tip, key);
}

QList<QKeySequence> keySequences(const std::string& commandId, const bindings::Table& keys)
{
    QList<QKeySequence> out;
    for (const std::string& s : bindings::keySequences(keys.current(commandId))) {
        const QKeySequence seq(QString::fromStdString(s), QKeySequence::PortableText);
        if (!seq.isEmpty()) out.append(seq);
    }
    return out;
}

QString keyText(const std::string& commandId, const bindings::Table& keys)
{
    const QList<QKeySequence> seqs = keySequences(commandId, keys);
    return seqs.isEmpty() ? QString() : seqs.first().toString(QKeySequence::NativeText);
}

QIcon icon(const commands::Command& command, const QStyle* style)
{
    QIcon fallback;
    if (style && *command.iconFallback) {
        const QMetaEnum pixmaps = QMetaEnum::fromType<QStyle::StandardPixmap>();
        bool ok = false;
        const int value = pixmaps.keyToValue(command.iconFallback, &ok);
        if (ok) fallback = style->standardIcon(static_cast<QStyle::StandardPixmap>(value));
    }
    if (!*command.icon) return fallback;
    return QIcon::fromTheme(QString::fromLatin1(command.icon), fallback);
}

QString resolveDefault(const std::string& binding)
{
    const std::string name = bindings::standardKeyName(binding);
    if (name.empty()) return QString::fromStdString(binding);
    // The platform keys the default arrangement names, spelled out by Qt
    // for the running platform.
    static const struct {
        const char* name;
        QKeySequence::StandardKey key;
    } kKeys[] = {
        {"New", QKeySequence::New},         {"Open", QKeySequence::Open},
        {"Save", QKeySequence::Save},       {"SaveAs", QKeySequence::SaveAs},
        {"Close", QKeySequence::Close},     {"Quit", QKeySequence::Quit},
        {"Undo", QKeySequence::Undo},       {"Redo", QKeySequence::Redo},
        {"Cut", QKeySequence::Cut},         {"Copy", QKeySequence::Copy},
        {"Paste", QKeySequence::Paste},     {"Delete", QKeySequence::Delete},
        {"SelectAll", QKeySequence::SelectAll},
        {"Preferences", QKeySequence::Preferences},
    };
    for (const auto& k : kKeys) {
        if (name == k.name) return QKeySequence(k.key).toString();
    }
    return QString();
}

}  // namespace commandtext
}  // namespace hobbycad
