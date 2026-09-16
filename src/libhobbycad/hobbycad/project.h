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
#include "body.h"
#include "types.h"
#include "feature.h"
#include "sketch/background.h"
#include "sketch/constraint.h"
#include "sketch/entity.h"
#include "sketch/group.h"
#include "parameters.h"

#include <TopoDS_Shape.hxx>

#include <array>
#include <limits>
#include <map>
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
    /// Design this plane belongs to.
    ///
    /// 0 means "not yet assigned"; addConstructionPlane() fills it in from
    /// the active design. See SketchData::designId.
    int designId = 0;

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

    // Where the center is measured from. Absolute (global axes) by default.
    // When centerRelative is set, originX/Y/Z are offsets along the X, Y and
    // normal directions of a reference frame: construction plane
    // centerRefPlaneId, or (-1) this plane's own base or reference plane.
    // The plane then follows its reference when the reference moves.
    bool centerRelative = false;
    int centerRefPlaneId = -1;

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

/// A constraint relationship between sketch entities - alias for the canonical
/// sketch::Constraint type.
///
/// Like SketchEntityData/sketch::Entity, this is one type, not two: the solver
/// simply ignores the display fields (labelPosition, labelAngle, anchorPoint,
/// supplementary) that it has no use for. Storing and solving therefore share a
/// single struct, so a new field is added in one place instead of being threaded
/// through a struct, two hand-written converters, and two serializers, which is
/// how fields used to get silently dropped.
using ConstraintData = sketch::Constraint;

/// The world frame of a standard origin plane (XY/XZ/YZ) at the given offset
/// along its normal. Convention (matches the 3D viewport / OCCT planes):
///   XY: uAxis=+X vAxis=+Y normal=+Z   (offset along Z)
///   XZ: uAxis=+X vAxis=+Z normal=-Y   (offset along -Y; right-handed n = u x v)
///   YZ: uAxis=+Y vAxis=+Z normal=+X   (offset along X)
/// For a custom/rotated/construction-plane sketch use the SketchData overload
/// below, which resolves the full frame via sketchFrame (see plane_frame.h).
HOBBYCAD_EXPORT PlaneBasis planeBasisFor(SketchPlane plane, double offset = 0.0);

/// Labels for a plane's axes: the two in-plane axes (u, v) and the normal. For
/// the origin planes these are world-axis letters (XY -> X,Y,Z; XZ -> X,Z,Y;
/// YZ -> Y,Z,X); a custom plane has no world-axis alignment, so its axes are
/// the generic U, V, N. The properties panel shows the u/v fields in 2D mode
/// and adds the normal field in 3D; the viewport colors the axes to match
/// (X red, Y green, Z blue). Matches planeBasisFor's uAxis/vAxis/normal.
struct PlaneAxisLabels {
    const char* u;       ///< first in-plane axis  (stored in points .x)
    const char* v;       ///< second in-plane axis (stored in points .y)
    const char* normal;  ///< plane normal (points .z, off-plane; hidden in 2D)
};
HOBBYCAD_EXPORT PlaneAxisLabels planeAxisLabels(SketchPlane plane);

/// A complete sketch
/// One model inside a project: its own sketches, bodies and construction
/// geometry. What Fusion calls a design.
///
/// Aaron, 2026-08-27: *"I define a design as not quite a project, but
/// almost."* That is the right distinction. A project is a directory and a
/// version-control boundary; a design is a self-contained model living
/// inside one. A project always contains at least one design, and today it
/// contains exactly one.
///
/// The point of declaring this NOW, while only one design is possible, is
/// that a single-design project writes its files exactly where it always
/// has (pathPrefix is empty) and records in the manifest that those
/// files belong to design 1. A later version that supports several designs
/// writes the second one with a prefix and leaves the first alone. Nothing
/// ever has to be converted.
struct DesignData {
    int id = 1;
    std::string name;

    /// Directory the design's files live under, relative to the project.
    ///
    /// Empty for the first design, so a single-design project keeps
    /// sketches/sketch_7.json rather than designs/1/sketches/sketch_7.json.
    /// A second design would use "designs/2/". Emptiness is what makes
    /// today's layout a valid special case rather than an old format.
    std::string pathPrefix;
};

