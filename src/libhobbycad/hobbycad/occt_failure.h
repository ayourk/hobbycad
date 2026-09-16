// =====================================================================
//  occt_failure.h — the message of an OCCT exception on either major
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//
//  OpenCASCADE 8 derives Standard_Failure from std::exception and reports
//  through what(); 7.x has only GetMessageString(), which 8 deprecates.
//  HobbyCAD builds against both majors (see src/libhobbycad/CMakeLists.txt),
//  so every catch of Standard_Failure reads the message through this one
//  accessor instead of naming either method.
// =====================================================================
#pragma once

#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

namespace hobbycad {

inline const char* occtFailureMessage(const Standard_Failure& e) noexcept
{
#if OCC_VERSION_MAJOR >= 8
    const char* message = e.what();
#else
    const char* message = e.GetMessageString();
#endif
    return (message && *message) ? message : "unknown OCCT failure";
}

}  // namespace hobbycad
