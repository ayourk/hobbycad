// =====================================================================
//  src/libhobbycad/hobbycad/qt_compat.h — Qt interop utilities
// =====================================================================
//
//  Explicit conversion functions between libhobbycad types and Qt types.
//  Include this header at the GUI/library boundary when you need to
//  convert between std::vector and QVector, std::string and QString, etc.
//
//  Note: Point2D, Rect2D, and Vec3 already have implicit conversion
//  operators when QT_CORE_LIB is defined (see types.h). This header
//  provides additional explicit conversions for containers and strings.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_QT_COMPAT_H
#define HOBBYCAD_QT_COMPAT_H

#include "types.h"  // defines HOBBYCAD_HAS_QT

#if HOBBYCAD_HAS_QT

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <string>
#include <vector>

namespace hobbycad {

// =====================================================================
//  Explicit Point / Rect Conversions
// =====================================================================

/// Convert Point2D to QPointF (explicit version of implicit operator)
inline QPointF toQt(const Point2D& p) { return QPointF(p.x, p.y); }

/// Convert QPointF to Point2D (explicit version of implicit constructor)
inline Point2D fromQt(const QPointF& p) { return {p.x(), p.y()}; }

/// Convert Rect2D to QRectF
inline QRectF toQt(const Rect2D& r) { return QRectF(r.x, r.y, r.width, r.height); }

/// Convert QRectF to Rect2D
inline Rect2D fromQt(const QRectF& r) { return {r.x(), r.y(), r.width(), r.height()}; }

// =====================================================================
//  String Conversions
// =====================================================================

/// Convert std::string to QString
inline QString toQt(const std::string& s) { return QString::fromStdString(s); }

/// Convert QString to std::string
inline std::string fromQt(const QString& s) { return s.toStdString(); }

// =====================================================================
//  Container Conversions
// =====================================================================

/// Convert std::vector<T> to QVector<T>
template<typename T>
QVector<T> toQVector(const std::vector<T>& v) {
    return QVector<T>(v.begin(), v.end());
}

/// Convert QVector<T> to std::vector<T>
template<typename T>
std::vector<T> fromQVector(const QVector<T>& v) {
    return std::vector<T>(v.begin(), v.end());
}

/// Convert std::vector<Point2D> to QVector<QPointF>
inline QVector<QPointF> toQtPoints(const std::vector<Point2D>& pts) {
    QVector<QPointF> result;
    result.reserve(static_cast<int>(pts.size()));
    for (const auto& p : pts)
        result.append(QPointF(p.x, p.y));
    return result;
}

/// Convert QVector<QPointF> to std::vector<Point2D>
inline std::vector<Point2D> fromQtPoints(const QVector<QPointF>& pts) {
    std::vector<Point2D> result;
    result.reserve(static_cast<size_t>(pts.size()));
    for (const auto& p : pts)
        result.push_back({p.x(), p.y()});
    return result;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_HAS_QT
#endif  // HOBBYCAD_QT_COMPAT_H
