// =====================================================================
//  src/libhobbycad/project.cpp — HobbyCAD project container
// =====================================================================

#include "hobbycad/project.h"
#include "hobbycad/brep_io.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hobbycad {

const char* Project::HOBBYCAD_VERSION = "0.1.0";

Project::Project()
{
    m_created = QDateTime::currentDateTimeUtc();
    m_modified_time = m_created;
}

Project::~Project() = default;

// ---- Modification tracking ----

void Project::setModified(bool modified)
{
    m_modified_flag = modified;
    if (modified) {
        m_modified_time = QDateTime::currentDateTimeUtc();
    }
}

// ---- Geometry ----

void Project::addShape(const TopoDS_Shape& shape)
{
    m_shapes.append(shape);
    setModified(true);
}

void Project::setShapes(const QList<TopoDS_Shape>& shapes)
{
    m_shapes = shapes;
    setModified(true);
}

void Project::clearShapes()
{
    m_shapes.clear();
    setModified(true);
}

// ---- Construction Planes ----

void Project::addConstructionPlane(const ConstructionPlaneData& plane)
{
    m_constructionPlanes.append(plane);
    setModified(true);
}

void Project::setConstructionPlane(int index, const ConstructionPlaneData& plane)
{
    if (index >= 0 && index < m_constructionPlanes.size()) {
        m_constructionPlanes[index] = plane;
        setModified(true);
    }
}

void Project::removeConstructionPlane(int index)
{
    if (index >= 0 && index < m_constructionPlanes.size()) {
        m_constructionPlanes.remove(index);
        setModified(true);
    }
}

void Project::clearConstructionPlanes()
{
    m_constructionPlanes.clear();
    setModified(true);
}

int Project::nextConstructionPlaneId() const
{
    int maxId = 0;
    for (const auto& plane : m_constructionPlanes) {
        if (plane.id > maxId) {
            maxId = plane.id;
        }
    }
    return maxId + 1;
}

const ConstructionPlaneData* Project::constructionPlaneById(int id) const
{
    for (const auto& plane : m_constructionPlanes) {
        if (plane.id == id) {
            return &plane;
        }
    }
    return nullptr;
}

// ---- Sketches ----

void Project::addSketch(const SketchData& sketch)
{
    m_sketches.append(sketch);
    setModified(true);
}

void Project::setSketch(int index, const SketchData& sketch)
{
    if (index >= 0 && index < m_sketches.size()) {
        m_sketches[index] = sketch;
        setModified(true);
    }
}

void Project::removeSketch(int index)
{
    if (index >= 0 && index < m_sketches.size()) {
        m_sketches.remove(index);
        setModified(true);
    }
}

void Project::clearSketches()
{
    m_sketches.clear();
    setModified(true);
}

// ---- Parameters ----

void Project::setParameters(const QList<ParameterData>& params)
{
    m_parameters = params;
    setModified(true);
}

void Project::addParameter(const ParameterData& param)
{
    m_parameters.append(param);
    setModified(true);
}

void Project::clearParameters()
{
    m_parameters.clear();
    setModified(true);
}

// ---- Features ----

void Project::addFeature(const FeatureData& feature)
{
    m_features.append(feature);
    setModified(true);
}

void Project::setFeatures(const QVector<FeatureData>& features)
{
    m_features = features;
    setModified(true);
}

void Project::clearFeatures()
{
    m_features.clear();
    setModified(true);
}

// ---- Create / Close ----

void Project::createNew(const QString& name)
{
    close();
    m_name = name.isEmpty() ? QStringLiteral("Untitled") : name;
    m_created = QDateTime::currentDateTimeUtc();
    m_modified_time = m_created;
    m_modified_flag = false;
}

void Project::close()
{
    m_name.clear();
    m_author.clear();
    m_description.clear();
    m_units = QStringLiteral("mm");
    m_projectPath.clear();
    m_modified_flag = false;

    m_shapes.clear();
    m_constructionPlanes.clear();
    m_sketches.clear();
    m_parameters.clear();
    m_features.clear();
    m_geometryFiles.clear();
    m_constructionPlaneFiles.clear();
    m_sketchFiles.clear();
}

// ---- JSON Serialization: Construction Planes ----

