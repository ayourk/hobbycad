// =====================================================================
//  src/libhobbycad/hobbycad/project.h — HobbyCAD project container
// =====================================================================
//
//  A Project represents a .hcad directory structure containing:
//    - Project manifest (<dirname>.hcad, e.g., my_widget/my_widget.hcad)
//    - Geometry bodies (.brep files)
//    - Sketches (JSON)
//    - Parameters (JSON)
//    - Feature tree (JSON)
//    - Metadata (thumbnails, etc.)
//
//  The .hcad format uses a directory structure for git-friendliness
//  and human-readability.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_PROJECT_H
#define HOBBYCAD_PROJECT_H

#include "core.h"
#include "sketch/background.h"

#include <TopoDS_Shape.hxx>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVector>

namespace hobbycad {

// ---- Sketch types (mirrored from GUI for serialization) ----

/// Sketch entity type
enum class SketchEntityType {
    Point,
    Line,
    Rectangle,
    Circle,
    Arc,
    Spline,
    Polygon,
    Slot,
    Ellipse,
    Text,
    Dimension
};

/// Constraint type for parametric sketching
enum class ConstraintType {
    // Dimensional constraints
    Distance,      ///< Linear distance between two points or point-to-line
    Radius,        ///< Circle/arc radius
    Diameter,      ///< Circle/arc diameter
    Angle,         ///< Angle between two lines

    // Geometric constraints
    Horizontal,    ///< Line is horizontal
    Vertical,      ///< Line is vertical
    Parallel,      ///< Two lines are parallel
    Perpendicular, ///< Two lines are perpendicular
    Coincident,    ///< Two points share same position
    Tangent,       ///< Arc/circle tangent to line or arc/circle
    Equal,         ///< Two entities have equal length/radius
    Midpoint,      ///< Point at midpoint of line
    Symmetric      ///< Two points symmetric about a line
};

/// Sketch plane orientation (for sketches referencing origin planes)
enum class SketchPlane {
    XY,      ///< XY plane (normal along Z)
    XZ,      ///< XZ plane (normal along Y)
    YZ,      ///< YZ plane (normal along X)
    Custom   ///< Custom angled plane or references a ConstructionPlane
};

/// Axis for plane rotation
enum class PlaneRotationAxis {
    X,
    Y,
    Z
};

/// How a construction plane is defined
enum class ConstructionPlaneType {
    OffsetFromOrigin,    ///< Offset from XY, XZ, or YZ origin plane
    OffsetFromPlane,     ///< Offset from another construction plane
    Angled               ///< Rotated around one or two axes
};

/// A construction plane - a first-class object in the project
struct ConstructionPlaneData {
    int id = 0;                          ///< Unique ID within the project
    QString name;                        ///< User-visible name

    ConstructionPlaneType type = ConstructionPlaneType::OffsetFromOrigin;

    // Base plane reference
    SketchPlane basePlane = SketchPlane::XY;  ///< For OffsetFromOrigin
    int basePlaneId = -1;                      ///< For OffsetFromPlane (-1 = origin plane)

    // Plane origin/center in absolute (global) coordinates
    // This is where the plane's local (0,0) point is located in 3D space
    // For OffsetFromOrigin planes, this is typically on the offset axis
    // For arbitrary planes, this can be anywhere
    double originX = 0.0;
    double originY = 0.0;
    double originZ = 0.0;

    // Offset along the normal (relative to origin point)
    double offset = 0.0;

    // Rotation (two axes for full 3D orientation)
    // First rotation is around the specified axis, second is around the resulting perpendicular
    PlaneRotationAxis primaryAxis = PlaneRotationAxis::X;
    double primaryAngle = 0.0;           ///< Rotation in degrees around primary axis
    PlaneRotationAxis secondaryAxis = PlaneRotationAxis::Y;
    double secondaryAngle = 0.0;         ///< Rotation in degrees around secondary axis

    // Roll angle - rotation around the plane's normal (affects sketch orientation)
    // This is the "least impact" rotation that spins the plane's X/Y axes in place
    // Useful for loft twist, pattern alignment, etc.
    double rollAngle = 0.0;              ///< Rotation in degrees around plane normal

