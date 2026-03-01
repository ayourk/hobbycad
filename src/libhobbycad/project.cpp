// =====================================================================
//  src/libhobbycad/project.cpp — HobbyCAD project container
// =====================================================================

#include "hobbycad/project.h"
#include "hobbycad/brep_io.h"
#include "hobbycad/format.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#if HOBBYCAD_HAS_QT
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#endif

namespace hobbycad {

const char* Project::HOBBYCAD_VERSION = "0.1.0";

Project::Project()
{
    // Timestamps left empty — no Qt dependency for time
    // The save code will populate them if needed
}

Project::~Project() = default;

// ---- Modification tracking ----

void Project::setModified(bool modified)
{
    m_modified_flag = modified;
}

// ---- Geometry ----

void Project::addShape(const TopoDS_Shape& shape)
{
    m_shapes.push_back(shape);
    setModified(true);
}

void Project::setShapes(const std::vector<TopoDS_Shape>& shapes)
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
    m_constructionPlanes.push_back(plane);
    setModified(true);
}

void Project::setConstructionPlane(int index, const ConstructionPlaneData& plane)
{
    if (index >= 0 && index < static_cast<int>(m_constructionPlanes.size())) {
        m_constructionPlanes[index] = plane;
        setModified(true);
    }
}

void Project::removeConstructionPlane(int index)
{
    if (index >= 0 && index < static_cast<int>(m_constructionPlanes.size())) {
        m_constructionPlanes.erase(m_constructionPlanes.begin() + index);
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
    m_sketches.push_back(sketch);
    setModified(true);
}

void Project::setSketch(int index, const SketchData& sketch)
{
    if (index >= 0 && index < static_cast<int>(m_sketches.size())) {
        m_sketches[index] = sketch;
        setModified(true);
    }
}

void Project::removeSketch(int index)
{
    if (index >= 0 && index < static_cast<int>(m_sketches.size())) {
        m_sketches.erase(m_sketches.begin() + index);
        setModified(true);
    }
}

void Project::clearSketches()
{
    m_sketches.clear();
    setModified(true);
}

// ---- Parameters ----

void Project::setParameters(const std::vector<ParameterData>& params)
{
    m_parameters = params;
    setModified(true);
}

void Project::addParameter(const ParameterData& param)
{
    m_parameters.push_back(param);
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
    m_features.push_back(feature);
    setModified(true);
}

void Project::setFeatures(const std::vector<FeatureData>& features)
{
    m_features = features;
    setModified(true);
}

void Project::clearFeatures()
{
    m_features.clear();
    setModified(true);
}

// ---- Foreign Files ----

void Project::addForeignFile(const ForeignFileData& file)
{
    // Check if already exists
    for (const auto& existing : m_foreignFiles) {
        if (existing.path == file.path) {
            return;
        }
    }
    m_foreignFiles.push_back(file);
    setModified(true);
}

void Project::addForeignFile(const std::string& path, const std::string& category,
                              const std::string& description)
{
    ForeignFileData file;
    file.path = path;
    file.category = category;
    file.description = description;
    addForeignFile(file);
}

void Project::removeForeignFile(const std::string& path)
{
    for (size_t i = 0; i < m_foreignFiles.size(); ++i) {
        if (m_foreignFiles[i].path == path) {
            m_foreignFiles.erase(m_foreignFiles.begin() + static_cast<ptrdiff_t>(i));
            setModified(true);
            return;
        }
    }
}

void Project::setForeignFiles(const std::vector<ForeignFileData>& files)
{
    m_foreignFiles = files;
    setModified(true);
}

void Project::clearForeignFiles()
{
    m_foreignFiles.clear();
    setModified(true);
}

bool Project::isForeignFile(const std::string& relativePath) const
{
    for (const auto& file : m_foreignFiles) {
        if (file.path == relativePath) {
            return true;
        }
        // Check if path is under a foreign directory (e.g., "docs/" matches "docs/readme.txt")
        if (!file.path.empty() && file.path.back() == '/' &&
            relativePath.substr(0, file.path.size()) == file.path) {
            return true;
        }
    }
    return false;
}

const ForeignFileData* Project::foreignFileByPath(const std::string& path) const
{
    for (const auto& file : m_foreignFiles) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

// ---- Create / Close ----

void Project::createNew(const std::string& name)
{
    close();
    m_name = name.empty() ? "Untitled" : name;
    m_modified_flag = false;
}

void Project::close()
{
    m_name.clear();
    m_author.clear();
    m_description.clear();
    m_units = "mm";
    m_projectPath.clear();
    m_modified_flag = false;

    m_shapes.clear();
    m_constructionPlanes.clear();
    m_sketches.clear();
    m_parameters.clear();
    m_features.clear();
    m_foreignFiles.clear();
    m_geometryFiles.clear();
    m_constructionPlaneFiles.clear();
    m_sketchFiles.clear();
}

// =====================================================================
//  JSON Serialization — only available when compiled with Qt
// =====================================================================

#if HOBBYCAD_HAS_QT

// ---- JSON Serialization: Construction Planes ----

QJsonObject Project::constructionPlaneToJson(const ConstructionPlaneData& plane) const
{
    QJsonObject obj;
    obj["id"] = plane.id;
    obj["name"] = QString::fromStdString(plane.name);
    obj["type"] = static_cast<int>(plane.type);
    obj["base_plane"] = static_cast<int>(plane.basePlane);
    obj["base_plane_id"] = plane.basePlaneId;

    // Origin point (plane center in absolute coordinates)
    obj["origin_x"] = plane.originX;
    obj["origin_y"] = plane.originY;
    obj["origin_z"] = plane.originZ;

    obj["offset"] = plane.offset;
    obj["primary_axis"] = static_cast<int>(plane.primaryAxis);
    obj["primary_angle"] = plane.primaryAngle;
    obj["secondary_axis"] = static_cast<int>(plane.secondaryAxis);
    obj["secondary_angle"] = plane.secondaryAngle;
    obj["roll_angle"] = plane.rollAngle;
    obj["visible"] = plane.visible;
    return obj;
}

ConstructionPlaneData Project::constructionPlaneFromJson(const QJsonObject& json) const
{
    ConstructionPlaneData plane;
    plane.id = json["id"].toInt();
    plane.name = json["name"].toString().toStdString();
    plane.type = static_cast<ConstructionPlaneType>(json["type"].toInt());
    plane.basePlane = static_cast<SketchPlane>(json["base_plane"].toInt());
    plane.basePlaneId = json["base_plane_id"].toInt(-1);

    // Origin point (plane center in absolute coordinates)
    plane.originX = json["origin_x"].toDouble(0.0);
    plane.originY = json["origin_y"].toDouble(0.0);
    plane.originZ = json["origin_z"].toDouble(0.0);

    plane.offset = json["offset"].toDouble(0.0);
    plane.primaryAxis = static_cast<PlaneRotationAxis>(json["primary_axis"].toInt());
    plane.primaryAngle = json["primary_angle"].toDouble(0.0);
    plane.secondaryAxis = static_cast<PlaneRotationAxis>(json["secondary_axis"].toInt());
    plane.secondaryAngle = json["secondary_angle"].toDouble(0.0);
    plane.rollAngle = json["roll_angle"].toDouble(0.0);
    plane.visible = json["visible"].toBool(true);
    return plane;
}

// ---- JSON Serialization: Sketches ----

QJsonObject Project::sketchToJson(const SketchData& sketch) const
{
    QJsonObject obj;
    obj["name"] = QString::fromStdString(sketch.name);
    obj["plane"] = static_cast<int>(sketch.plane);
    obj["construction_plane_id"] = sketch.constructionPlaneId;
    obj["plane_offset"] = sketch.planeOffset;
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        obj["rotation_axis"] = static_cast<int>(sketch.rotationAxis);
        obj["rotation_angle"] = sketch.rotationAngle;
    }
    obj["grid_spacing"] = sketch.gridSpacing;

    QJsonArray entities;
    for (const auto& entity : sketch.entities) {
        QJsonObject ent;
        ent["id"] = entity.id;
        ent["type"] = static_cast<int>(entity.type);

        QJsonArray pts;
        for (const auto& pt : entity.points) {
            QJsonArray ptArr;
            ptArr.append(pt.x);
            ptArr.append(pt.y);
            pts.append(ptArr);
        }
        ent["points"] = pts;

        if (entity.type == SketchEntityType::Circle ||
            entity.type == SketchEntityType::Arc ||
            entity.type == SketchEntityType::Slot) {
            ent["radius"] = entity.radius;
        }
        if (entity.type == SketchEntityType::Arc) {
            ent["start_angle"] = entity.startAngle;
            ent["sweep_angle"] = entity.sweepAngle;
        }
        if (entity.type == SketchEntityType::Polygon) {
            ent["sides"] = entity.sides;
        }
        if (entity.type == SketchEntityType::Ellipse) {
            ent["major_radius"] = entity.majorRadius;
            ent["minor_radius"] = entity.minorRadius;
        }
        if (entity.type == SketchEntityType::Text) {
            ent["text"] = QString::fromStdString(entity.text);
            if (!entity.fontFamily.empty()) {
                ent["font_family"] = QString::fromStdString(entity.fontFamily);
            }
            ent["font_size"] = entity.fontSize;
            ent["font_bold"] = entity.fontBold;
            ent["font_italic"] = entity.fontItalic;
            if (!fuzzyIsNull(entity.textRotation)) {
                ent["text_rotation"] = entity.textRotation;
            }
        }
        if (entity.type == SketchEntityType::Slot && entity.arcFlipped) {
            ent["arc_flipped"] = true;
        }
        ent["constrained"] = entity.constrained;
        ent["is_construction"] = entity.isConstruction;

        entities.append(ent);
    }
    obj["entities"] = entities;

    // Serialize constraints
    QJsonArray constraints;
    for (const auto& constraint : sketch.constraints) {
        QJsonObject c;
        c["id"] = constraint.id;
        c["type"] = static_cast<int>(constraint.type);

        // Entity IDs
        QJsonArray eids;
        for (int eid : constraint.entityIds) {
            eids.append(eid);
        }
        c["entity_ids"] = eids;

        // Point indices
        QJsonArray pidxs;
        for (int pidx : constraint.pointIndices) {
            pidxs.append(pidx);
        }
        c["point_indices"] = pidxs;

        c["value"] = constraint.value;
        c["is_driving"] = constraint.isDriving;
        c["label_x"] = constraint.labelPosition.x;
        c["label_y"] = constraint.labelPosition.y;
        c["label_visible"] = constraint.labelVisible;
        c["enabled"] = constraint.enabled;

        constraints.append(c);
    }
    obj["constraints"] = constraints;

    // Serialize background image (only if enabled)
    if (sketch.backgroundImage.enabled) {
        QJsonObject bg;
        bg["enabled"] = true;
        bg["storage"] = static_cast<int>(sketch.backgroundImage.storage);
        bg["file_path"] = QString::fromStdString(sketch.backgroundImage.filePath);
        bg["mime_type"] = QString::fromStdString(sketch.backgroundImage.mimeType);

        // Position and size
        bg["position_x"] = sketch.backgroundImage.position.x;
        bg["position_y"] = sketch.backgroundImage.position.y;
        bg["width"] = sketch.backgroundImage.width;
        bg["height"] = sketch.backgroundImage.height;
        bg["rotation"] = sketch.backgroundImage.rotation;

        // Display options
        bg["opacity"] = sketch.backgroundImage.opacity;
        bg["lock_aspect_ratio"] = sketch.backgroundImage.lockAspectRatio;
        bg["grayscale"] = sketch.backgroundImage.grayscale;
        bg["contrast"] = sketch.backgroundImage.contrast;
        bg["brightness"] = sketch.backgroundImage.brightness;

        // Calibration
        bg["calibrated"] = sketch.backgroundImage.calibrated;
        bg["calibration_scale"] = sketch.backgroundImage.calibrationScale;

        // Embed image data if storage is Embedded
        if (sketch.backgroundImage.storage == sketch::BackgroundStorage::Embedded &&
            !sketch.backgroundImage.imageData.empty()) {
            QByteArray rawData(reinterpret_cast<const char*>(sketch.backgroundImage.imageData.data()),
                               static_cast<int>(sketch.backgroundImage.imageData.size()));
            bg["image_data"] = QString::fromLatin1(rawData.toBase64());
        }

        obj["background_image"] = bg;
    }

    return obj;
}

SketchData Project::sketchFromJson(const QJsonObject& json) const
{
    SketchData sketch;
    sketch.name = json["name"].toString().toStdString();
    sketch.plane = static_cast<SketchPlane>(json["plane"].toInt());
    sketch.constructionPlaneId = json["construction_plane_id"].toInt(-1);
    sketch.planeOffset = json["plane_offset"].toDouble(0.0);
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        sketch.rotationAxis = static_cast<PlaneRotationAxis>(
            json["rotation_axis"].toInt());
        sketch.rotationAngle = json["rotation_angle"].toDouble(0.0);
    }
    sketch.gridSpacing = json["grid_spacing"].toDouble(10.0);

    QJsonArray entities = json["entities"].toArray();
    for (const auto& entVal : entities) {
        QJsonObject ent = entVal.toObject();
        SketchEntityData entity;
        entity.id = ent["id"].toInt();
        entity.type = static_cast<SketchEntityType>(ent["type"].toInt());

        QJsonArray pts = ent["points"].toArray();
        for (const auto& ptVal : pts) {
            QJsonArray ptArr = ptVal.toArray();
            if (ptArr.size() >= 2) {
                entity.points.push_back(Point2D(ptArr[0].toDouble(), ptArr[1].toDouble()));
            }
        }

        entity.radius = ent["radius"].toDouble();
        entity.startAngle = ent["start_angle"].toDouble();
        entity.sweepAngle = ent["sweep_angle"].toDouble();
        entity.sides = ent["sides"].toInt(6);  // Default 6 sides (hexagon)
        entity.majorRadius = ent["major_radius"].toDouble();
        entity.minorRadius = ent["minor_radius"].toDouble();
        entity.text = ent["text"].toString().toStdString();
        entity.fontFamily = ent["font_family"].toString().toStdString();
        entity.fontSize = ent["font_size"].toDouble(12.0);
        entity.fontBold = ent["font_bold"].toBool();
        entity.fontItalic = ent["font_italic"].toBool();
        entity.textRotation = ent["text_rotation"].toDouble();
        entity.arcFlipped = ent["arc_flipped"].toBool();
        entity.constrained = ent["constrained"].toBool();
        entity.isConstruction = ent["is_construction"].toBool();

        sketch.entities.push_back(entity);
    }

    // Deserialize constraints
    QJsonArray constraints = json["constraints"].toArray();
    for (const auto& cVal : constraints) {
        QJsonObject c = cVal.toObject();
        ConstraintData constraint;

        constraint.id = c["id"].toInt();
        constraint.type = static_cast<ConstraintType>(c["type"].toInt());

        // Entity IDs
        QJsonArray eids = c["entity_ids"].toArray();
        for (const auto& eid : eids) {
            constraint.entityIds.push_back(eid.toInt());
        }

        // Point indices
        QJsonArray pidxs = c["point_indices"].toArray();
        for (const auto& pidx : pidxs) {
            constraint.pointIndices.push_back(pidx.toInt());
        }

        constraint.value = c["value"].toDouble();
        constraint.isDriving = c["is_driving"].toBool(true);
        constraint.labelPosition = Point2D(
            c["label_x"].toDouble(),
            c["label_y"].toDouble()
        );
        constraint.labelVisible = c["label_visible"].toBool(true);
        constraint.enabled = c["enabled"].toBool(true);

        sketch.constraints.push_back(constraint);
    }

    // Deserialize background image
    if (json.contains("background_image")) {
        QJsonObject bg = json["background_image"].toObject();

        sketch.backgroundImage.enabled = bg["enabled"].toBool(false);
        sketch.backgroundImage.storage = static_cast<sketch::BackgroundStorage>(
            bg["storage"].toInt(0));
        sketch.backgroundImage.filePath = bg["file_path"].toString().toStdString();
        sketch.backgroundImage.mimeType = bg["mime_type"].toString().toStdString();

        // Position and size
        sketch.backgroundImage.position = Point2D(
            bg["position_x"].toDouble(0),
            bg["position_y"].toDouble(0));
        sketch.backgroundImage.width = bg["width"].toDouble(100);
        sketch.backgroundImage.height = bg["height"].toDouble(100);
        sketch.backgroundImage.rotation = bg["rotation"].toDouble(0);

        // Display options
        sketch.backgroundImage.opacity = bg["opacity"].toDouble(0.5);
        sketch.backgroundImage.lockAspectRatio = bg["lock_aspect_ratio"].toBool(true);
        sketch.backgroundImage.grayscale = bg["grayscale"].toBool(false);
        sketch.backgroundImage.contrast = bg["contrast"].toDouble(1.0);
        sketch.backgroundImage.brightness = bg["brightness"].toDouble(0.0);

        // Calibration
        sketch.backgroundImage.calibrated = bg["calibrated"].toBool(false);
        sketch.backgroundImage.calibrationScale = bg["calibration_scale"].toDouble(1.0);

        // Embedded image data
        if (bg.contains("image_data")) {
            QByteArray decoded = QByteArray::fromBase64(
                bg["image_data"].toString().toLatin1());
            sketch.backgroundImage.imageData.assign(
                reinterpret_cast<const uint8_t*>(decoded.constData()),
                reinterpret_cast<const uint8_t*>(decoded.constData()) + decoded.size());
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
        p["name"] = QString::fromStdString(param.name);
        p["expression"] = QString::fromStdString(param.expression);
        p["value"] = param.value;
        p["unit"] = QString::fromStdString(param.unit);
        p["comment"] = QString::fromStdString(param.comment);
        p["is_user_param"] = param.isUserParam;
        params.append(p);
    }

    obj["parameters"] = params;
    return obj;
}

void Project::parametersFromJson(const QJsonObject& json)
{
    m_parameters.clear();
    QJsonArray params = json["parameters"].toArray();

    for (const auto& pVal : params) {
        QJsonObject p = pVal.toObject();
        ParameterData param;
        param.name = p["name"].toString().toStdString();
        param.expression = p["expression"].toString().toStdString();
        param.value = p["value"].toDouble();
        param.unit = p["unit"].toString().toStdString();
        param.comment = p["comment"].toString().toStdString();
        param.isUserParam = p["is_user_param"].toBool(true);
        m_parameters.push_back(param);
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
        f["id"] = feature.id;
        f["type"] = featureTypeToString(feature.type);
        f["name"] = QString::fromStdString(feature.name);
        if (!feature.properties.isEmpty()) {
            f["properties"] = feature.properties;
        }
        features.append(f);
    }

    obj["features"] = features;
    return obj;
}

void Project::featuresFromJson(const QJsonObject& json)
{
    m_features.clear();
    QJsonArray features = json["features"].toArray();

    for (const auto& fVal : features) {
        QJsonObject f = fVal.toObject();
        FeatureData feature;
        feature.id = f["id"].toInt();
        feature.type = featureTypeFromString(f["type"].toString());
        feature.name = f["name"].toString().toStdString();
        feature.properties = f["properties"].toObject();
        m_features.push_back(feature);
    }
}

// ---- Manifest ----

QJsonObject Project::manifestToJson() const
{
    QJsonObject obj;

    // Version info
    obj["hobbycad_version"] = QString::fromLatin1(HOBBYCAD_VERSION);
    obj["format_version"] = FORMAT_VERSION;

    // Metadata
    obj["project_name"] = QString::fromStdString(m_name);
    obj["author"] = QString::fromStdString(m_author);
    obj["description"] = QString::fromStdString(m_description);
    obj["units"] = QString::fromStdString(m_units);
    obj["created"] = QString::fromStdString(m_created);
    obj["modified"] = QString::fromStdString(m_modified_time);

    // File references
    {
        QJsonArray geoArr;
        for (const auto& f : m_geometryFiles) {
            geoArr.append(QString::fromStdString(f));
        }
        obj["geometry"] = geoArr;
    }
    {
        QJsonArray cpArr;
        for (const auto& f : m_constructionPlaneFiles) {
            cpArr.append(QString::fromStdString(f));
        }
        obj["construction_planes"] = cpArr;
    }
    {
        QJsonArray skArr;
        for (const auto& f : m_sketchFiles) {
            skArr.append(QString::fromStdString(f));
        }
        obj["sketches"] = skArr;
    }
    obj["parameters"] = QStringLiteral("features/parameters.json");
    obj["features"] = QStringLiteral("features/feature_tree.json");

    // Foreign files (non-CAD content tracked by the project)
    if (!m_foreignFiles.empty()) {
        QJsonArray foreignArr;
        for (const auto& file : m_foreignFiles) {
            if (file.description.empty() && file.category.empty()) {
                // Simple string format for minimal entries
                foreignArr.append(QString::fromStdString(file.path));
            } else {
                // Object format with metadata
                QJsonObject fileObj;
                fileObj["path"] = QString::fromStdString(file.path);
                if (!file.description.empty()) {
                    fileObj["description"] = QString::fromStdString(file.description);
                }
                if (!file.category.empty()) {
                    fileObj["category"] = QString::fromStdString(file.category);
                }
                foreignArr.append(fileObj);
            }
        }
        obj["foreign_files"] = foreignArr;
    }

    return obj;
}

bool Project::manifestFromJson(const QJsonObject& json, std::string* errorMsg)
{
    // Check format version
    int formatVersion = json["format_version"].toInt(0);
    if (formatVersion > FORMAT_VERSION) {
        if (errorMsg) {
            *errorMsg = format(
                "Project was created with a newer version of HobbyCAD (format %d, this version supports %d)",
                formatVersion, FORMAT_VERSION);
        }
        return false;
    }

    // Metadata
    m_name = json["project_name"].toString().toStdString();
    m_author = json["author"].toString().toStdString();
    m_description = json["description"].toString().toStdString();
    m_units = json["units"].toString(QStringLiteral("mm")).toStdString();
    m_created = json["created"].toString().toStdString();
    m_modified_time = json["modified"].toString().toStdString();

    // File references
    m_geometryFiles.clear();
    for (const auto& val : json["geometry"].toArray()) {
        m_geometryFiles.push_back(val.toString().toStdString());
    }

    m_constructionPlaneFiles.clear();
    for (const auto& val : json["construction_planes"].toArray()) {
        m_constructionPlaneFiles.push_back(val.toString().toStdString());
    }

    m_sketchFiles.clear();
    for (const auto& val : json["sketches"].toArray()) {
        m_sketchFiles.push_back(val.toString().toStdString());
    }

    // Foreign files
    m_foreignFiles.clear();
    for (const auto& val : json["foreign_files"].toArray()) {
        ForeignFileData file;
        if (val.isString()) {
            // Simple string format
            file.path = val.toString().toStdString();
        } else if (val.isObject()) {
            // Object format with metadata
            QJsonObject fileObj = val.toObject();
            file.path = fileObj["path"].toString().toStdString();
            file.description = fileObj["description"].toString().toStdString();
            file.category = fileObj["category"].toString().toStdString();
        }
        if (!file.path.empty()) {
            m_foreignFiles.push_back(file);
        }
    }

    return true;
}

// ---- File I/O: Save ----

bool Project::save(const std::string& path, std::string* errorMsg)
{
    std::string savePath = path.empty() ? m_projectPath : path;
    if (savePath.empty()) {
        if (errorMsg) *errorMsg = "No save path specified";
        return false;
    }

    namespace fs = std::filesystem;

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
    {
        fs::path p(savePath);
        std::string ext = p.extension().string();
        // Case-insensitive .hcad check
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".hcad") {
            if (fs::is_regular_file(p) || !fs::exists(p)) {
                // User specified the manifest file path - use parent as project dir
                savePath = p.parent_path().string();
                // Extract project name from manifest filename
                std::string baseName = p.stem().string();
                if (!baseName.empty() && m_name.empty()) {
                    m_name = baseName;
                }
            }
            // If it's an existing directory ending in .hcad, use it as-is (legacy support)
        }
    }

    // Set project name from directory if not already set
    if (m_name.empty()) {
        fs::path dirPath(savePath);
        m_name = dirPath.filename().string();
    }

    // Create directory structure
    try {
        fs::create_directories(savePath);
        fs::create_directories(savePath + "/geometry");
        fs::create_directories(savePath + "/construction");
        fs::create_directories(savePath + "/sketches");
        fs::create_directories(savePath + "/features");
        fs::create_directories(savePath + "/metadata");
    } catch (const fs::filesystem_error& e) {
        if (errorMsg) *errorMsg = std::string("Failed to create project directory: ") + e.what();
        return false;
    }

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

bool Project::saveManifest(const std::string& dir, std::string* errorMsg)
{
    // Manifest is named after the project: my_widget/my_widget.hcad
    namespace fs = std::filesystem;
    std::string dirName = fs::path(dir).filename().string();
    std::string manifestName = dirName + ".hcad";
    std::string path = dir + "/" + manifestName;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to create manifest: " + file.errorString().toStdString();
        return false;
    }

    QJsonDocument doc(manifestToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool Project::saveGeometry(const std::string& dir, std::string* errorMsg)
{
    m_geometryFiles.clear();

    for (size_t i = 0; i < m_shapes.size(); ++i) {
        std::string relPath = format("geometry/body_%03d.brep", static_cast<int>(i + 1));
        std::string fullPath = dir + "/" + relPath;

        if (!brep_io::writeBrep(fullPath, {m_shapes[i]}, errorMsg)) {
            return false;
        }
        m_geometryFiles.push_back(relPath);
    }

    return true;
}

bool Project::saveConstructionPlanes(const std::string& dir, std::string* errorMsg)
{
    m_constructionPlaneFiles.clear();

    for (size_t i = 0; i < m_constructionPlanes.size(); ++i) {
        std::string relPath = format("construction/plane_%03d.json", static_cast<int>(i + 1));
        std::string fullPath = dir + "/" + relPath;

        QFile file(QString::fromStdString(fullPath));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = "Failed to save construction plane: " + file.errorString().toStdString();
            return false;
        }

        QJsonDocument doc(constructionPlaneToJson(m_constructionPlanes[i]));
        file.write(doc.toJson(QJsonDocument::Indented));
        m_constructionPlaneFiles.push_back(relPath);
    }

    return true;
}

bool Project::saveSketches(const std::string& dir, std::string* errorMsg)
{
    m_sketchFiles.clear();

    for (size_t i = 0; i < m_sketches.size(); ++i) {
        std::string relPath = format("sketches/sketch_%03d.json", static_cast<int>(i + 1));
        std::string fullPath = dir + "/" + relPath;

        QFile file(QString::fromStdString(fullPath));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = "Failed to save sketch: " + file.errorString().toStdString();
            return false;
        }

        QJsonDocument doc(sketchToJson(m_sketches[i]));
        file.write(doc.toJson(QJsonDocument::Indented));
        m_sketchFiles.push_back(relPath);
    }

    return true;
}