QJsonObject Project::constructionPlaneToJson(const ConstructionPlaneData& plane) const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = plane.id;
    obj[QStringLiteral("name")] = plane.name;
    obj[QStringLiteral("type")] = static_cast<int>(plane.type);
    obj[QStringLiteral("base_plane")] = static_cast<int>(plane.basePlane);
    obj[QStringLiteral("base_plane_id")] = plane.basePlaneId;

    // Origin point (plane center in absolute coordinates)
    obj[QStringLiteral("origin_x")] = plane.originX;
    obj[QStringLiteral("origin_y")] = plane.originY;
    obj[QStringLiteral("origin_z")] = plane.originZ;

    obj[QStringLiteral("offset")] = plane.offset;
    obj[QStringLiteral("primary_axis")] = static_cast<int>(plane.primaryAxis);
    obj[QStringLiteral("primary_angle")] = plane.primaryAngle;
    obj[QStringLiteral("secondary_axis")] = static_cast<int>(plane.secondaryAxis);
    obj[QStringLiteral("secondary_angle")] = plane.secondaryAngle;
    obj[QStringLiteral("roll_angle")] = plane.rollAngle;
    obj[QStringLiteral("visible")] = plane.visible;
    return obj;
}

ConstructionPlaneData Project::constructionPlaneFromJson(const QJsonObject& json) const
{
    ConstructionPlaneData plane;
    plane.id = json[QStringLiteral("id")].toInt();
    plane.name = json[QStringLiteral("name")].toString();
    plane.type = static_cast<ConstructionPlaneType>(json[QStringLiteral("type")].toInt());
    plane.basePlane = static_cast<SketchPlane>(json[QStringLiteral("base_plane")].toInt());
    plane.basePlaneId = json[QStringLiteral("base_plane_id")].toInt(-1);

    // Origin point (plane center in absolute coordinates)
    plane.originX = json[QStringLiteral("origin_x")].toDouble(0.0);
    plane.originY = json[QStringLiteral("origin_y")].toDouble(0.0);
    plane.originZ = json[QStringLiteral("origin_z")].toDouble(0.0);

    plane.offset = json[QStringLiteral("offset")].toDouble(0.0);
    plane.primaryAxis = static_cast<PlaneRotationAxis>(json[QStringLiteral("primary_axis")].toInt());
    plane.primaryAngle = json[QStringLiteral("primary_angle")].toDouble(0.0);
    plane.secondaryAxis = static_cast<PlaneRotationAxis>(json[QStringLiteral("secondary_axis")].toInt());
    plane.secondaryAngle = json[QStringLiteral("secondary_angle")].toDouble(0.0);
    plane.rollAngle = json[QStringLiteral("roll_angle")].toDouble(0.0);
    plane.visible = json[QStringLiteral("visible")].toBool(true);
    return plane;
}

// ---- JSON Serialization: Sketches ----