struct SketchData {
    std::string name;

    /// Design this sketch belongs to.
    ///
    /// 0 means "not yet assigned"; addSketch() fills it in from the active
    /// design. A missing value in a file reads as design 1, which is what a
    /// hand-edited or truncated manifest should degrade to rather than
    /// leaving the sketch orphaned.
    int designId = 0;

    /// Feature id, matching FeatureData::id in the feature tree.
    ///
    /// This is the sketch's stable identity: it names the file the sketch is
    /// saved to (sketches/sketch_<id>.json) and is how the feature tree, the
    /// timeline and the undo history refer to it. addSketch() assigns one when
    /// the caller leaves it at -1, so a saved sketch always has a real id.
    int id = -1;

    // Plane reference - either an origin plane or a construction plane
    SketchPlane plane = SketchPlane::XY;     ///< XY/XZ/YZ for origin, Custom for construction plane
    int constructionPlaneId = -1;             ///< ID of construction plane (-1 = use origin plane)

    // Inline plane parameters (used when constructionPlaneId == -1)
    double planeOffset = 0.0;                 ///< Offset from origin along plane normal
    PlaneRotationAxis rotationAxis = PlaneRotationAxis::X;  ///< Axis to rotate around
    double rotationAngle = 0.0;               ///< Rotation angle in degrees

    std::vector<SketchEntityData> entities;
    std::vector<ConstraintData> constraints;   ///< Parametric constraints (dimensions, geometric)
    std::vector<sketch::Group> groups;         ///< Decomposition and user organization groups
    double gridSpacing = 10.0;
    bool flipView = false;                    ///< Heads/tails: draw from the far side (view flip; u mirrored)

    // Background image for tracing
    sketch::BackgroundImage backgroundImage;  ///< Optional background image for tracing
};

// ---- Parameter type ----

/// A single parameter.
///
/// Unlike sketches, bodies and construction planes, a parameter carries NO
/// surrogate id and is not saved to a file of its own, deliberately.
/// Expressions reference a parameter by NAME (`width * 2`), so the name is
/// the identity, it is user-visible, and an id alongside it would be a
/// second identity that nothing resolves against.
///
/// What a parameter does need is the other half of what the id bought
/// elsewhere: a design it belongs to, a guarantee that its name is unique
/// within that design, and a rename that carries references with it.
struct ParameterData {
    /// Design this parameter belongs to.
    ///
    /// 0 means "not yet assigned"; addParameter() fills it in from the
    /// active design. Parameters are per-design, as they are in Fusion:
    /// two designs may each have a "width" without colliding.
    int designId = 0;

    std::string name;
    std::string expression;
    double value = 0.0;
    std::string unit;
    std::string comment;
    bool isUserParam = true;
    bool isReference = false;         ///< Value measured from the solved model
                                      ///< (not authored); see ParameterEngine.
    std::string referenceSource;      ///< What geometry it measures, e.g.
                                      ///< "distance <ptA> <ptB>".
};

/// A named 3D coordinate the user can reference wherever a coordinate is
/// expected ("create plane at origin", "point at corner"). Each component is a
/// number, formula or parameter, evaluated with the project parameters at use
/// time. Named points are per-design, like parameters. "origin" is a built-in
/// (0,0,0) even without an explicit entry.
struct NamedPointData {
    int designId = 0;
    std::string name;
    std::string xExpr;   ///< expression for X (number / formula / parameter)
    std::string yExpr;   ///< expression for Y
    std::string zExpr;   ///< expression for Z ("" -> 0, so 2D points are fine)
};

// ---- Foreign file tracking ----

/// A foreign file entry (non-CAD file tracked by the project)
struct ForeignFileData {
    std::string path;           ///< Relative path from project root
    std::string description;    ///< Optional description
    std::string category;       ///< Category (version_control, documentation, etc.)
};