bool Project::saveParameters(const std::string& dir, std::string* errorMsg)
{
    std::string path = dir + "/features/parameters.json";
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to save parameters: " + file.errorString().toStdString();
        return false;
    }

    QJsonDocument doc(parametersToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool Project::saveFeatures(const std::string& dir, std::string* errorMsg)
{
    std::string path = dir + "/features/feature_tree.json";
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to save features: " + file.errorString().toStdString();
        return false;
    }

    QJsonDocument doc(featuresToJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

// ---- File I/O: Load ----

/// Find the .hcad manifest file in a project directory.
/// Returns empty string if not found.
static std::string findManifest(const std::string& dirPath)
{
    namespace fs = std::filesystem;

    fs::path dir(dirPath);

    // Look for <dirname>.hcad first (standard naming)
    std::string standardName = dir.filename().string() + ".hcad";
    std::string standardPath = dirPath + "/" + standardName;
    if (fs::exists(standardPath)) {
        return standardPath;
    }

    // Fall back to any .hcad file in the directory
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".hcad") {
                    return entry.path().string();
                }
            }
        }
    } catch (...) {
        // Directory iteration failed
    }

    return {};
}

bool Project::load(const std::string& path, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    std::string projectDir = path;
    std::string manifestPath;

    fs::path p(path);

    if (fs::is_regular_file(p)) {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".hcad") {
            // User specified the manifest file directly
            manifestPath = path;
            projectDir = p.parent_path().string();
        }
    } else if (fs::is_directory(p)) {
        // User specified the project directory
        projectDir = path;
        manifestPath = findManifest(projectDir);
    } else {
        if (errorMsg) *errorMsg = "Path does not exist: " + path;
        return false;
    }

    if (manifestPath.empty()) {
        if (errorMsg) *errorMsg = "Not a valid HobbyCAD project (no .hcad manifest found)";
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

bool Project::loadManifestFile(const std::string& manifestPath, std::string* errorMsg)
{
    QFile file(QString::fromStdString(manifestPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to read manifest: " + file.errorString().toStdString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = "Invalid manifest JSON: " + parseError.errorString().toStdString();
        return false;
    }

    return manifestFromJson(doc.object(), errorMsg);
}

bool Project::loadGeometry(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_shapes.clear();

    for (const std::string& relPath : m_geometryFiles) {
        std::string fullPath = dir + "/" + relPath;
        if (!fs::exists(fullPath)) {
            // Skip missing files (may have been deleted)
            continue;
        }

        auto shapes = brep_io::readBrep(fullPath, errorMsg);
        if (shapes.empty() && errorMsg && !errorMsg->empty()) {
            return false;
        }
        m_shapes.insert(m_shapes.end(), shapes.begin(), shapes.end());
    }

    return true;
}

bool Project::loadConstructionPlanes(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_constructionPlanes.clear();

    for (const std::string& relPath : m_constructionPlaneFiles) {
        std::string fullPath = dir + "/" + relPath;
        if (!fs::exists(fullPath)) {
            continue;
        }

        QFile file(QString::fromStdString(fullPath));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = "Failed to read construction plane: " + file.errorString().toStdString();
            return false;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (errorMsg) *errorMsg = "Invalid construction plane JSON: " + parseError.errorString().toStdString();
            return false;
        }

        m_constructionPlanes.push_back(constructionPlaneFromJson(doc.object()));
    }

    return true;
}

bool Project::loadSketches(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_sketches.clear();

    for (const std::string& relPath : m_sketchFiles) {
        std::string fullPath = dir + "/" + relPath;
        if (!fs::exists(fullPath)) {
            continue;
        }

        QFile file(QString::fromStdString(fullPath));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = "Failed to read sketch: " + file.errorString().toStdString();
            return false;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (errorMsg) *errorMsg = "Invalid sketch JSON: " + parseError.errorString().toStdString();
            return false;
        }

        m_sketches.push_back(sketchFromJson(doc.object()));
    }

    return true;
}