QJsonObject Project::sketchToJson(const SketchData& sketch) const
{
    QJsonObject obj;
    obj[QStringLiteral("name")] = sketch.name;
    obj[QStringLiteral("plane")] = static_cast<int>(sketch.plane);
    obj[QStringLiteral("construction_plane_id")] = sketch.constructionPlaneId;
    obj[QStringLiteral("plane_offset")] = sketch.planeOffset;
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        obj[QStringLiteral("rotation_axis")] = static_cast<int>(sketch.rotationAxis);
        obj[QStringLiteral("rotation_angle")] = sketch.rotationAngle;
    }
    obj[QStringLiteral("grid_spacing")] = sketch.gridSpacing;

    QJsonArray entities;
    for (const auto& entity : sketch.entities) {
        QJsonObject ent;
        ent[QStringLiteral("id")] = entity.id;
        ent[QStringLiteral("type")] = static_cast<int>(entity.type);

        QJsonArray pts;
        for (const auto& pt : entity.points) {
            QJsonArray ptArr;
            ptArr.append(pt.x());
            ptArr.append(pt.y());
            pts.append(ptArr);
        }
        ent[QStringLiteral("points")] = pts;

        if (entity.type == SketchEntityType::Circle ||
            entity.type == SketchEntityType::Arc ||
            entity.type == SketchEntityType::Slot) {
            ent[QStringLiteral("radius")] = entity.radius;
        }
        if (entity.type == SketchEntityType::Arc) {
            ent[QStringLiteral("start_angle")] = entity.startAngle;
            ent[QStringLiteral("sweep_angle")] = entity.sweepAngle;
        }
        if (entity.type == SketchEntityType::Polygon) {
            ent[QStringLiteral("sides")] = entity.sides;
        }
        if (entity.type == SketchEntityType::Ellipse) {
            ent[QStringLiteral("major_radius")] = entity.majorRadius;
            ent[QStringLiteral("minor_radius")] = entity.minorRadius;
        }
        if (entity.type == SketchEntityType::Text) {
            ent[QStringLiteral("text")] = entity.text;
        }
        ent[QStringLiteral("constrained")] = entity.constrained;
        ent[QStringLiteral("is_construction")] = entity.isConstruction;

        entities.append(ent);
    }
    obj[QStringLiteral("entities")] = entities;

    // Serialize constraints
    QJsonArray constraints;
    for (const auto& constraint : sketch.constraints) {
        QJsonObject c;
        c[QStringLiteral("id")] = constraint.id;
        c[QStringLiteral("type")] = static_cast<int>(constraint.type);

        // Entity IDs
        QJsonArray eids;
        for (int eid : constraint.entityIds) {
            eids.append(eid);
        }
        c[QStringLiteral("entity_ids")] = eids;

        // Point indices
        QJsonArray pidxs;
        for (int pidx : constraint.pointIndices) {
            pidxs.append(pidx);
        }
        c[QStringLiteral("point_indices")] = pidxs;

        c[QStringLiteral("value")] = constraint.value;
        c[QStringLiteral("is_driving")] = constraint.isDriving;
        c[QStringLiteral("label_x")] = constraint.labelPosition.x();
        c[QStringLiteral("label_y")] = constraint.labelPosition.y();
        c[QStringLiteral("label_visible")] = constraint.labelVisible;
        c[QStringLiteral("enabled")] = constraint.enabled;

        constraints.append(c);
    }
    obj[QStringLiteral("constraints")] = constraints;

    // Serialize background image (only if enabled)
    if (sketch.backgroundImage.enabled) {
        QJsonObject bg;
        bg[QStringLiteral("enabled")] = true;
        bg[QStringLiteral("storage")] = static_cast<int>(sketch.backgroundImage.storage);
        bg[QStringLiteral("file_path")] = sketch.backgroundImage.filePath;
        bg[QStringLiteral("mime_type")] = sketch.backgroundImage.mimeType;

        // Position and size
        bg[QStringLiteral("position_x")] = sketch.backgroundImage.position.x();
        bg[QStringLiteral("position_y")] = sketch.backgroundImage.position.y();
        bg[QStringLiteral("width")] = sketch.backgroundImage.width;
        bg[QStringLiteral("height")] = sketch.backgroundImage.height;
        bg[QStringLiteral("rotation")] = sketch.backgroundImage.rotation;

        // Display options
        bg[QStringLiteral("opacity")] = sketch.backgroundImage.opacity;
        bg[QStringLiteral("lock_aspect_ratio")] = sketch.backgroundImage.lockAspectRatio;
        bg[QStringLiteral("grayscale")] = sketch.backgroundImage.grayscale;
        bg[QStringLiteral("contrast")] = sketch.backgroundImage.contrast;
        bg[QStringLiteral("brightness")] = sketch.backgroundImage.brightness;

        // Calibration
        bg[QStringLiteral("calibrated")] = sketch.backgroundImage.calibrated;
        bg[QStringLiteral("calibration_scale")] = sketch.backgroundImage.calibrationScale;

        // Embed image data if storage is Embedded
        if (sketch.backgroundImage.storage == sketch::BackgroundStorage::Embedded &&
            !sketch.backgroundImage.imageData.isEmpty()) {
            bg[QStringLiteral("image_data")] = QString::fromLatin1(
                sketch.backgroundImage.imageData.toBase64());
        }

        obj[QStringLiteral("background_image")] = bg;
    }

    return obj;
}

