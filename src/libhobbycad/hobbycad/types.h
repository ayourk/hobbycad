// =====================================================================
//  src/libhobbycad/hobbycad/types.h — Core data types for libhobbycad
// =====================================================================
//
//  Qt-free fundamental types used throughout the library.
//  When Qt is available (QT_CORE_LIB defined), implicit conversions
//  to/from Qt types are provided for seamless interoperability.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TYPES_H
#define HOBBYCAD_TYPES_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#ifdef QT_CORE_LIB
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 2)
#define HOBBYCAD_HAS_QT 1
#include <QPointF>
#include <QRectF>
#include <QVector3D>
#else
#define HOBBYCAD_HAS_QT 0
#endif
#else
#define HOBBYCAD_HAS_QT 0
#endif

namespace hobbycad {

// =====================================================================
//  Point2D — replaces QPointF
// =====================================================================

/// 2D point / vector with double precision.
/// Direct member access (p.x, p.y) instead of accessor functions.
struct Point2D {
    double x = 0.0;
    double y = 0.0;

    constexpr Point2D() = default;
    constexpr Point2D(double x_, double y_) : x(x_), y(y_) {}

    // Arithmetic operators
    constexpr Point2D operator+(const Point2D& rhs) const { return {x + rhs.x, y + rhs.y}; }
    constexpr Point2D operator-(const Point2D& rhs) const { return {x - rhs.x, y - rhs.y}; }
    constexpr Point2D operator*(double s) const { return {x * s, y * s}; }
    constexpr Point2D operator/(double s) const { return {x / s, y / s}; }
    constexpr Point2D operator-() const { return {-x, -y}; }

    Point2D& operator+=(const Point2D& rhs) { x += rhs.x; y += rhs.y; return *this; }
    Point2D& operator-=(const Point2D& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    Point2D& operator*=(double s) { x *= s; y *= s; return *this; }
    Point2D& operator/=(double s) { x /= s; y /= s; return *this; }

    constexpr bool operator==(const Point2D& rhs) const { return x == rhs.x && y == rhs.y; }
    constexpr bool operator!=(const Point2D& rhs) const { return !(*this == rhs); }

    /// Lexicographic ordering (for std::sort in convex hull, etc.)
    constexpr bool operator<(const Point2D& rhs) const {
        return (x < rhs.x) || (x == rhs.x && y < rhs.y);
    }

    /// Check if both components are zero
    constexpr bool isNull() const { return x == 0.0 && y == 0.0; }

#if HOBBYCAD_HAS_QT
    /// Implicit conversion from QPointF
    Point2D(const QPointF& qp) : x(qp.x()), y(qp.y()) {}  // NOLINT(google-explicit-constructor)

    /// Implicit conversion to QPointF
    operator QPointF() const { return QPointF(x, y); }  // NOLINT(google-explicit-constructor)
#endif
};

/// Scalar * Point2D (allows 2.0 * point syntax)
constexpr Point2D operator*(double s, const Point2D& p) { return {s * p.x, s * p.y}; }

struct PlaneBasis;  // defined below; used by Point3::world/project

// =====================================================================
//  Point3 — a sketch point in 3D
// =====================================================================
//  Sketch points are 3D internally; a 2D sketch is the special case z == 0
//  (the UI hides the third component in 2D mode). Point3 interconverts
//  IMPLICITLY with Point2D (a 2D point lifts to z == 0, and dropping to 2D
//  discards z), so the large body of existing 2D-point code keeps compiling
//  while the storage is 3D. Arithmetic here is genuine 3D; use .xy() when a
//  2D value is meant explicitly.
struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Point3() = default;
    constexpr Point3(double x_, double y_, double z_ = 0.0) : x(x_), y(y_), z(z_) {}
    constexpr Point3(const Point2D& p) : x(p.x), y(p.y), z(0.0) {}  // NOLINT implicit lift
    constexpr operator Point2D() const { return {x, y}; }           // NOLINT implicit drop
    constexpr Point2D xy() const { return {x, y}; }