bool Project::loadParameters(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    std::string path = dir + "/features/parameters.json";
    if (!fs::exists(path)) {
        // Parameters file is optional
        return true;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to read parameters: " + file.errorString().toStdString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = "Invalid parameters JSON: " + parseError.errorString().toStdString();
        return false;
    }

    parametersFromJson(doc.object());
    return true;
}

bool Project::loadFeatures(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    std::string path = dir + "/features/feature_tree.json";
    if (!fs::exists(path)) {
        // Features file is optional
        return true;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMsg) *errorMsg = "Failed to read features: " + file.errorString().toStdString();
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = "Invalid features JSON: " + parseError.errorString().toStdString();
        return false;
    }

    featuresFromJson(doc.object());
    return true;
}

#else  // !HOBBYCAD_HAS_QT — nlohmann/json + std::fstream fallback

#include <nlohmann/json.hpp>
#include "hobbycad/base64.h"

// ---- File I/O helpers ----

static nlohmann::json readJsonFile(const std::string& path, std::string* errorMsg)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        if (errorMsg) *errorMsg = "Failed to open: " + path;
        return {};
    }
    try {
        return nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& e) {
        if (errorMsg) *errorMsg = std::string("JSON parse error: ") + e.what();
        return {};
    }
}

static bool writeJsonFile(const std::string& path, const nlohmann::json& json,
                          std::string* errorMsg)
{
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        if (errorMsg) *errorMsg = "Failed to create: " + path;
        return false;
    }
    ofs << json.dump(4);
    if (!ofs.good()) {
        if (errorMsg) *errorMsg = "Failed to write: " + path;
        return false;
    }
    return true;
}