SketchData Project::sketchFromJson(const QJsonObject& json) const
{
    SketchData sketch;
    sketch.name = json[QStringLiteral("name")].toString();
    sketch.plane = static_cast<SketchPlane>(json[QStringLiteral("plane")].toInt());
    sketch.constructionPlaneId = json[QStringLiteral("construction_plane_id")].toInt(-1);
    sketch.planeOffset = json[QStringLiteral("plane_offset")].toDouble(0.0);
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        sketch.rotationAxis = static_cast<PlaneRotationAxis>(
            json[QStringLiteral("rotation_axis")].toInt());
        sketch.rotationAngle = json[QStringLiteral("rotation_angle")].toDouble(0.0);
    }
    sketch.gridSpacing = json[QStringLiteral("grid_spacing")].toDouble(10.0);

    QJsonArray entities = json[QStringLiteral("entities")].toArray();
    for (const auto& entVal : entities) {
        QJsonObject ent = entVal.toObject();
        SketchEntityData entity;
        entity.id = ent[QStringLiteral("id")].toInt();
        entity.type = static_cast<SketchEntityType>(ent[QStringLiteral("type")].toInt());

        QJsonArray pts = ent[QStringLiteral("points")].toArray();
        for (const auto& ptVal : pts) {
            QJsonArray ptArr = ptVal.toArray();
            if (ptArr.size() >= 2) {
                entity.points.append(QPointF(ptArr[0].toDouble(), ptArr[1].toDouble()));
            }
        }

        entity.radius = ent[QStringLiteral("radius")].toDouble();
        entity.startAngle = ent[QStringLiteral("start_angle")].toDouble();
        entity.sweepAngle = ent[QStringLiteral("sweep_angle")].toDouble();
        entity.sides = ent[QStringLiteral("sides")].toInt(6);  // Default 6 sides (hexagon)
        entity.majorRadius = ent[QStringLiteral("major_radius")].toDouble();
        entity.minorRadius = ent[QStringLiteral("minor_radius")].toDouble();
        entity.text = ent[QStringLiteral("text")].toString();
        entity.constrained = ent[QStringLiteral("constrained")].toBool();
        entity.isConstruction = ent[QStringLiteral("is_construction")].toBool();

        sketch.entities.append(entity);
    }

    // Deserialize constraints
    QJsonArray constraints = json[QStringLiteral("constraints")].toArray();
    for (const auto& cVal : constraints) {
        QJsonObject c = cVal.toObject();
        ConstraintData constraint;

        constraint.id = c[QStringLiteral("id")].toInt();
        constraint.type = static_cast<ConstraintType>(c[QStringLiteral("type")].toInt());

        // Entity IDs
        QJsonArray eids = c[QStringLiteral("entity_ids")].toArray();
        for (const auto& eid : eids) {
            constraint.entityIds.append(eid.toInt());
        }

        // Point indices
        QJsonArray pidxs = c[QStringLiteral("point_indices")].toArray();
        for (const auto& pidx : pidxs) {
            constraint.pointIndices.append(pidx.toInt());
        }

        constraint.value = c[QStringLiteral("value")].toDouble();
        constraint.isDriving = c[QStringLiteral("is_driving")].toBool(true);
        constraint.labelPosition = QPointF(
            c[QStringLiteral("label_x")].toDouble(),
            c[QStringLiteral("label_y")].toDouble()
        );
        constraint.labelVisible = c[QStringLiteral("label_visible")].toBool(true);
        constraint.enabled = c[QStringLiteral("enabled")].toBool(true);

        sketch.constraints.append(constraint);
    }

    // Deserialize background image
    if (json.contains(QStringLiteral("background_image"))) {
        QJsonObject bg = json[QStringLiteral("background_image")].toObject();

        sketch.backgroundImage.enabled = bg[QStringLiteral("enabled")].toBool(false);
        sketch.backgroundImage.storage = static_cast<sketch::BackgroundStorage>(
            bg[QStringLiteral("storage")].toInt(0));
        sketch.backgroundImage.filePath = bg[QStringLiteral("file_path")].toString();
        sketch.backgroundImage.mimeType = bg[QStringLiteral("mime_type")].toString();

        // Position and size
        sketch.backgroundImage.position = QPointF(
            bg[QStringLiteral("position_x")].toDouble(0),
            bg[QStringLiteral("position_y")].toDouble(0));
        sketch.backgroundImage.width = bg[QStringLiteral("width")].toDouble(100);
        sketch.backgroundImage.height = bg[QStringLiteral("height")].toDouble(100);
        sketch.backgroundImage.rotation = bg[QStringLiteral("rotation")].toDouble(0);

        // Display options
        sketch.backgroundImage.opacity = bg[QStringLiteral("opacity")].toDouble(0.5);
        sketch.backgroundImage.lockAspectRatio = bg[QStringLiteral("lock_aspect_ratio")].toBool(true);
        sketch.backgroundImage.grayscale = bg[QStringLiteral("grayscale")].toBool(false);
        sketch.backgroundImage.contrast = bg[QStringLiteral("contrast")].toDouble(1.0);
        sketch.backgroundImage.brightness = bg[QStringLiteral("brightness")].toDouble(0.0);

        // Calibration
        sketch.backgroundImage.calibrated = bg[QStringLiteral("calibrated")].toBool(false);
        sketch.backgroundImage.calibrationScale = bg[QStringLiteral("calibration_scale")].toDouble(1.0);

        // Embedded image data
        if (bg.contains(QStringLiteral("image_data"))) {
            sketch.backgroundImage.imageData = QByteArray::fromBase64(
                bg[QStringLiteral("image_data")].toString().toLatin1());
        }
    }

    return sketch;
}