    // Visibility
    bool visible = true;

    /// Check if the plane has a non-zero origin (not at global 0,0,0)
    bool hasCustomOrigin() const {
        return !qFuzzyIsNull(originX) || !qFuzzyIsNull(originY) || !qFuzzyIsNull(originZ);
    }
};

/// A single entity in a sketch
struct SketchEntityData {
    int id = 0;
    SketchEntityType type = SketchEntityType::Point;
    QVector<QPointF> points;
    double radius = 0.0;
    double startAngle = 0.0;
    double sweepAngle = 0.0;
    int sides = 6;                ///< For polygons (number of sides)
    double majorRadius = 0.0;     ///< For ellipses (semi-major axis)
    double minorRadius = 0.0;     ///< For ellipses (semi-minor axis)
    QString text;
    bool constrained = false;
    bool isConstruction = false;  ///< Construction geometry (excluded from profiles)
};

/// A constraint relationship between sketch entities
struct ConstraintData {
    int id = 0;
    ConstraintType type = ConstraintType::Distance;
    QVector<int> entityIds;        ///< IDs of entities involved in constraint
    QVector<int> pointIndices;     ///< Point indices within entities (for multi-point entities)
    double value = 0.0;            ///< Constraint value (distance in mm, angle in degrees, etc.)
    bool isDriving = true;         ///< True = driving constraint, False = reference (display only)
    QPointF labelPosition;         ///< Where to display the dimension label in 2D sketch space
    bool labelVisible = true;      ///< Show/hide dimension text
    bool enabled = true;           ///< Whether constraint is active
};

/// A complete sketch
struct SketchData {
    QString name;

    // Plane reference - either an origin plane or a construction plane
    SketchPlane plane = SketchPlane::XY;     ///< XY/XZ/YZ for origin, Custom for construction plane
    int constructionPlaneId = -1;             ///< ID of construction plane (-1 = use origin plane)

    // Inline plane parameters (used when constructionPlaneId == -1)
    double planeOffset = 0.0;                 ///< Offset from origin along plane normal
    PlaneRotationAxis rotationAxis = PlaneRotationAxis::X;  ///< Axis to rotate around
    double rotationAngle = 0.0;               ///< Rotation angle in degrees

    QVector<SketchEntityData> entities;
    QVector<ConstraintData> constraints;   ///< Parametric constraints (dimensions, geometric)
    double gridSpacing = 10.0;

    // Background image for tracing
    sketch::BackgroundImage backgroundImage;  ///< Optional background image for tracing
};

// ---- Parameter type ----

/// A single parameter
struct ParameterData {
    QString name;
    QString expression;
    double value = 0.0;
    QString unit;
    QString comment;
    bool isUserParam = true;
};

// ---- Feature types ----

/// Feature type in the modeling history
enum class FeatureType {
    Origin,
    Sketch,
    Extrude,
    Revolve,
    Fillet,
    Chamfer,
    Hole,
    Mirror,
    Pattern,
    Box,
    Cylinder,
    Sphere,
    Move,
    Join,
    Cut,
    Intersect
};

/// A single feature in the history tree
struct FeatureData {
    int id = 0;
    FeatureType type = FeatureType::Origin;
    QString name;
    QJsonObject properties;  ///< Feature-specific properties
};

// ---- Project class ----

class HOBBYCAD_EXPORT Project {
public:
    Project();
    ~Project();

