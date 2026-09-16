// =====================================================================
//  src/libhobbycad/file_format.cpp — CAD file formats by name
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================

#include "hobbycad/file_format.h"
#include "hobbycad/format.h"

namespace hobbycad {

namespace {

bool hasExtension(const std::string& path, const char* ext)
{
    const std::string e(ext);
    if (path.size() < e.size()) return false;
    return equalsIgnoreCase(path.substr(path.size() - e.size()), e);
}

}  // namespace

CadFileFormat fileFormatFromPath(const std::string& path, bool isDirectory)
{
    if (isDirectory || hasExtension(path, "/") || hasExtension(path, ".hcad")) return CadFileFormat::Project;
    if (hasExtension(path, ".brep") || hasExtension(path, ".brp")) return CadFileFormat::Brep;
    if (hasExtension(path, ".stl")) return CadFileFormat::Stl;
    if (hasExtension(path, ".step") || hasExtension(path, ".stp")) return CadFileFormat::Step;
    return CadFileFormat::Unknown;
}

CadFileFormat fileFormatFromName(const std::string& name)
{
    if (equalsIgnoreCase(name, "hcad")) return CadFileFormat::Project;
    if (equalsIgnoreCase(name, "brep") || equalsIgnoreCase(name, "brp")) return CadFileFormat::Brep;
    if (equalsIgnoreCase(name, "stl")) return CadFileFormat::Stl;
    if (equalsIgnoreCase(name, "step") || equalsIgnoreCase(name, "stp")) return CadFileFormat::Step;
    return CadFileFormat::Unknown;
}

const char* fileFormatExtension(CadFileFormat format)
{
    switch (format) {
    case CadFileFormat::Project: return ".hcad";
    case CadFileFormat::Brep:    return ".brep";
    case CadFileFormat::Stl:     return ".stl";
    case CadFileFormat::Step:    return ".step";
    case CadFileFormat::Unknown: break;
    }
    return "";
}

}  // namespace hobbycad