// ---- JSON Serialization: Parameters ----

QJsonObject Project::parametersToJson() const
{
    QJsonObject obj;
    QJsonArray params;

    for (const auto& param : m_parameters) {
        QJsonObject p;
        p[QStringLiteral("name")] = param.name;
        p[QStringLiteral("expression")] = param.expression;
        p[QStringLiteral("value")] = param.value;
        p[QStringLiteral("unit")] = param.unit;
        p[QStringLiteral("comment")] = param.comment;
        p[QStringLiteral("is_user_param")] = param.isUserParam;
        params.append(p);
    }

    obj[QStringLiteral("parameters")] = params;
    return obj;
}

void Project::parametersFromJson(const QJsonObject& json)
{
    m_parameters.clear();
    QJsonArray params = json[QStringLiteral("parameters")].toArray();

    for (const auto& pVal : params) {
        QJsonObject p = pVal.toObject();
        ParameterData param;
        param.name = p[QStringLiteral("name")].toString();
        param.expression = p[QStringLiteral("expression")].toString();
        param.value = p[QStringLiteral("value")].toDouble();
        param.unit = p[QStringLiteral("unit")].toString();
        param.comment = p[QStringLiteral("comment")].toString();
        param.isUserParam = p[QStringLiteral("is_user_param")].toBool(true);
        m_parameters.append(param);
    }
}

// ---- JSON Serialization: Features ----

static QString featureTypeToString(FeatureType type)
{
    switch (type) {
    case FeatureType::Origin:    return QStringLiteral("Origin");
    case FeatureType::Sketch:    return QStringLiteral("Sketch");
    case FeatureType::Extrude:   return QStringLiteral("Extrude");
    case FeatureType::Revolve:   return QStringLiteral("Revolve");
    case FeatureType::Fillet:    return QStringLiteral("Fillet");
    case FeatureType::Chamfer:   return QStringLiteral("Chamfer");
    case FeatureType::Hole:      return QStringLiteral("Hole");
    case FeatureType::Mirror:    return QStringLiteral("Mirror");
    case FeatureType::Pattern:   return QStringLiteral("Pattern");
    case FeatureType::Box:       return QStringLiteral("Box");
    case FeatureType::Cylinder:  return QStringLiteral("Cylinder");
    case FeatureType::Sphere:    return QStringLiteral("Sphere");
    case FeatureType::Move:      return QStringLiteral("Move");
    case FeatureType::Join:      return QStringLiteral("Join");
    case FeatureType::Cut:       return QStringLiteral("Cut");
    case FeatureType::Intersect: return QStringLiteral("Intersect");
    }
    return QStringLiteral("Unknown");
}

static FeatureType featureTypeFromString(const QString& str)
{
    if (str == QStringLiteral("Origin"))    return FeatureType::Origin;
    if (str == QStringLiteral("Sketch"))    return FeatureType::Sketch;
    if (str == QStringLiteral("Extrude"))   return FeatureType::Extrude;
    if (str == QStringLiteral("Revolve"))   return FeatureType::Revolve;
    if (str == QStringLiteral("Fillet"))    return FeatureType::Fillet;
    if (str == QStringLiteral("Chamfer"))   return FeatureType::Chamfer;
    if (str == QStringLiteral("Hole"))      return FeatureType::Hole;
    if (str == QStringLiteral("Mirror"))    return FeatureType::Mirror;
    if (str == QStringLiteral("Pattern"))   return FeatureType::Pattern;
    if (str == QStringLiteral("Box"))       return FeatureType::Box;
    if (str == QStringLiteral("Cylinder"))  return FeatureType::Cylinder;
    if (str == QStringLiteral("Sphere"))    return FeatureType::Sphere;
    if (str == QStringLiteral("Move"))      return FeatureType::Move;
    if (str == QStringLiteral("Join"))      return FeatureType::Join;
    if (str == QStringLiteral("Cut"))       return FeatureType::Cut;
    if (str == QStringLiteral("Intersect")) return FeatureType::Intersect;
    return FeatureType::Origin;
}

QJsonObject Project::featuresToJson() const
{
    QJsonObject obj;
    QJsonArray features;

    for (const auto& feature : m_features) {
        QJsonObject f;
        f[QStringLiteral("id")] = feature.id;
        f[QStringLiteral("type")] = featureTypeToString(feature.type);
        f[QStringLiteral("name")] = feature.name;
        if (!feature.properties.isEmpty()) {
            f[QStringLiteral("properties")] = feature.properties;
        }
        features.append(f);
    }

    obj[QStringLiteral("features")] = features;
    return obj;
}