// ---- Feature types ----


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

    /// The bodies in this project.
    const std::vector<BodyData>& bodies() const { return m_bodies; }

    /// Lowest unused body id.
    int nextBodyId() const;

    /// Add a body, assigning an id, a name and the active design.
    int addBody(const TopoDS_Shape& shape, const std::string& name = {});
    void addShape(const TopoDS_Shape& shape);
    /// Replace every body, identity included.
    ///
    /// The Document now carries BodyData too, so a sync back into the
    /// project moves ids, names and design ids across intact. It used to
    /// take a bare shape list and match by POSITION, which silently
    /// reassigned every body's identity as soon as one was deleted.
    void setBodies(const std::vector<BodyData>& bodies);
    void clearShapes();

    // ---- Construction Planes ----

    const std::vector<ConstructionPlaneData>& constructionPlanes() const { return m_constructionPlanes; }
    /// Add a construction plane, assigning an id and the active design if
    /// the caller has not. @return the plane's id.
    int addConstructionPlane(const ConstructionPlaneData& plane);
    void setConstructionPlane(int index, const ConstructionPlaneData& plane);
    /// Would making `candidateRefId` a reference of plane `planeId` close a
    /// loop? True when the candidate IS the plane, or when following the
    /// candidate's own references (offset-from-plane base, relative center)
    /// reaches the plane. Every entry point that sets a reference asks this
    /// first, so a plane can never be defined in terms of itself.
    static bool planeDependsOn(const std::vector<ConstructionPlaneData>& planes,
                               int planeId, int candidateRefId);
    bool constructionPlaneDependsOn(int planeId, int candidateRefId) const {
        return planeDependsOn(m_constructionPlanes, planeId, candidateRefId);
    }
    void removeConstructionPlane(int index);
    /// Replace every construction plane, identity included. See setSketches().
    void setConstructionPlanes(const std::vector<ConstructionPlaneData>& planes);
    void clearConstructionPlanes();
    int nextConstructionPlaneId() const;
    const ConstructionPlaneData* constructionPlaneById(int id) const;

    // ---- Sketches ----

    const std::vector<SketchData>& sketches() const { return m_sketches; }
    /// Add a sketch, assigning a feature id if it does not have one.
    void addSketch(const SketchData& sketch);

    /// Lowest unused feature id across the sketches held by this project.
    int nextSketchFeatureId() const;

    /// Designs in this project. Never empty: a project always has at least
    /// one, synthesized on load when the manifest does not list any.
    const std::vector<DesignData>& designs() const { return m_designs; }

    /// Add a design and return its id.
    ///
    /// The first design keeps an empty path prefix; every later one gets
    /// "designs/<id>/", which is what keeps a single-design project's files
    /// where they are.
    int addDesign(const std::string& name);

    /// The design a new object is added to. The first design unless set.
    int activeDesignId() const { return m_activeDesignId; }
    void setActiveDesignId(int id) { m_activeDesignId = id; }

    /// Path prefix for a design id, or "" if unknown.
    std::string designPathPrefix(int designId) const;
    void setSketch(int index, const SketchData& sketch);
    void removeSketch(int index);

    /// Replace every sketch, identity included.
    ///
    /// The bulk counterpart of setBodies(), and it exists for the same
    /// reason: restoring an undo snapshot has to put back ids and names,
    /// not just shapes. Rebuilding the list one addSketch() at a time
    /// would reassign ids.
    void setSketches(const std::vector<SketchData>& sketches);
    void clearSketches();

    // ---- Parameters ----

    const std::vector<ParameterData>& parameters() const { return m_parameters; }
    const std::vector<NamedPointData>& namedPoints() const { return m_namedPoints; }
    void setParameters(const std::vector<ParameterData>& params);
    /// Add a parameter, assigning the active design if the caller has not.
    ///
    /// @return false if a parameter of that name already exists in the same
    /// design. Names are how expressions resolve, so a duplicate is the
    /// parameter equivalent of two files claiming the same path: one of
    /// them silently wins.
    bool addParameter(const ParameterData& param);
    /// Add or update a named point by name (per design). Returns true.
    bool addNamedPoint(const NamedPointData& np);
    const NamedPointData* namedPointByName(const std::string& name, int design = 0) const;

    /// Find a parameter by name within a design. Null if there is none.
    const ParameterData* parameterByName(const std::string& name,
                                         int designId) const;

    /// Rename a parameter and rewrite every expression that referenced it.
    ///
    /// Renaming without this silently breaks each expression using the old
    /// name or, worse, repoints it at a different parameter that happens
    /// to have taken the name. Both are the same class of defect as a
    /// deleted construction plane renumbering the file a sketch resolves
    /// through.
    ///
    /// @return false if @p oldName does not exist, or @p newName is already
    /// taken in that design.
    bool renameParameter(const std::string& oldName,
                         const std::string& newName,
                         int designId);
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

    /// Save-file format version.
    ///
    /// HobbyCAD is unreleased, so this stays at 1: there are no files in the
    /// wild to stay compatible with, and inventing a version history for a
    /// format nobody has shipped only adds compatibility code that can never
    /// be exercised. Bump it on the first release that changes the format
    /// after users exist.
    static constexpr int FORMAT_VERSION = 1;
    static const char* HOBBYCAD_VERSION;