// ---- JSON Serialization: Construction Planes ----

nlohmann::json Project::constructionPlaneToJson(const ConstructionPlaneData& plane) const
{
    nlohmann::json obj;
    obj["id"] = plane.id;
    obj["name"] = plane.name;
    obj["type"] = static_cast<int>(plane.type);
    obj["base_plane"] = static_cast<int>(plane.basePlane);
    obj["base_plane_id"] = plane.basePlaneId;

    // Origin point (plane center in absolute coordinates)
    obj["origin_x"] = plane.originX;
    obj["origin_y"] = plane.originY;
    obj["origin_z"] = plane.originZ;

    obj["offset"] = plane.offset;
    obj["primary_axis"] = static_cast<int>(plane.primaryAxis);
    obj["primary_angle"] = plane.primaryAngle;
    obj["secondary_axis"] = static_cast<int>(plane.secondaryAxis);
    obj["secondary_angle"] = plane.secondaryAngle;
    obj["roll_angle"] = plane.rollAngle;
    obj["visible"] = plane.visible;
    return obj;
}

ConstructionPlaneData Project::constructionPlaneFromJson(const nlohmann::json& json) const
{
    ConstructionPlaneData plane;
    plane.id = json.value("id", 0);
    plane.name = json.value("name", std::string{});
    plane.type = static_cast<ConstructionPlaneType>(json.value("type", 0));
    plane.basePlane = static_cast<SketchPlane>(json.value("base_plane", 0));
    plane.basePlaneId = json.value("base_plane_id", -1);

    // Origin point (plane center in absolute coordinates)
    plane.originX = json.value("origin_x", 0.0);
    plane.originY = json.value("origin_y", 0.0);
    plane.originZ = json.value("origin_z", 0.0);

    plane.offset = json.value("offset", 0.0);
    plane.primaryAxis = static_cast<PlaneRotationAxis>(json.value("primary_axis", 0));
    plane.primaryAngle = json.value("primary_angle", 0.0);
    plane.secondaryAxis = static_cast<PlaneRotationAxis>(json.value("secondary_axis", 0));
    plane.secondaryAngle = json.value("secondary_angle", 0.0);
    plane.rollAngle = json.value("roll_angle", 0.0);
    plane.visible = json.value("visible", true);
    return plane;
}