void Project::featuresFromJson(const QJsonObject& json)
{
    m_features.clear();
    QJsonArray features = json[QStringLiteral("features")].toArray();

    for (const auto& fVal : features) {
        QJsonObject f = fVal.toObject();
        FeatureData feature;
        feature.id = f[QStringLiteral("id")].toInt();
        feature.type = featureTypeFromString(f[QStringLiteral("type")].toString());
        feature.name = f[QStringLiteral("name")].toString();
        feature.properties = f[QStringLiteral("properties")].toObject();
        m_features.append(feature);
    }
}

// ---- Manifest ----

QJsonObject Project::manifestToJson() const
{
    QJsonObject obj;

    // Version info
    obj[QStringLiteral("hobbycad_version")] = QString::fromLatin1(HOBBYCAD_VERSION);
    obj[QStringLiteral("format_version")] = FORMAT_VERSION;

    // Metadata
    obj[QStringLiteral("project_name")] = m_name;
    obj[QStringLiteral("author")] = m_author;
    obj[QStringLiteral("description")] = m_description;
    obj[QStringLiteral("units")] = m_units;
    obj[QStringLiteral("created")] = m_created.toString(Qt::ISODate);
    obj[QStringLiteral("modified")] = m_modified_time.toString(Qt::ISODate);

    // File references
    obj[QStringLiteral("geometry")] = QJsonArray::fromStringList(m_geometryFiles);
    obj[QStringLiteral("construction_planes")] = QJsonArray::fromStringList(m_constructionPlaneFiles);
    obj[QStringLiteral("sketches")] = QJsonArray::fromStringList(m_sketchFiles);
    obj[QStringLiteral("parameters")] = QStringLiteral("features/parameters.json");
    obj[QStringLiteral("features")] = QStringLiteral("features/feature_tree.json");

    return obj;
}

bool Project::manifestFromJson(const QJsonObject& json, QString* errorMsg)
{
    // Check format version
    int formatVersion = json[QStringLiteral("format_version")].toInt(0);
    if (formatVersion > FORMAT_VERSION) {
        if (errorMsg) {
            *errorMsg = QStringLiteral("Project was created with a newer version of HobbyCAD (format %1, this version supports %2)")
                        .arg(formatVersion).arg(FORMAT_VERSION);
        }
        return false;
    }

    // Metadata
    m_name = json[QStringLiteral("project_name")].toString();
    m_author = json[QStringLiteral("author")].toString();
    m_description = json[QStringLiteral("description")].toString();
    m_units = json[QStringLiteral("units")].toString(QStringLiteral("mm"));
    m_created = QDateTime::fromString(json[QStringLiteral("created")].toString(), Qt::ISODate);
    m_modified_time = QDateTime::fromString(json[QStringLiteral("modified")].toString(), Qt::ISODate);

    // File references
    m_geometryFiles.clear();
    for (const auto& val : json[QStringLiteral("geometry")].toArray()) {
        m_geometryFiles.append(val.toString());
    }

    m_constructionPlaneFiles.clear();
    for (const auto& val : json[QStringLiteral("construction_planes")].toArray()) {
        m_constructionPlaneFiles.append(val.toString());
    }

    m_sketchFiles.clear();
    for (const auto& val : json[QStringLiteral("sketches")].toArray()) {
        m_sketchFiles.append(val.toString());
    }

    return true;
}

// ---- File I/O: Save ----