private:
#if HOBBYCAD_HAS_QT
    // JSON serialization helpers (Qt path: QJsonDocument)
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
    // JSON serialization helpers (non-Qt path: nlohmann/json)
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
    std::vector<BodyData> m_bodies;
    std::vector<ConstructionPlaneData> m_constructionPlanes;
    std::vector<DesignData> m_designs;
    int m_activeDesignId = 1;
    std::vector<SketchData> m_sketches;
    std::vector<ParameterData> m_parameters;
    std::vector<NamedPointData> m_namedPoints;
    std::vector<FeatureData> m_features;
    std::vector<ForeignFileData> m_foreignFiles;

    // File references (relative paths within project)
    std::vector<std::string> m_geometryFiles;
    std::vector<std::string> m_constructionPlaneFiles;
    std::vector<std::string> m_sketchFiles;
};

/// The project directory a save to `path` writes. A directory is used as
/// given. An existing manifest saves into its own directory, so re-saving a
/// project never nests it inside itself. A new "name.hcad" becomes the
/// directory "name" beside it, unless it already sits in a directory of that
/// name. Project::save and every front end's Save As go through this rule.
HOBBYCAD_EXPORT std::string projectDirForSavePath(const std::string& path);

/// World frame a sketch actually sits on: resolves its construction plane
/// (with the full rotation/roll/offset chain) or its inline base plane +
/// rotation + offset. The canonical, un-rotated case delegates to the
/// closed-form planeBasisFor(SketchPlane, offset) above. Defined in
/// plane_frame.cpp (pulls in OCCT there, not into this header).
HOBBYCAD_EXPORT PlaneBasis planeBasisFor(const SketchData& sketch, const Project& project);

/// Find a construction plane by name, ignoring case. Null when there is none.
HOBBYCAD_EXPORT const ConstructionPlaneData* findConstructionPlaneByName(
    const std::vector<ConstructionPlaneData>& planes, const std::string& name);

/// Resolve a plane reference as typed ("XY", "xz", "YZ", or a construction
/// plane's name) onto a sketch: `plane` and `constructionPlaneId` are set
/// together, so a named plane can never leave the sketch on the XY basis by
/// mistake. `displayName`, when given, receives the canonical spelling ("XY"
/// or the plane's stored name).
/// @return false when the text is neither an origin plane nor a known
///         construction plane; the outputs are left untouched.
HOBBYCAD_EXPORT bool resolveSketchPlaneRef(const std::string& ref,
                                           const std::vector<ConstructionPlaneData>& planes,
                                           SketchPlane& plane, int& constructionPlaneId,
                                           std::string* displayName = nullptr);

// ---- Named document objects -------------------------------------------

/// The document objects a person names at a prompt or in a browser.
enum class ObjectKind { Sketch, Body, Plane };
HOBBYCAD_EXPORT bool parseObjectKind(const std::string& word, ObjectKind& out);
HOBBYCAD_EXPORT const char* objectKindName(ObjectKind kind);

/// The project's list of a kind, by position (its mutation API is
/// index-based: setSketch(index, ...), setConstructionPlane(index, ...)).
HOBBYCAD_EXPORT int objectCount(const Project& project, ObjectKind kind);
HOBBYCAD_EXPORT std::string objectNameAt(const Project& project, ObjectKind kind, int index);
HOBBYCAD_EXPORT int objectIdAt(const Project& project, ObjectKind kind, int index);
/// True when another object of the kind (not the one at `skipIndex`)
/// already has `name`.
HOBBYCAD_EXPORT bool objectNameTaken(const Project& project, ObjectKind kind,
                                     const std::string& name, int skipIndex = -1);