// ---- JSON Serialization: Sketches ----

nlohmann::json Project::sketchToJson(const SketchData& sketch) const
{
    nlohmann::json obj;
    obj["name"] = sketch.name;
    obj["plane"] = static_cast<int>(sketch.plane);
    obj["construction_plane_id"] = sketch.constructionPlaneId;
    obj["plane_offset"] = sketch.planeOffset;
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        obj["rotation_axis"] = static_cast<int>(sketch.rotationAxis);
        obj["rotation_angle"] = sketch.rotationAngle;
    }
    obj["grid_spacing"] = sketch.gridSpacing;

    nlohmann::json entities = nlohmann::json::array();
    for (const auto& entity : sketch.entities) {
        nlohmann::json ent;
        ent["id"] = entity.id;
        ent["type"] = static_cast<int>(entity.type);

        nlohmann::json pts = nlohmann::json::array();
        for (const auto& pt : entity.points) {
            pts.push_back({pt.x, pt.y});
        }
        ent["points"] = pts;

        if (entity.type == SketchEntityType::Circle ||
            entity.type == SketchEntityType::Arc ||
            entity.type == SketchEntityType::Slot) {
            ent["radius"] = entity.radius;
        }
        if (entity.type == SketchEntityType::Arc) {
            ent["start_angle"] = entity.startAngle;
            ent["sweep_angle"] = entity.sweepAngle;
        }
        if (entity.type == SketchEntityType::Polygon) {
            ent["sides"] = entity.sides;
        }
        if (entity.type == SketchEntityType::Ellipse) {
            ent["major_radius"] = entity.majorRadius;
            ent["minor_radius"] = entity.minorRadius;
        }
        if (entity.type == SketchEntityType::Text) {
            ent["text"] = entity.text;
            if (!entity.fontFamily.empty()) {
                ent["font_family"] = entity.fontFamily;
            }
            ent["font_size"] = entity.fontSize;
            ent["font_bold"] = entity.fontBold;
            ent["font_italic"] = entity.fontItalic;
            if (!fuzzyIsNull(entity.textRotation)) {
                ent["text_rotation"] = entity.textRotation;
            }
        }
        if (entity.type == SketchEntityType::Slot && entity.arcFlipped) {
            ent["arc_flipped"] = true;
        }
        ent["constrained"] = entity.constrained;
        ent["is_construction"] = entity.isConstruction;

        entities.push_back(ent);
    }
    obj["entities"] = entities;

    // Serialize constraints
    nlohmann::json constraints = nlohmann::json::array();
    for (const auto& constraint : sketch.constraints) {
        nlohmann::json c;
        c["id"] = constraint.id;
        c["type"] = static_cast<int>(constraint.type);

        // Entity IDs
        nlohmann::json eids = nlohmann::json::array();
        for (int eid : constraint.entityIds) {
            eids.push_back(eid);
        }
        c["entity_ids"] = eids;

        // Point indices
        nlohmann::json pidxs = nlohmann::json::array();
        for (int pidx : constraint.pointIndices) {
            pidxs.push_back(pidx);
        }
        c["point_indices"] = pidxs;

        c["value"] = constraint.value;
        c["is_driving"] = constraint.isDriving;
        c["label_x"] = constraint.labelPosition.x;
        c["label_y"] = constraint.labelPosition.y;
        c["label_visible"] = constraint.labelVisible;
        c["enabled"] = constraint.enabled;

        constraints.push_back(c);
    }
    obj["constraints"] = constraints;

    // Serialize background image (only if enabled)
    if (sketch.backgroundImage.enabled) {
        nlohmann::json bg;
        bg["enabled"] = true;
        bg["storage"] = static_cast<int>(sketch.backgroundImage.storage);
        bg["file_path"] = sketch.backgroundImage.filePath;
        bg["mime_type"] = sketch.backgroundImage.mimeType;

        // Position and size
        bg["position_x"] = sketch.backgroundImage.position.x;
        bg["position_y"] = sketch.backgroundImage.position.y;
        bg["width"] = sketch.backgroundImage.width;
        bg["height"] = sketch.backgroundImage.height;
        bg["rotation"] = sketch.backgroundImage.rotation;

        // Display options
        bg["opacity"] = sketch.backgroundImage.opacity;
        bg["lock_aspect_ratio"] = sketch.backgroundImage.lockAspectRatio;
        bg["grayscale"] = sketch.backgroundImage.grayscale;
        bg["contrast"] = sketch.backgroundImage.contrast;
        bg["brightness"] = sketch.backgroundImage.brightness;

        // Calibration
        bg["calibrated"] = sketch.backgroundImage.calibrated;
        bg["calibration_scale"] = sketch.backgroundImage.calibrationScale;

        // Embed image data if storage is Embedded
        if (sketch.backgroundImage.storage == sketch::BackgroundStorage::Embedded &&
            !sketch.backgroundImage.imageData.empty()) {
            bg["image_data"] = hobbycad::base64Encode(sketch.backgroundImage.imageData);
        }

        obj["background_image"] = bg;
    }

    return obj;
}