bool Project::save(const QString& path, QString* errorMsg)
{
    QString savePath = path.isEmpty() ? m_projectPath : path;
    if (savePath.isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("No save path specified");
        return false;
    }

    // Project structure per project_definition.txt Section 5.2:
    //   my_widget/              <- directory (no .hcad extension)
    //     my_widget.hcad        <- manifest (named after directory)
    //     geometry/
    //     sketches/
    //     features/
    //     metadata/
    //
    // If user provides a path ending in .hcad, treat it as the manifest path
    // and use its parent directory as the project directory.
    if (savePath.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive)) {
        QFileInfo info(savePath);
        if (info.isFile() || !info.exists()) {
            // User specified the manifest file path - use parent as project dir
            savePath = info.absolutePath();
            // Extract project name from manifest filename
            QString baseName = info.completeBaseName();  // e.g., "my_widget" from "my_widget.hcad"
            if (!baseName.isEmpty() && m_name.isEmpty()) {
                m_name = baseName;
            }
        }
        // If it's an existing directory ending in .hcad, use it as-is (legacy support)
    }

    // Set project name from directory if not already set
    if (m_name.isEmpty()) {
        QDir dir(savePath);
        m_name = dir.dirName();
    }

    // Create directory structure
    QDir dir(savePath);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to create project directory");
            return false;
        }
    }

    // Create subdirectories
    dir.mkpath(QStringLiteral("geometry"));
    dir.mkpath(QStringLiteral("construction"));
    dir.mkpath(QStringLiteral("sketches"));
    dir.mkpath(QStringLiteral("features"));
    dir.mkpath(QStringLiteral("metadata"));

    // Save all components
    if (!saveGeometry(savePath, errorMsg)) return false;
    if (!saveConstructionPlanes(savePath, errorMsg)) return false;
    if (!saveSketches(savePath, errorMsg)) return false;
    if (!saveParameters(savePath, errorMsg)) return false;
    if (!saveFeatures(savePath, errorMsg)) return false;
    if (!saveManifest(savePath, errorMsg)) return false;

    m_projectPath = savePath;
    m_modified_flag = false;
    return true;
}

bool Project::saveManifest(const QString& dir, QString* errorMsg)
{
    // Manifest is named after the project: my_widget/my_widget.hcad
    QDir d(dir);
    QString manifestName = d.dirName() + QStringLiteral(".hcad");
    QString path = dir + QStringLiteral("/") + manifestName;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to create manifest: %1").arg(file.errorString());
        return false;
    }

    QJsonDocument doc(manifestToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool Project::saveGeometry(const QString& dir, QString* errorMsg)
{
    m_geometryFiles.clear();

    for (int i = 0; i < m_shapes.size(); ++i) {
        QString relPath = QStringLiteral("geometry/body_%1.brep").arg(i + 1, 3, 10, QLatin1Char('0'));
        QString fullPath = dir + QStringLiteral("/") + relPath;

        if (!brep_io::writeBrep(fullPath, {m_shapes[i]}, errorMsg)) {
            return false;
        }
        m_geometryFiles.append(relPath);
    }

    return true;
}

bool Project::saveConstructionPlanes(const QString& dir, QString* errorMsg)
{
    m_constructionPlaneFiles.clear();

    for (int i = 0; i < m_constructionPlanes.size(); ++i) {
        QString relPath = QStringLiteral("construction/plane_%1.json").arg(i + 1, 3, 10, QLatin1Char('0'));
        QString fullPath = dir + QStringLiteral("/") + relPath;

        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to save construction plane: %1").arg(file.errorString());
            return false;
        }

        QJsonDocument doc(constructionPlaneToJson(m_constructionPlanes[i]));
        file.write(doc.toJson(QJsonDocument::Indented));
        m_constructionPlaneFiles.append(relPath);
    }

    return true;
}

bool Project::saveSketches(const QString& dir, QString* errorMsg)
{
    m_sketchFiles.clear();

    for (int i = 0; i < m_sketches.size(); ++i) {
        QString relPath = QStringLiteral("sketches/sketch_%1.json").arg(i + 1, 3, 10, QLatin1Char('0'));
        QString fullPath = dir + QStringLiteral("/") + relPath;

        QFile file(fullPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to save sketch: %1").arg(file.errorString());
            return false;
        }

        QJsonDocument doc(sketchToJson(m_sketches[i]));
        file.write(doc.toJson(QJsonDocument::Indented));
        m_sketchFiles.append(relPath);
    }

    return true;
}