enum class ObjectRefProblem {
    None,
    NoneOfKind,    ///< the document has no objects of that kind
    BadIdNumber,   ///< "id=" followed by something that is not a number
    NoSuchId,      ///< "id=<n>" names no object (id in ObjectRef::id)
    Ambiguous,     ///< a bare token is one object's name and another's id (that id in ObjectRef::id)
    NoSuchName     ///< nothing of the kind has that name or id
};
struct ObjectRef {
    int index = -1;                                    ///< position in the kind's list
    ObjectRefProblem problem = ObjectRefProblem::None;
    int id = -1;                                       ///< the id involved, see the problems
};

/// Resolve a token as typed ("Sketch1", "12", "id=12") to a POSITION in
/// the kind's list. A bare token is matched by NAME first, then by id,
/// because a sketch may legitimately be named "12"; when a name match and a
/// different object's id both fit the reference is Ambiguous rather than a
/// guess, and "id=<n>" says which was meant. Resolving at the moment of use
/// keeps positions from being held across an edit.
HOBBYCAD_EXPORT ObjectRef resolveObjectRef(const Project& project, ObjectKind kind,
                                           const std::string& token);

// ---- Parameter bridge ---------------------------------------------------

/// The parameter engine loaded from a project's parameter records.
HOBBYCAD_EXPORT ParameterEngine parameterEngineFrom(const std::vector<ParameterData>& params);

/// The engine's evaluated parameters written back as project records: the
/// records in `before` keep their order and design, updated from the engine
/// (a parameter the engine no longer has is dropped); parameters the engine
/// added come after, in the design `designIdForNew`.
HOBBYCAD_EXPORT std::vector<ParameterData> parametersFromEngine(const ParameterEngine& engine,
                                                                const std::vector<ParameterData>& before,
                                                                int designIdForNew);

/// What "create plane" collects; makeConstructionPlane() maps it onto the
/// stored model, so the mapping (default name, relative vs angled vs offset,
/// X/Y/Z angles -> primary/secondary/roll) is written once.
struct ConstructionPlaneSpec {
    std::string name;                        ///< empty: "Plane <n>", n = existing count + 1
    double originX = 0.0, originY = 0.0, originZ = 0.0;  ///< absolute origin (unused when refPlaneId >= 0)
    double offset = 0.0;                     ///< along the plane normal
    double rotX = 0.0, rotY = 0.0, rotZ = 0.0;   ///< global-axis angles in degrees
    int refPlaneId = -1;                     ///< >= 0: offset from, and centered on, that plane
};
HOBBYCAD_EXPORT ConstructionPlaneData makeConstructionPlane(const ConstructionPlaneSpec& spec,
                                                            size_t existingCount);

/// Every named point evaluated against `params`, plus the built-in "origin"
/// at (0,0,0). A component that does not evaluate reads as 0.
HOBBYCAD_EXPORT std::map<std::string, std::array<double, 3>> evaluateNamedPoints(
    const std::vector<NamedPointData>& points,
    const std::map<std::string, double>& params);

/// Why defineNamedPoint() refused.
enum class NamedPointProblem { None, ReservedName, BadComponentCount, DoesNotEvaluate };

/// Build a named point from its name and "x,y[,z]" text: the built-in
/// "origin" cannot be redefined, two or three components are required, and
/// each must evaluate against `params` (a number, formula or parameter).
/// On success `out` is filled (zExpr "0" when omitted) and `value`, when
/// given, holds the evaluated coordinates.
HOBBYCAD_EXPORT NamedPointProblem defineNamedPoint(const std::string& name,
                                                   const std::string& coordText,
                                                   const std::map<std::string, double>& params,
                                                   NamedPointData& out,
                                                   std::array<double, 3>* value = nullptr);

}  // namespace hobbycad

#endif  // HOBBYCAD_PROJECT_H
