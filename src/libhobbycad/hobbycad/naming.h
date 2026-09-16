// =====================================================================
//  src/libhobbycad/hobbycad/naming.h — what a valid object name is
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  One rule, one place. Names are checked by the objects browser, the CLI
//  and anything else that lets a user type one; each having its own idea
//  of "valid" is how a name that one accepts becomes a name another
//  cannot display.
//
#ifndef HOBBYCAD_NAMING_H
#define HOBBYCAD_NAMING_H

#include <string>

namespace hobbycad {

/// The character that marks unsaved edits in the CLI prompt.
///
/// Aaron, 2026-08-27: *"I'd argue that the * should go before the name and
/// make * an illegal first character."* Both halves matter together: the
/// marker is only unambiguous while no name can start with it. `*Profile`
/// then means "Profile, being edited" and can mean nothing else.
constexpr char kEditMarker = '*';

/// Whether @p name may be given to a sketch, body, plane or design.
///
/// @param reason  Filled with a user-facing explanation when false.
bool isValidObjectName(const std::string& name, std::string* reason = nullptr);

}  // namespace hobbycad

#endif  // HOBBYCAD_NAMING_H