bool Project::saveParameters(const QString& dir, QString* errorMsg)
{
    QString path = dir + QStringLiteral("/features/parameters.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to save parameters: %1").arg(file.errorString());
        return false;
    }

    QJsonDocument doc(parametersToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool Project::saveFeatures(const QString& dir, QString* errorMsg)
{
    QString path = dir + QStringLiteral("/features/feature_tree.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to save features: %1").arg(file.errorString());
        return false;
    }

    QJsonDocument doc(featuresToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

// ---- File I/O: Load ----

/// Find the .hcad manifest file in a project directory.
/// Returns empty string if not found.
static QString findManifest(const QString& dirPath)
{
    QDir dir(dirPath);

    // Look for <dirname>.hcad first (standard naming)
    QString standardName = dir.dirName() + QStringLiteral(".hcad");
    QString standardPath = dirPath + QStringLiteral("/") + standardName;
    if (QFileInfo::exists(standardPath)) {
        return standardPath;
    }

    // Fall back to any .hcad file in the directory
    QStringList filters;
    filters << QStringLiteral("*.hcad");
    QStringList hcadFiles = dir.entryList(filters, QDir::Files);
    if (!hcadFiles.isEmpty()) {
        return dirPath + QStringLiteral("/") + hcadFiles.first();
    }

    return QString();
}

bool Project::load(const QString& path, QString* errorMsg)
{
    QString projectDir = path;
    QString manifestPath;

    QFileInfo info(path);
    if (info.isFile() && path.endsWith(QStringLiteral(".hcad"), Qt::CaseInsensitive)) {
        // User specified the manifest file directly
        manifestPath = path;
        projectDir = info.absolutePath();
    } else if (info.isDir()) {
        // User specified the project directory
        projectDir = path;
        manifestPath = findManifest(projectDir);
    } else {
        if (errorMsg) *errorMsg = QStringLiteral("Path does not exist: %1").arg(path);
        return false;
    }

    if (manifestPath.isEmpty()) {
        if (errorMsg) *errorMsg = QStringLiteral("Not a valid HobbyCAD project (no .hcad manifest found)");
        return false;
    }

    close();

    // Load manifest first
    if (!loadManifestFile(manifestPath, errorMsg)) return false;

    // Load all components
    if (!loadGeometry(projectDir, errorMsg)) return false;
    if (!loadConstructionPlanes(projectDir, errorMsg)) return false;
    if (!loadSketches(projectDir, errorMsg)) return false;
    if (!loadParameters(projectDir, errorMsg)) return false;
    if (!loadFeatures(projectDir, errorMsg)) return false;

    m_projectPath = projectDir;
    m_modified_flag = false;
    return true;
}

bool Project::loadManifestFile(const QString& manifestPath, QString* errorMsg)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to read manifest: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid manifest JSON: %1").arg(parseError.errorString());
        return false;
    }

    return manifestFromJson(doc.object(), errorMsg);
}

bool Project::loadGeometry(const QString& dir, QString* errorMsg)
{
    m_shapes.clear();

    for (const QString& relPath : m_geometryFiles) {
        QString fullPath = dir + QStringLiteral("/") + relPath;
        if (!QFileInfo::exists(fullPath)) {
            // Skip missing files (may have been deleted)
            continue;
        }

        auto shapes = brep_io::readBrep(fullPath, errorMsg);
        if (shapes.isEmpty() && errorMsg && !errorMsg->isEmpty()) {
            return false;
        }
        m_shapes.append(shapes);
    }

    return true;
}

bool Project::loadConstructionPlanes(const QString& dir, QString* errorMsg)
{
    m_constructionPlanes.clear();

    for (const QString& relPath : m_constructionPlaneFiles) {
        QString fullPath = dir + QStringLiteral("/") + relPath;
        if (!QFileInfo::exists(fullPath)) {
            continue;
        }

        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to read construction plane: %1").arg(file.errorString());
            return false;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (errorMsg) *errorMsg = QStringLiteral("Invalid construction plane JSON: %1").arg(parseError.errorString());
            return false;
        }

        m_constructionPlanes.append(constructionPlaneFromJson(doc.object()));
    }

    return true;
}

bool Project::loadSketches(const QString& dir, QString* errorMsg)
{
    m_sketches.clear();

    for (const QString& relPath : m_sketchFiles) {
        QString fullPath = dir + QStringLiteral("/") + relPath;
        if (!QFileInfo::exists(fullPath)) {
            continue;
        }

        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QStringLiteral("Failed to read sketch: %1").arg(file.errorString());
            return false;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (errorMsg) *errorMsg = QStringLiteral("Invalid sketch JSON: %1").arg(parseError.errorString());
            return false;
        }

        m_sketches.append(sketchFromJson(doc.object()));
    }

    return true;
}

bool Project::loadParameters(const QString& dir, QString* errorMsg)
{
    QString path = dir + QStringLiteral("/features/parameters.json");
    if (!QFileInfo::exists(path)) {
        // Parameters file is optional
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to read parameters: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid parameters JSON: %1").arg(parseError.errorString());
        return false;
    }

    parametersFromJson(doc.object());
    return true;
}

bool Project::loadFeatures(const QString& dir, QString* errorMsg)
{
    QString path = dir + QStringLiteral("/features/feature_tree.json");
    if (!QFileInfo::exists(path)) {
        // Features file is optional
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = QStringLiteral("Failed to read features: %1").arg(file.errorString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QStringLiteral("Invalid features JSON: %1").arg(parseError.errorString());
        return false;
    }

    featuresFromJson(doc.object());
    return true;
}

}  // namespace hobbycad
