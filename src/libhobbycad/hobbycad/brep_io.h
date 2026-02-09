// =====================================================================
//  src/libhobbycad/hobbycad/brep_io.h — BREP file read/write utilities
// =====================================================================
//
//  Standalone BREP I/O functions that operate on TopoDS_Shape.
//  These are used by Document but can also be used independently.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_BREP_IO_H
#define HOBBYCAD_BREP_IO_H

#include "core.h"

#include <TopoDS_Shape.hxx>
#include <QString>
#include <QList>

namespace hobbycad {
namespace brep_io {

/// Read a BREP file and return the shapes it contains.
/// On failure, returns an empty list and sets errorMsg if non-null.
HOBBYCAD_EXPORT QList<TopoDS_Shape> readBrep(
    const QString& path,
    QString* errorMsg = nullptr);

/// Write shapes to a BREP file.
/// Returns true on success.  Sets errorMsg on failure.
HOBBYCAD_EXPORT bool writeBrep(
    const QString& path,
    const QList<TopoDS_Shape>& shapes,
    QString* errorMsg = nullptr);

/// Write a single shape to a BREP file.
HOBBYCAD_EXPORT bool writeBrep(
    const QString& path,
    const TopoDS_Shape& shape,
    QString* errorMsg = nullptr);

}  // namespace brep_io
}  // namespace hobbycad

#endif  // HOBBYCAD_BREP_IO_H

