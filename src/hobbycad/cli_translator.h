// =====================================================================
//  src/hobbycad/cli_translator.h — Qt translation for the CLI
// =====================================================================
//
//  Installs the Qt translator the Qt-free command layer asks for
//  through hobbycad::translate(). Application-side only: libhobbycad
//  neither knows nor needs this.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_CLI_TRANSLATOR_H
#define HOBBYCAD_CLI_TRANSLATOR_H

namespace hobbycad {

/// Route the command layer's messages through QCoreApplication. Safe to
/// call more than once; call it before the first command runs.
void installCliTranslator();

}  // namespace hobbycad

#endif  // HOBBYCAD_CLI_TRANSLATOR_H