SketchData Project::sketchFromJson(const nlohmann::json& json) const
{
    SketchData sketch;
    sketch.name = json.value("name", std::string{});
    sketch.plane = static_cast<SketchPlane>(json.value("plane", 0));
    sketch.constructionPlaneId = json.value("construction_plane_id", -1);
    sketch.planeOffset = json.value("plane_offset", 0.0);
    // Inline plane parameters (when not referencing a construction plane)
    if (sketch.constructionPlaneId < 0 && sketch.plane == SketchPlane::Custom) {
        sketch.rotationAxis = static_cast<PlaneRotationAxis>(
            json.value("rotation_axis", 0));
        sketch.rotationAngle = json.value("rotation_angle", 0.0);
    }
    sketch.gridSpacing = json.value("grid_spacing", 10.0);

    if (json.contains("entities")) {
        for (const auto& ent : json["entities"]) {
            SketchEntityData entity;
            entity.id = ent.value("id", 0);
            entity.type = static_cast<SketchEntityType>(ent.value("type", 0));

            if (ent.contains("points")) {
                for (const auto& ptArr : ent["points"]) {
                    if (ptArr.is_array() && ptArr.size() >= 2) {
                        entity.points.push_back(Point2D(
                            ptArr[0].get<double>(), ptArr[1].get<double>()));
                    }
                }
            }

            entity.radius = ent.value("radius", 0.0);
            entity.startAngle = ent.value("start_angle", 0.0);
            entity.sweepAngle = ent.value("sweep_angle", 0.0);
            entity.sides = ent.value("sides", 6);
            entity.majorRadius = ent.value("major_radius", 0.0);
            entity.minorRadius = ent.value("minor_radius", 0.0);
            entity.text = ent.value("text", std::string{});
            entity.fontFamily = ent.value("font_family", std::string{});
            entity.fontSize = ent.value("font_size", 12.0);
            entity.fontBold = ent.value("font_bold", false);
            entity.fontItalic = ent.value("font_italic", false);
            entity.textRotation = ent.value("text_rotation", 0.0);
            entity.arcFlipped = ent.value("arc_flipped", false);
            entity.constrained = ent.value("constrained", false);
            entity.isConstruction = ent.value("is_construction", false);

            sketch.entities.push_back(entity);
        }
    }

    // Deserialize constraints
    if (json.contains("constraints")) {
        for (const auto& c : json["constraints"]) {
            ConstraintData constraint;

            constraint.id = c.value("id", 0);
            constraint.type = static_cast<ConstraintType>(c.value("type", 0));

            // Entity IDs
            if (c.contains("entity_ids")) {
                for (const auto& eid : c["entity_ids"]) {
                    constraint.entityIds.push_back(eid.get<int>());
                }
            }

            // Point indices
            if (c.contains("point_indices")) {
                for (const auto& pidx : c["point_indices"]) {
                    constraint.pointIndices.push_back(pidx.get<int>());
                }
            }

            constraint.value = c.value("value", 0.0);
            constraint.isDriving = c.value("is_driving", true);
            constraint.labelPosition = Point2D(
                c.value("label_x", 0.0),
                c.value("label_y", 0.0)
            );
            constraint.labelVisible = c.value("label_visible", true);
            constraint.enabled = c.value("enabled", true);

            sketch.constraints.push_back(constraint);
        }
    }

    // Deserialize background image
    if (json.contains("background_image")) {
        const auto& bg = json["background_image"];

        sketch.backgroundImage.enabled = bg.value("enabled", false);
        sketch.backgroundImage.storage = static_cast<sketch::BackgroundStorage>(
            bg.value("storage", 0));
        sketch.backgroundImage.filePath = bg.value("file_path", std::string{});
        sketch.backgroundImage.mimeType = bg.value("mime_type", std::string{});

        // Position and size
        sketch.backgroundImage.position = Point2D(
            bg.value("position_x", 0.0),
            bg.value("position_y", 0.0));
        sketch.backgroundImage.width = bg.value("width", 100.0);
        sketch.backgroundImage.height = bg.value("height", 100.0);
        sketch.backgroundImage.rotation = bg.value("rotation", 0.0);

        // Display options
        sketch.backgroundImage.opacity = bg.value("opacity", 0.5);
        sketch.backgroundImage.lockAspectRatio = bg.value("lock_aspect_ratio", true);
        sketch.backgroundImage.grayscale = bg.value("grayscale", false);
        sketch.backgroundImage.contrast = bg.value("contrast", 1.0);
        sketch.backgroundImage.brightness = bg.value("brightness", 0.0);

        // Calibration
        sketch.backgroundImage.calibrated = bg.value("calibrated", false);
        sketch.backgroundImage.calibrationScale = bg.value("calibration_scale", 1.0);

        // Embedded image data
        if (bg.contains("image_data")) {
            std::string encoded = bg["image_data"].get<std::string>();
            sketch.backgroundImage.imageData = hobbycad::base64Decode(encoded);
        }
    }

    return sketch;
}

