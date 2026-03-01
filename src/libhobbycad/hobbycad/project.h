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
#include "types.h"
#include "sketch/background.h"
#include "sketch/constraint.h"
#include "sketch/entity.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

#if HOBBYCAD_HAS_QT
#include <QJsonObject>
#else
#include <nlohmann/json.hpp>
#endif

namespace hobbycad {

// ---- Sketch types ----

// Use the canonical entity type from the sketch library
using SketchEntityType = sketch::EntityType;

// Use the canonical constraint type from the sketch library
using ConstraintType = sketch::ConstraintType;

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
    std::string name;                    ///< User-visible name

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
        return !hobbycad::fuzzyIsNull(originX) ||
               !hobbycad::fuzzyIsNull(originY) ||
               !hobbycad::fuzzyIsNull(originZ);
    }
};

/// A single entity in a sketch - alias for the canonical sketch::Entity type
using SketchEntityData = sketch::Entity;

/// A constraint relationship between sketch entities
struct ConstraintData {
    int id = 0;
    ConstraintType type = ConstraintType::Distance;
    std::vector<int> entityIds;        ///< IDs of entities involved in constraint
    std::vector<int> pointIndices;     ///< Point indices within entities (for multi-point entities)
    double value = 0.0;                ///< Constraint value (distance in mm, angle in degrees, etc.)
    bool isDriving = true;             ///< True = driving constraint, False = reference (display only)
    Point2D labelPosition;             ///< Where to display the dimension label in 2D sketch space
    bool labelVisible = true;          ///< Show/hide dimension text
    bool enabled = true;               ///< Whether constraint is active
};

/// A complete sketch
struct SketchData {
    std::string name;

    // Plane reference - either an origin plane or a construction plane
    SketchPlane plane = SketchPlane::XY;     ///< XY/XZ/YZ for origin, Custom for construction plane
    int constructionPlaneId = -1;             ///< ID of construction plane (-1 = use origin plane)

    // Inline plane parameters (used when constructionPlaneId == -1)
    double planeOffset = 0.0;                 ///< Offset from origin along plane normal
    PlaneRotationAxis rotationAxis = PlaneRotationAxis::X;  ///< Axis to rotate around
    double rotationAngle = 0.0;               ///< Rotation angle in degrees

    std::vector<SketchEntityData> entities;
    std::vector<ConstraintData> constraints;   ///< Parametric constraints (dimensions, geometric)
    double gridSpacing = 10.0;

    // Background image for tracing
    sketch::BackgroundImage backgroundImage;  ///< Optional background image for tracing
};

// ---- Parameter type ----

/// A single parameter
struct ParameterData {
    std::string name;
    std::string expression;
    double value = 0.0;
    std::string unit;
    std::string comment;
    bool isUserParam = true;
};

// ---- Foreign file tracking ----

/// A foreign file entry (non-CAD file tracked by the project)
struct ForeignFileData {
    std::string path;           ///< Relative path from project root
    std::string description;    ///< Optional description
    std::string category;       ///< Category (version_control, documentation, etc.)
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

/// Feature state for validation status
/// Used to indicate errors or warnings from constraint solving, BREP operations, etc.
enum class FeatureState {
    Normal,     ///< Feature is valid
    Warning,    ///< Feature has a warning (e.g., over-constrained sketch)
    Error       ///< Feature has an error (e.g., failed BREP operation, conflicting constraints)
};

/// A single feature in the history tree
struct FeatureData {
    int id = 0;
    FeatureType type = FeatureType::Origin;
    std::string name;
#if HOBBYCAD_HAS_QT
    QJsonObject properties;  ///< Feature-specific properties
#else
    nlohmann::json properties = nlohmann::json::object();  ///< Feature-specific properties
#endif
    std::vector<int> dependsOn;  ///< IDs of features this depends on (parents)
    bool suppressed = false; ///< True if feature is suppressed
    FeatureState state = FeatureState::Normal;  ///< Validation state
    std::string stateMessage;    ///< Human-readable error/warning message
};

// ---- Project class ----

class HOBBYCAD_EXPORT Project {
public:
    Project();
    ~Project();

    // ---- Project metadata ----

    std::string name() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    std::string author() const { return m_author; }
    void setAuthor(const std::string& author) { m_author = author; }

    std::string description() const { return m_description; }
    void setDescription(const std::string& desc) { m_description = desc; }

    std::string units() const { return m_units; }
    void setUnits(const std::string& units) { m_units = units; }

    /// Created timestamp as ISO 8601 string
    std::string created() const { return m_created; }

    /// Modified timestamp as ISO 8601 string
    std::string modified() const { return m_modified_time; }

    // ---- Project path ----

    /// Directory path of the project (empty if unsaved)
    std::string projectPath() const { return m_projectPath; }

    /// True if the project has never been saved
    bool isNew() const { return m_projectPath.empty(); }

    /// True if the project has unsaved changes
    bool isModified() const { return m_modified_flag; }
    void setModified(bool modified = true);

    // ---- Geometry ----

    const std::vector<TopoDS_Shape>& shapes() const { return m_shapes; }
    void addShape(const TopoDS_Shape& shape);
    void setShapes(const std::vector<TopoDS_Shape>& shapes);
    void clearShapes();

    // ---- Construction Planes ----