    constexpr Point3 operator+(const Point3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    constexpr Point3 operator-(const Point3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    constexpr Point3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Point3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Point3 operator-() const { return {-x, -y, -z}; }
    Point3& operator+=(const Point3& r) { x += r.x; y += r.y; z += r.z; return *this; }
    Point3& operator-=(const Point3& r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
    Point3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Point3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }
    constexpr bool operator==(const Point3& r) const { return x == r.x && y == r.y && z == r.z; }
    constexpr bool operator!=(const Point3& r) const { return !(*this == r); }
    constexpr bool operator<(const Point3& r) const {
        return (x < r.x) || (x == r.x && (y < r.y || (y == r.y && z < r.z)));
    }
    constexpr bool isNull() const { return x == 0.0 && y == 0.0 && z == 0.0; }

    /// Plane <-> world (see PlaneBasis), the way dune3d/Fusion expose it.
    /// world():  treat this as a plane-local (u, v, w) point -> WORLD point.
    /// project(): treat this as a WORLD point -> its plane-local (u, v, w).
    /// Defined out-of-line below PlaneBasis. Inverses of each other.
    Point3 world(const PlaneBasis& basis) const;
    Point3 project(const PlaneBasis& basis) const;

#if HOBBYCAD_HAS_QT
    Point3(const QPointF& q) : x(q.x()), y(q.y()), z(0.0) {}  // NOLINT
    operator QPointF() const { return QPointF(x, y); }        // NOLINT
#endif
};
constexpr Point3 operator*(double s, const Point3& p) { return {s * p.x, s * p.y, s * p.z}; }

// =====================================================================
//  Rect2D — replaces QRectF
// =====================================================================

/// Axis-aligned rectangle defined by top-left corner and dimensions.
struct Rect2D {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    constexpr Rect2D() = default;
    constexpr Rect2D(double x_, double y_, double w_, double h_)
        : x(x_), y(y_), width(w_), height(h_) {}

    /// Construct from two corner points
    static Rect2D fromCorners(double x1, double y1, double x2, double y2) {
        double minX = (x1 < x2) ? x1 : x2;
        double minY = (y1 < y2) ? y1 : y2;
        return {minX, minY, std::abs(x2 - x1), std::abs(y2 - y1)};
    }

    /// Construct from two Point2D corners
    static Rect2D fromPoints(const Point2D& p1, const Point2D& p2) {
        return fromCorners(p1.x, p1.y, p2.x, p2.y);
    }

    constexpr double left() const { return x; }
    constexpr double top() const { return y; }
    constexpr double right() const { return x + width; }
    constexpr double bottom() const { return y + height; }
    constexpr Point2D topLeft() const { return {x, y}; }
    constexpr Point2D topRight() const { return {x + width, y}; }
    constexpr Point2D bottomLeft() const { return {x, y + height}; }
    constexpr Point2D bottomRight() const { return {x + width, y + height}; }
    constexpr Point2D center() const { return {x + width * 0.5, y + height * 0.5}; }

    /// Check if a point is inside (inclusive on all edges)
    bool contains(const Point2D& point) const {
        return point.x >= x && point.x <= x + width &&
               point.y >= y && point.y <= y + height;
    }

    /// Check if another rectangle is fully inside this one
    bool contains(const Rect2D& other) const {
        return other.x >= x && other.x + other.width <= x + width &&
               other.y >= y && other.y + other.height <= y + height;
    }

    /// Return a rectangle adjusted by the given deltas
    Rect2D adjusted(double dx1, double dy1, double dx2, double dy2) const {
        return {x + dx1, y + dy1, width - dx1 + dx2, height - dy1 + dy2};
    }

    constexpr bool operator==(const Rect2D& rhs) const {
        return x == rhs.x && y == rhs.y && width == rhs.width && height == rhs.height;
    }
    constexpr bool operator!=(const Rect2D& rhs) const { return !(*this == rhs); }

#if HOBBYCAD_HAS_QT
    /// Implicit conversion from QRectF
    Rect2D(const QRectF& r) : x(r.x()), y(r.y()), width(r.width()), height(r.height()) {}  // NOLINT

    /// Implicit conversion to QRectF
    operator QRectF() const { return QRectF(x, y, width, height); }  // NOLINT
#endif
};

// =====================================================================
//  Vec3 — replaces QVector3D (for OBJ material colors)
// =====================================================================

/// 3-component float vector (used for material colors in OBJ I/O).
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

#if HOBBYCAD_HAS_QT
    Vec3(const QVector3D& v) : x(v.x()), y(v.y()), z(v.z()) {}  // NOLINT
    operator QVector3D() const { return QVector3D(x, y, z); }  // NOLINT
#endif
};

/// A workplane's frame: origin plus orthonormal in-plane axes and the normal.
/// A sketch point (u, v) maps to world as  origin + u*uAxis + v*vAxis;
/// projecting a world point P back onto the plane gives u = dot(P-origin,uAxis),
/// v = dot(P-origin,vAxis) (the normal component is dropped). This is the
/// "everything is relative to the plane's origin" frame the sketcher uses.
struct PlaneBasis {
    Vec3 origin;
    Vec3 uAxis{1.0f, 0.0f, 0.0f};
    Vec3 vAxis{0.0f, 1.0f, 0.0f};
    Vec3 normal{0.0f, 0.0f, 1.0f};
};

inline Point3 Point3::world(const PlaneBasis& b) const {
    // world = origin + u*uAxis + v*vAxis + w*normal   (u,v,w == x,y,z)
    return {
        double(b.origin.x) + x*double(b.uAxis.x) + y*double(b.vAxis.x) + z*double(b.normal.x),
        double(b.origin.y) + x*double(b.uAxis.y) + y*double(b.vAxis.y) + z*double(b.normal.y),
        double(b.origin.z) + x*double(b.uAxis.z) + y*double(b.vAxis.z) + z*double(b.normal.z),
    };
}
inline Point3 Point3::project(const PlaneBasis& b) const {
    const double dx = x - double(b.origin.x);
    const double dy = y - double(b.origin.y);
    const double dz = z - double(b.origin.z);
    return {
        dx*double(b.uAxis.x)  + dy*double(b.uAxis.y)  + dz*double(b.uAxis.z),   // u
        dx*double(b.vAxis.x)  + dy*double(b.vAxis.y)  + dz*double(b.vAxis.z),   // v
        dx*double(b.normal.x) + dy*double(b.normal.y) + dz*double(b.normal.z),  // w
    };
}

// =====================================================================
//  Container Helpers — replacements for Qt convenience methods
// =====================================================================

/// Check if a vector contains a value (replaces QVector::contains)
template<typename T>
bool contains(const std::vector<T>& vec, const T& value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}

/// Find the index of the first occurrence, or -1 (replaces QVector::indexOf)
template<typename T>
int indexOf(const std::vector<T>& vec, const T& value) {
    auto it = std::find(vec.begin(), vec.end(), value);
    return (it != vec.end()) ? static_cast<int>(std::distance(vec.begin(), it)) : -1;
}

/// Remove the first occurrence of a value; returns true if found (replaces QVector::removeOne)
template<typename T>
bool removeOne(std::vector<T>& vec, const T& value) {
    auto it = std::find(vec.begin(), vec.end(), value);
    if (it != vec.end()) {
        vec.erase(it);
        return true;
    }
    return false;
}

/// Remove all occurrences of a value; returns count removed (replaces QVector::removeAll)
template<typename T>
int removeAll(std::vector<T>& vec, const T& value) {
    auto it = std::remove(vec.begin(), vec.end(), value);
    int count = static_cast<int>(std::distance(it, vec.end()));
    vec.erase(it, vec.end());
    return count;
}

/// Remove element at index (replaces QVector::removeAt)
template<typename T>
void removeAt(std::vector<T>& vec, int index) {
    vec.erase(vec.begin() + index);
}

/// Safe element access with default value (replaces QVector::value(index, default))
template<typename T>
T valueAt(const std::vector<T>& vec, int index, const T& defaultValue = T{}) {
    if (index >= 0 && static_cast<size_t>(index) < vec.size())
        return vec[static_cast<size_t>(index)];
    return defaultValue;
}

// =====================================================================
//  Numeric Helpers
// =====================================================================

/// Fuzzy floating-point comparison (replaces qFuzzyCompare)
inline bool fuzzyCompare(double a, double b) {
    return std::abs(a - b) <= 1e-12 * std::max({1.0, std::abs(a), std::abs(b)});
}

/// Fuzzy zero check (replaces qFuzzyIsNull)
inline bool fuzzyIsNull(double a) {
    return std::abs(a) < 1e-12;
}


/// Arbitrary Axis Algorithm (AutoCAD/DXF): a right-handed plane frame from a
/// bare normal, for planes with NO construction history: 3-point, from-face,
/// or a DXF entity's OCS. If |Nx| < 1/64 and |Ny| < 1/64 the in-plane X is
/// seeded from world Y, else from world Z; then Y = N x X. This is the same
/// convention the DXF OCS uses, so import/export share one implementation.
inline PlaneBasis arbitraryAxisBasis(const Vec3& normal, const Vec3& origin = Vec3()) {
    float nx = normal.x, ny = normal.y, nz = normal.z;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12f) { nx = 0; ny = 0; nz = 1; } else { nx /= len; ny /= len; nz /= len; }
    float axx, axy, axz;
    if (std::fabs(nx) < 1.0f / 64.0f && std::fabs(ny) < 1.0f / 64.0f) {
        axx = nz;  axy = 0.0f; axz = -nx;              // Wy(0,1,0) x N
    } else {
        axx = -ny; axy = nx;   axz = 0.0f;             // Wz(0,0,1) x N
    }
    float al = std::sqrt(axx * axx + axy * axy + axz * axz); if (al < 1e-12f) al = 1.0f;
    axx /= al; axy /= al; axz /= al;
    const float ayx = ny * axz - nz * axy;             // Ay = N x Ax
    const float ayy = nz * axx - nx * axz;
    const float ayz = nx * axy - ny * axx;
    return { origin, Vec3(axx, axy, axz), Vec3(ayx, ayy, ayz), Vec3(nx, ny, nz) };
}

}  // namespace hobbycad

#endif  // HOBBYCAD_TYPES_H