// ---- JSON Serialization: Parameters ----

nlohmann::json Project::parametersToJson() const
{
    nlohmann::json obj;
    nlohmann::json params = nlohmann::json::array();

    for (const auto& param : m_parameters) {
        nlohmann::json p;
        p["name"] = param.name;
        p["expression"] = param.expression;
        p["value"] = param.value;
        p["unit"] = param.unit;
        p["comment"] = param.comment;
        p["is_user_param"] = param.isUserParam;
        params.push_back(p);
    }

    obj["parameters"] = params;
    return obj;
}

void Project::parametersFromJson(const nlohmann::json& json)
{
    m_parameters.clear();
    if (!json.contains("parameters")) return;

    for (const auto& p : json["parameters"]) {
        ParameterData param;
        param.name = p.value("name", std::string{});
        param.expression = p.value("expression", std::string{});
        param.value = p.value("value", 0.0);
        param.unit = p.value("unit", std::string{});
        param.comment = p.value("comment", std::string{});
        param.isUserParam = p.value("is_user_param", true);
        m_parameters.push_back(param);
    }
}

// ---- JSON Serialization: Features ----

static std::string featureTypeToString(FeatureType type)
{
    switch (type) {
    case FeatureType::Origin:    return "Origin";
    case FeatureType::Sketch:    return "Sketch";
    case FeatureType::Extrude:   return "Extrude";
    case FeatureType::Revolve:   return "Revolve";
    case FeatureType::Fillet:    return "Fillet";
    case FeatureType::Chamfer:   return "Chamfer";
    case FeatureType::Hole:      return "Hole";
    case FeatureType::Mirror:    return "Mirror";
    case FeatureType::Pattern:   return "Pattern";
    case FeatureType::Box:       return "Box";
    case FeatureType::Cylinder:  return "Cylinder";
    case FeatureType::Sphere:    return "Sphere";
    case FeatureType::Move:      return "Move";
    case FeatureType::Join:      return "Join";
    case FeatureType::Cut:       return "Cut";
    case FeatureType::Intersect: return "Intersect";
    }
    return "Unknown";
}

static FeatureType featureTypeFromString(const std::string& str)
{
    if (str == "Origin")    return FeatureType::Origin;
    if (str == "Sketch")    return FeatureType::Sketch;
    if (str == "Extrude")   return FeatureType::Extrude;
    if (str == "Revolve")   return FeatureType::Revolve;
    if (str == "Fillet")    return FeatureType::Fillet;
    if (str == "Chamfer")   return FeatureType::Chamfer;
    if (str == "Hole")      return FeatureType::Hole;
    if (str == "Mirror")    return FeatureType::Mirror;
    if (str == "Pattern")   return FeatureType::Pattern;
    if (str == "Box")       return FeatureType::Box;
    if (str == "Cylinder")  return FeatureType::Cylinder;
    if (str == "Sphere")    return FeatureType::Sphere;
    if (str == "Move")      return FeatureType::Move;
    if (str == "Join")      return FeatureType::Join;
    if (str == "Cut")       return FeatureType::Cut;
    if (str == "Intersect") return FeatureType::Intersect;
    return FeatureType::Origin;
}

nlohmann::json Project::featuresToJson() const
{
    nlohmann::json obj;
    nlohmann::json features = nlohmann::json::array();

    for (const auto& feature : m_features) {
        nlohmann::json f;
        f["id"] = feature.id;
        f["type"] = featureTypeToString(feature.type);
        f["name"] = feature.name;
        if (!feature.properties.empty()) {
            f["properties"] = feature.properties;
        }
        features.push_back(f);
    }

    obj["features"] = features;
    return obj;
}

void Project::featuresFromJson(const nlohmann::json& json)
{
    m_features.clear();
    if (!json.contains("features")) return;

    for (const auto& f : json["features"]) {
        FeatureData feature;
        feature.id = f.value("id", 0);
        feature.type = featureTypeFromString(f.value("type", std::string{"Origin"}));
        feature.name = f.value("name", std::string{});
        if (f.contains("properties")) {
            feature.properties = f["properties"];
        }
        m_features.push_back(feature);
    }
}

// ---- Manifest ----

