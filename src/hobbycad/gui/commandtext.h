// =====================================================================
//  src/hobbycad/gui/commandtext.h — Qt text, icons and keys for commands
// =====================================================================
//
//  The command registry (hobbycad/commands.h) keeps English text with a
//  context and a disambiguation, icon names and nothing Qt. These turn a
//  command into what a Qt widget shows: translated text, a QIcon, and a
//  tooltip that names the key currently bound to it.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_COMMANDTEXT_H
#define HOBBYCAD_COMMANDTEXT_H

#include <hobbycad/bindings.h>
#include <hobbycad/commands.h>

#include <QHash>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QString>

class QStyle;

namespace hobbycad {
namespace commandtext {

/// The command with this id, or nullptr.
const commands::Command* find(const QString& id);

/// Registry text translated for display.
QString translate(const commands::Text& text);

/// The names the person gave elements in the Customize dialog, by
/// command id (layout::Customization::label). A name is shown exactly as
/// typed and never translated, so the text below returns it unchanged
/// where there is one. ArrangementStore keeps this up to date.
void setUserLabels(const QHash<QString, QString>& labels);

/// The person's own name for a command, or "" when they gave it none.
QString userLabel(const std::string& commandId);

/// The plain name ("Line"), or the person's own.
QString label(const commands::Command& command);

/// The menu entry, with its mnemonic; the label when it has none.
QString menuText(const commands::Command& command);

/// The toolbar caption, which may break lines; the label when it has none.
QString toolbarText(const commands::Command& command);

/// The tooltip, followed by the first key bound to the command when
/// `keys` has one and the command works yet ("Draw line (L)"); the label
/// when there is no tooltip.
QString tooltip(const commands::Command& command, const bindings::Table* keys);

/// The first key bound to a command, as the platform writes it; "" for
/// none.
QString keyText(const std::string& commandId, const bindings::Table& keys);

/// Every key sequence bound to a command.
QList<QKeySequence> keySequences(const std::string& commandId, const bindings::Table& keys);

/// The command's icon: its theme icon, or the style's standard pixmap it
/// names as a fallback.
QIcon icon(const commands::Command& command, const QStyle* style);

/// A default binding with any "std:<Name>" platform key spelled out for the
/// running platform (empty when the platform has none).
QString resolveDefault(const std::string& binding);

}  // namespace commandtext
}  // namespace hobbycad

#endif  // HOBBYCAD_COMMANDTEXT_H