    const std::vector<ConstructionPlaneData>& constructionPlanes() const { return m_constructionPlanes; }
    void addConstructionPlane(const ConstructionPlaneData& plane);
    void setConstructionPlane(int index, const ConstructionPlaneData& plane);
    void removeConstructionPlane(int index);
    void clearConstructionPlanes();
    int nextConstructionPlaneId() const;
    const ConstructionPlaneData* constructionPlaneById(int id) const;

    // ---- Sketches ----

    const std::vector<SketchData>& sketches() const { return m_sketches; }
    void addSketch(const SketchData& sketch);
    void setSketch(int index, const SketchData& sketch);
    void removeSketch(int index);
    void clearSketches();

    // ---- Parameters ----

    const std::vector<ParameterData>& parameters() const { return m_parameters; }
    void setParameters(const std::vector<ParameterData>& params);
    void addParameter(const ParameterData& param);
    void clearParameters();

    // ---- Features ----

    const std::vector<FeatureData>& features() const { return m_features; }
    void addFeature(const FeatureData& feature);
    void setFeatures(const std::vector<FeatureData>& features);
    void clearFeatures();

    // ---- Foreign Files ----

    const std::vector<ForeignFileData>& foreignFiles() const { return m_foreignFiles; }
    void addForeignFile(const ForeignFileData& file);
    void addForeignFile(const std::string& path, const std::string& category = {},
                        const std::string& description = {});
    void removeForeignFile(const std::string& path);
    void setForeignFiles(const std::vector<ForeignFileData>& files);
    void clearForeignFiles();
    bool isForeignFile(const std::string& relativePath) const;
    const ForeignFileData* foreignFileByPath(const std::string& path) const;

    // ---- File I/O ----

    /// Load a project from a .hcad directory
    /// Returns true on success
    bool load(const std::string& path, std::string* errorMsg = nullptr);

    /// Save the project to a .hcad directory
    /// If path is empty, uses the current projectPath()
    /// Returns true on success
    bool save(const std::string& path = {}, std::string* errorMsg = nullptr);

    /// Create a new empty project
    void createNew(const std::string& name = {});

    /// Close the project and clear all data
    void close();

    // ---- Static constants ----

    static constexpr int FORMAT_VERSION = 1;
    static const char* HOBBYCAD_VERSION;

private:
#if HOBBYCAD_HAS_QT
    // JSON serialization helpers (Qt path — QJsonDocument)
    QJsonObject sketchToJson(const SketchData& sketch) const;
    SketchData sketchFromJson(const QJsonObject& json) const;

    QJsonObject parametersToJson() const;
    void parametersFromJson(const QJsonObject& json);

    QJsonObject featuresToJson() const;
    void featuresFromJson(const QJsonObject& json);

    QJsonObject constructionPlaneToJson(const ConstructionPlaneData& plane) const;
    ConstructionPlaneData constructionPlaneFromJson(const QJsonObject& json) const;

    QJsonObject manifestToJson() const;
    bool manifestFromJson(const QJsonObject& json, std::string* errorMsg);
#else
    // JSON serialization helpers (non-Qt path — nlohmann/json)
    nlohmann::json sketchToJson(const SketchData& sketch) const;
    SketchData sketchFromJson(const nlohmann::json& json) const;

    nlohmann::json parametersToJson() const;
    void parametersFromJson(const nlohmann::json& json);

    nlohmann::json featuresToJson() const;
    void featuresFromJson(const nlohmann::json& json);

    nlohmann::json constructionPlaneToJson(const ConstructionPlaneData& plane) const;
    ConstructionPlaneData constructionPlaneFromJson(const nlohmann::json& json) const;

    nlohmann::json manifestToJson() const;
    bool manifestFromJson(const nlohmann::json& json, std::string* errorMsg);
#endif

    // File I/O helpers
    bool saveManifest(const std::string& dir, std::string* errorMsg);
    bool saveGeometry(const std::string& dir, std::string* errorMsg);
    bool saveConstructionPlanes(const std::string& dir, std::string* errorMsg);
    bool saveSketches(const std::string& dir, std::string* errorMsg);
    bool saveParameters(const std::string& dir, std::string* errorMsg);
    bool saveFeatures(const std::string& dir, std::string* errorMsg);

    bool loadManifestFile(const std::string& manifestPath, std::string* errorMsg);
    bool loadGeometry(const std::string& dir, std::string* errorMsg);
    bool loadConstructionPlanes(const std::string& dir, std::string* errorMsg);
    bool loadSketches(const std::string& dir, std::string* errorMsg);
    bool loadParameters(const std::string& dir, std::string* errorMsg);
    bool loadFeatures(const std::string& dir, std::string* errorMsg);

    // Metadata
    std::string m_name;
    std::string m_author;
    std::string m_description;
    std::string m_units = "mm";
    std::string m_created;         ///< ISO 8601 timestamp string
    std::string m_modified_time;   ///< ISO 8601 timestamp string

    // Project state
    std::string m_projectPath;
    bool m_modified_flag = false;

    // Content
    std::vector<TopoDS_Shape> m_shapes;
    std::vector<ConstructionPlaneData> m_constructionPlanes;
    std::vector<SketchData> m_sketches;
    std::vector<ParameterData> m_parameters;
    std::vector<FeatureData> m_features;
    std::vector<ForeignFileData> m_foreignFiles;

    // File references (relative paths within project)
    std::vector<std::string> m_geometryFiles;
    std::vector<std::string> m_constructionPlaneFiles;
    std::vector<std::string> m_sketchFiles;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_H