nlohmann::json Project::manifestToJson() const
{
    nlohmann::json obj;

    // Version info
    obj["hobbycad_version"] = std::string(HOBBYCAD_VERSION);
    obj["format_version"] = FORMAT_VERSION;

    // Metadata
    obj["project_name"] = m_name;
    obj["author"] = m_author;
    obj["description"] = m_description;
    obj["units"] = m_units;
    obj["created"] = m_created;
    obj["modified"] = m_modified_time;

    // File references
    {
        nlohmann::json geoArr = nlohmann::json::array();
        for (const auto& f : m_geometryFiles) {
            geoArr.push_back(f);
        }
        obj["geometry"] = geoArr;
    }
    {
        nlohmann::json cpArr = nlohmann::json::array();
        for (const auto& f : m_constructionPlaneFiles) {
            cpArr.push_back(f);
        }
        obj["construction_planes"] = cpArr;
    }
    {
        nlohmann::json skArr = nlohmann::json::array();
        for (const auto& f : m_sketchFiles) {
            skArr.push_back(f);
        }
        obj["sketches"] = skArr;
    }
    obj["parameters"] = "features/parameters.json";
    obj["features"] = "features/feature_tree.json";

    // Foreign files (non-CAD content tracked by the project)
    if (!m_foreignFiles.empty()) {
        nlohmann::json foreignArr = nlohmann::json::array();
        for (const auto& file : m_foreignFiles) {
            if (file.description.empty() && file.category.empty()) {
                // Simple string format for minimal entries
                foreignArr.push_back(file.path);
            } else {
                // Object format with metadata
                nlohmann::json fileObj;
                fileObj["path"] = file.path;
                if (!file.description.empty()) {
                    fileObj["description"] = file.description;
                }
                if (!file.category.empty()) {
                    fileObj["category"] = file.category;
                }
                foreignArr.push_back(fileObj);
            }
        }
        obj["foreign_files"] = foreignArr;
    }

    return obj;
}

bool Project::manifestFromJson(const nlohmann::json& json, std::string* errorMsg)
{
    // Check format version
    int formatVersion = json.value("format_version", 0);
    if (formatVersion > FORMAT_VERSION) {
        if (errorMsg) {
            *errorMsg = format(
                "Project was created with a newer version of HobbyCAD (format %d, this version supports %d)",
                formatVersion, FORMAT_VERSION);
        }
        return false;
    }

    // Metadata
    m_name = json.value("project_name", std::string{});
    m_author = json.value("author", std::string{});
    m_description = json.value("description", std::string{});
    m_units = json.value("units", std::string{"mm"});
    m_created = json.value("created", std::string{});
    m_modified_time = json.value("modified", std::string{});

    // File references
    m_geometryFiles.clear();
    if (json.contains("geometry")) {
        for (const auto& val : json["geometry"]) {
            m_geometryFiles.push_back(val.get<std::string>());
        }
    }

    m_constructionPlaneFiles.clear();
    if (json.contains("construction_planes")) {
        for (const auto& val : json["construction_planes"]) {
            m_constructionPlaneFiles.push_back(val.get<std::string>());
        }
    }

    m_sketchFiles.clear();
    if (json.contains("sketches")) {
        for (const auto& val : json["sketches"]) {
            m_sketchFiles.push_back(val.get<std::string>());
        }
    }

    // Foreign files
    m_foreignFiles.clear();
    if (json.contains("foreign_files")) {
        for (const auto& val : json["foreign_files"]) {
            ForeignFileData file;
            if (val.is_string()) {
                // Simple string format
                file.path = val.get<std::string>();
            } else if (val.is_object()) {
                // Object format with metadata
                file.path = val.value("path", std::string{});
                file.description = val.value("description", std::string{});
                file.category = val.value("category", std::string{});
            }
            if (!file.path.empty()) {
                m_foreignFiles.push_back(file);
            }
        }
    }

    return true;
}

// ---- File I/O: Save ----

bool Project::saveManifest(const std::string& dir, std::string* errorMsg)
{
    // Manifest is named after the project: my_widget/my_widget.hcad
    namespace fs = std::filesystem;
    std::string dirName = fs::path(dir).filename().string();
    std::string manifestName = dirName + ".hcad";
    std::string path = (fs::path(dir) / manifestName).string();

    return writeJsonFile(path, manifestToJson(), errorMsg);
}

bool Project::saveConstructionPlanes(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;
    m_constructionPlaneFiles.clear();

    for (size_t i = 0; i < m_constructionPlanes.size(); ++i) {
        std::string relPath = format("construction/plane_%03d.json", static_cast<int>(i + 1));
        std::string fullPath = (fs::path(dir) / relPath).string();

        if (!writeJsonFile(fullPath, constructionPlaneToJson(m_constructionPlanes[i]), errorMsg))
            return false;
        m_constructionPlaneFiles.push_back(relPath);
    }

    return true;
}

bool Project::saveSketches(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;
    m_sketchFiles.clear();

    for (size_t i = 0; i < m_sketches.size(); ++i) {
        std::string relPath = format("sketches/sketch_%03d.json", static_cast<int>(i + 1));
        std::string fullPath = (fs::path(dir) / relPath).string();

        if (!writeJsonFile(fullPath, sketchToJson(m_sketches[i]), errorMsg))
            return false;
        m_sketchFiles.push_back(relPath);
    }

    return true;
}

bool Project::saveParameters(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;
    std::string path = (fs::path(dir) / "features" / "parameters.json").string();
    return writeJsonFile(path, parametersToJson(), errorMsg);
}

bool Project::saveFeatures(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;
    std::string path = (fs::path(dir) / "features" / "feature_tree.json").string();
    return writeJsonFile(path, featuresToJson(), errorMsg);
}

// ---- File I/O: Load ----

bool Project::loadManifestFile(const std::string& manifestPath, std::string* errorMsg)
{
    auto json = readJsonFile(manifestPath, errorMsg);
    if (json.is_null()) return false;
    return manifestFromJson(json, errorMsg);
}

bool Project::loadConstructionPlanes(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_constructionPlanes.clear();

    for (const std::string& relPath : m_constructionPlaneFiles) {
        std::string fullPath = (fs::path(dir) / relPath).string();
        if (!fs::exists(fullPath)) {
            continue;
        }

        auto json = readJsonFile(fullPath, errorMsg);
        if (json.is_null()) return false;

        m_constructionPlanes.push_back(constructionPlaneFromJson(json));
    }

    return true;
}

bool Project::loadSketches(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_sketches.clear();

    for (const std::string& relPath : m_sketchFiles) {
        std::string fullPath = (fs::path(dir) / relPath).string();
        if (!fs::exists(fullPath)) {
            continue;
        }

        auto json = readJsonFile(fullPath, errorMsg);
        if (json.is_null()) return false;

        m_sketches.push_back(sketchFromJson(json));
    }

    return true;
}

bool Project::loadParameters(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    std::string path = (fs::path(dir) / "features" / "parameters.json").string();
    if (!fs::exists(path)) {
        // Parameters file is optional
        return true;
    }

    auto json = readJsonFile(path, errorMsg);
    if (json.is_null()) return false;

    parametersFromJson(json);
    return true;
}

bool Project::loadFeatures(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    std::string path = (fs::path(dir) / "features" / "feature_tree.json").string();
    if (!fs::exists(path)) {
        // Features file is optional
        return true;
    }

    auto json = readJsonFile(path, errorMsg);
    if (json.is_null()) return false;

    featuresFromJson(json);
    return true;
}

#endif  // HOBBYCAD_HAS_QT

}  // namespace hobbycad
