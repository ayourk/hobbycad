// =====================================================================
//  src/libhobbycad/hobbycad/file_format.h — CAD file formats by name
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//  Which format a path or a command-line name means, written once so the
//  CLI's convert command and any other front end sniff the same way.
// =====================================================================

#ifndef HOBBYCAD_FILE_FORMAT_H
#define HOBBYCAD_FILE_FORMAT_H

#include "core.h"

#include <string>

namespace hobbycad {

enum class CadFileFormat { Unknown, Project, Brep, Stl, Step };

/// Format a path implies, by extension, ignoring case: ".hcad" or a
/// trailing '/' is a project directory, ".brep" / ".brp" BREP, ".stl",
/// ".step" / ".stp". `isDirectory` (what the filesystem says) also means
/// Project.
HOBBYCAD_EXPORT CadFileFormat fileFormatFromPath(const std::string& path, bool isDirectory = false);

/// Format named on a command line: "hcad", "brep" / "brp", "stl",
/// "step" / "stp". Ignores case; anything else is Unknown.
HOBBYCAD_EXPORT CadFileFormat fileFormatFromName(const std::string& name);

/// Canonical extension for a format (".brep" ...); "" for Unknown.
HOBBYCAD_EXPORT const char* fileFormatExtension(CadFileFormat format);

}  // namespace hobbycad

#endif  // HOBBYCAD_FILE_FORMAT_H