    // ---- Project metadata ----

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QString author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }

    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }

    QString units() const { return m_units; }
    void setUnits(const QString& units) { m_units = units; }

    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified_time; }

    // ---- Project path ----

    /// Directory path of the project (empty if unsaved)
    QString projectPath() const { return m_projectPath; }

    /// True if the project has never been saved
    bool isNew() const { return m_projectPath.isEmpty(); }

    /// True if the project has unsaved changes
    bool isModified() const { return m_modified_flag; }
    void setModified(bool modified = true);

    // ---- Geometry ----

    const QList<TopoDS_Shape>& shapes() const { return m_shapes; }
    void addShape(const TopoDS_Shape& shape);
    void setShapes(const QList<TopoDS_Shape>& shapes);
    void clearShapes();

    // ---- Construction Planes ----

    const QVector<ConstructionPlaneData>& constructionPlanes() const { return m_constructionPlanes; }
    void addConstructionPlane(const ConstructionPlaneData& plane);
    void setConstructionPlane(int index, const ConstructionPlaneData& plane);
    void removeConstructionPlane(int index);
    void clearConstructionPlanes();
    int nextConstructionPlaneId() const;
    const ConstructionPlaneData* constructionPlaneById(int id) const;

    // ---- Sketches ----

    const QVector<SketchData>& sketches() const { return m_sketches; }
    void addSketch(const SketchData& sketch);
    void setSketch(int index, const SketchData& sketch);
    void removeSketch(int index);
    void clearSketches();

    // ---- Parameters ----

    const QList<ParameterData>& parameters() const { return m_parameters; }
    void setParameters(const QList<ParameterData>& params);
    void addParameter(const ParameterData& param);
    void clearParameters();

    // ---- Features ----

    const QVector<FeatureData>& features() const { return m_features; }
    void addFeature(const FeatureData& feature);
    void setFeatures(const QVector<FeatureData>& features);
    void clearFeatures();

    // ---- File I/O ----

    /// Load a project from a .hcad directory
    /// Returns true on success
    bool load(const QString& path, QString* errorMsg = nullptr);

    /// Save the project to a .hcad directory
    /// If path is empty, uses the current projectPath()
    /// Returns true on success
    bool save(const QString& path = QString(), QString* errorMsg = nullptr);

    /// Create a new empty project
    void createNew(const QString& name = QString());

    /// Close the project and clear all data
    void close();

    // ---- Static constants ----

    static constexpr int FORMAT_VERSION = 1;
    static const char* HOBBYCAD_VERSION;

private:
    // JSON serialization helpers
    QJsonObject sketchToJson(const SketchData& sketch) const;
    SketchData sketchFromJson(const QJsonObject& json) const;

    QJsonObject parametersToJson() const;
    void parametersFromJson(const QJsonObject& json);

    QJsonObject featuresToJson() const;
    void featuresFromJson(const QJsonObject& json);

    QJsonObject constructionPlaneToJson(const ConstructionPlaneData& plane) const;
    ConstructionPlaneData constructionPlaneFromJson(const QJsonObject& json) const;

    QJsonObject manifestToJson() const;
    bool manifestFromJson(const QJsonObject& json, QString* errorMsg);

    // File I/O helpers
    bool saveManifest(const QString& dir, QString* errorMsg);
    bool saveGeometry(const QString& dir, QString* errorMsg);
    bool saveConstructionPlanes(const QString& dir, QString* errorMsg);
    bool saveSketches(const QString& dir, QString* errorMsg);
    bool saveParameters(const QString& dir, QString* errorMsg);
    bool saveFeatures(const QString& dir, QString* errorMsg);

    bool loadManifestFile(const QString& manifestPath, QString* errorMsg);
    bool loadGeometry(const QString& dir, QString* errorMsg);
    bool loadConstructionPlanes(const QString& dir, QString* errorMsg);
    bool loadSketches(const QString& dir, QString* errorMsg);
    bool loadParameters(const QString& dir, QString* errorMsg);
    bool loadFeatures(const QString& dir, QString* errorMsg);

    // Metadata
    QString m_name;
    QString m_author;
    QString m_description;
    QString m_units = "mm";
    QDateTime m_created;
    QDateTime m_modified_time;

    // Project state
    QString m_projectPath;
    bool m_modified_flag = false;

    // Content
    QList<TopoDS_Shape> m_shapes;
    QVector<ConstructionPlaneData> m_constructionPlanes;
    QVector<SketchData> m_sketches;
    QList<ParameterData> m_parameters;
    QVector<FeatureData> m_features;

    // File references (relative paths within project)
    QStringList m_geometryFiles;
    QStringList m_constructionPlaneFiles;
    QStringList m_sketchFiles;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_H
