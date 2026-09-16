// =====================================================================
//  src/libhobbycad/project.cpp — HobbyCAD project container
// =====================================================================

#include <algorithm>
#include "hobbycad/project.h"
#include "hobbycad/parameters.h"
#include "hobbycad/sketch/parsing.h"

#include <cmath>
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
    // Timestamps left empty; no Qt dependency for time
    // The save code will populate them if needed
}

Project::~Project() = default;

// ---- Modification tracking ----

void Project::setModified(bool modified)
{
    m_modified_flag = modified;
}

// ---- Geometry ----

int Project::nextBodyId() const
{
    int next = 1;
    for (const BodyData& b : m_bodies) {
        if (b.id >= next) next = b.id + 1;
    }
    return next;
}

int Project::addBody(const TopoDS_Shape& shape, const std::string& name)
{
    // A project always contains at least one design.
    if (m_designs.empty()) {
        addDesign({});
    }

    BodyData body;
    body.id = nextBodyId();
    body.name = name.empty() ? format("Body%d", body.id) : name;
    body.designId = m_activeDesignId;
    body.shape = shape;
    m_bodies.push_back(body);
    setModified(true);
    return body.id;
}

void Project::addShape(const TopoDS_Shape& shape)
{
    addBody(shape);
}

void Project::setBodies(const std::vector<BodyData>& bodies)
{
    m_bodies = bodies;

    // Fill in anything the caller left unset, so a body can never reach the
    // save path without an id to name its file.
    for (BodyData& b : m_bodies) {
        if (b.id < 0) {
            b.id = nextBodyId();
        }
        if (b.name.empty()) {
            b.name = format("Body%d", b.id);
        }
        if (b.designId <= 0) {
            b.designId = m_activeDesignId;
        }
    }
    setModified(true);
}

void Project::clearShapes()
{
    m_bodies.clear();
    setModified(true);
}

// ---- Construction Planes ----

int Project::addConstructionPlane(const ConstructionPlaneData& plane)
{
    // A project always contains at least one design.
    if (m_designs.empty()) {
        addDesign({});
    }

    m_constructionPlanes.push_back(plane);
    ConstructionPlaneData& added = m_constructionPlanes.back();

    // A plane's id is its identity: sketches reference it by id
    // (SketchData::constructionPlaneId) and it names the file the plane is
    // saved to. Assign one here so a caller that does not track ids cannot
    // produce two planes claiming the same file.
    if (added.id <= 0) {
        added.id = nextConstructionPlaneId();
    }
    if (added.designId <= 0) {
        added.designId = m_activeDesignId;
    }
    setModified(true);
    return added.id;
}

void Project::setConstructionPlane(int index, const ConstructionPlaneData& plane)
{
    if (index >= 0 && index < static_cast<int>(m_constructionPlanes.size())) {
        m_constructionPlanes[index] = plane;
        setModified(true);
    }
}

void Project::setConstructionPlanes(
    const std::vector<ConstructionPlaneData>& planes)
{
    m_constructionPlanes = planes;
    setModified(true);
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

bool Project::planeDependsOn(const std::vector<ConstructionPlaneData>& planes,
                             int planeId, int candidateRefId)
{
    if (candidateRefId < 0) return false;          // absolute / origin plane: no chain
    if (candidateRefId == planeId) return true;    // a plane relative to itself
    std::vector<int> visited;
    int cur = candidateRefId;
    while (cur >= 0) {
        if (cur == planeId) return true;
        if (std::find(visited.begin(), visited.end(), cur) != visited.end()) return false;  // pre-existing loop elsewhere; not ours
        visited.push_back(cur);
        const ConstructionPlaneData* q = nullptr;
        for (const auto& c : planes) if (c.id == cur) { q = &c; break; }
        if (!q) return false;
        // Follow both kinds of reference; check the second branch recursively.
        const int viaBase = (q->type == ConstructionPlaneType::OffsetFromPlane) ? q->basePlaneId : -1;
        const int viaCenter = q->centerRelative ? q->centerRefPlaneId : -1;
        if (viaBase >= 0 && viaCenter >= 0 && viaBase != viaCenter) {
            if (planeDependsOn(planes, planeId, viaCenter)) return true;
            cur = viaBase;
        } else {
            cur = viaBase >= 0 ? viaBase : viaCenter;
        }
    }
    return false;
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

int Project::addDesign(const std::string& name)
{
    int id = 1;
    for (const DesignData& d : m_designs) {
        if (d.id >= id) id = d.id + 1;
    }

    DesignData design;
    design.id = id;
    design.name = name.empty() ? format("Design%d", id) : name;

    // The FIRST design keeps an empty prefix, so a project with one design
    // writes sketches/sketch_7.json rather than designs/1/sketches/... .
    // Only a second design introduces the designs/ level, and it does so
    // without disturbing the first. That keeps the common case flat and
    // readable on disk, which matters because the whole project is text
    // under version control.
    design.pathPrefix = m_designs.empty() ? std::string()
                                          : format("designs/%d/", id);
    m_designs.push_back(design);
    setModified(true);
    return id;
}

std::string Project::designPathPrefix(int designId) const
{
    for (const DesignData& d : m_designs) {
        if (d.id == designId) return d.pathPrefix;
    }
    return {};
}

int Project::nextSketchFeatureId() const
{
    int next = 1;
    for (const SketchData& s : m_sketches) {
        if (s.id >= next) {
            next = s.id + 1;
        }
    }
    return next;
}

PlaneBasis planeBasisFor(SketchPlane plane, double offset)
{
    const float o = static_cast<float>(offset);
    switch (plane) {
    case SketchPlane::XZ: return { {0, -o, 0}, {1,0,0}, {0,0,1}, {0,-1,0} };  // right-handed: n = u x v = X x Z = -Y
    case SketchPlane::YZ: return { {o, 0, 0}, {0,1,0}, {0,0,1}, {1,0,0} };
    case SketchPlane::XY:
    default:              return { {0, 0, o}, {1,0,0}, {0,1,0}, {0,0,1} };
    }
}

PlaneAxisLabels planeAxisLabels(SketchPlane plane)
{
    switch (plane) {
    case SketchPlane::XZ:     return { "X", "Z", "Y" };
    case SketchPlane::YZ:     return { "Y", "Z", "X" };
    case SketchPlane::Custom: return { "U", "V", "N" };
    case SketchPlane::XY:
    default:                  return { "X", "Y", "Z" };
    }
}

void Project::addSketch(const SketchData& sketch)
{
    // A project always contains at least one design; create it lazily so a
    // freshly constructed Project needs no special handling.
    if (m_designs.empty()) {
        const_cast<Project*>(this)->addDesign({});
    }

    m_sketches.push_back(sketch);
    if (m_sketches.back().designId <= 0) {
        m_sketches.back().designId = m_activeDesignId;
    }

    // A sketch's id is its identity: it names the file the sketch is saved
    // to. Assign one here so callers that do not track ids (the CLI, tests)
    // still produce a well-formed project rather than a directory of sketches
    // all claiming the same name.
    if (m_sketches.back().id < 0) {
        m_sketches.back().id = nextSketchFeatureId();
    }
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

void Project::setSketches(const std::vector<SketchData>& sketches)
{
    m_sketches = sketches;
    setModified(true);
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

bool Project::addParameter(const ParameterData& param)
{
    if (m_designs.empty()) {
        addDesign({});
    }

    const int design = param.designId > 0 ? param.designId : m_activeDesignId;

    // A duplicate name is not a harmless collision: expressions resolve by
    // name, so the second one would shadow the first with no diagnostic.
    if (parameterByName(param.name, design) != nullptr) {
        return false;
    }

    m_parameters.push_back(param);
    m_parameters.back().designId = design;
    setModified(true);
    return true;
}

bool Project::addNamedPoint(const NamedPointData& np)
{
    if (m_designs.empty()) {
        addDesign({});
    }
    const int design = np.designId != 0 ? np.designId : m_activeDesignId;
    for (auto& existing : m_namedPoints) {
        if (existing.name == np.name && existing.designId == design) {
            existing.xExpr = np.xExpr;
            existing.yExpr = np.yExpr;
            existing.zExpr = np.zExpr;
            return true;
        }
    }
    NamedPointData added = np;
    added.designId = design;
    m_namedPoints.push_back(added);
    return true;
}

std::map<std::string, std::array<double, 3>> evaluateNamedPoints(
    const std::vector<NamedPointData>& points,
    const std::map<std::string, double>& params)
{
    std::map<std::string, std::array<double, 3>> out;
    out["origin"] = {0.0, 0.0, 0.0};   // built-in, always available
    for (const NamedPointData& np : points) {
        double x = 0, y = 0, z = 0;
        evaluateExpression(np.xExpr, x, params);
        evaluateExpression(np.yExpr, y, params);
        if (!np.zExpr.empty()) evaluateExpression(np.zExpr, z, params);
        out[np.name] = {x, y, z};
    }
    return out;
}

NamedPointProblem defineNamedPoint(const std::string& name,
                                   const std::string& coordText,
                                   const std::map<std::string, double>& params,
                                   NamedPointData& out,
                                   std::array<double, 3>* value)
{
    if (equalsIgnoreCase(name, "origin")) return NamedPointProblem::ReservedName;
    const std::vector<std::string> parts = sketch::splitCoordinate(coordText);
    if (parts.size() < 2 || parts.size() > 3) return NamedPointProblem::BadComponentCount;
    NamedPointData np;
    np.name = name;
    np.xExpr = parts[0];
    np.yExpr = parts[1];
    np.zExpr = parts.size() == 3 ? parts[2] : std::string("0");
    double vx = 0, vy = 0, vz = 0;
    if (!evaluateExpression(np.xExpr, vx, params)
        || !evaluateExpression(np.yExpr, vy, params)
        || !evaluateExpression(np.zExpr, vz, params)) {
        return NamedPointProblem::DoesNotEvaluate;
    }
    out = std::move(np);
    if (value) *value = {vx, vy, vz};
    return NamedPointProblem::None;
}

// ---- Named document objects -------------------------------------------

bool parseObjectKind(const std::string& word, ObjectKind& out)
{
    if (equalsIgnoreCase(word, "sketch")) { out = ObjectKind::Sketch; return true; }
    if (equalsIgnoreCase(word, "body"))   { out = ObjectKind::Body;   return true; }
    if (equalsIgnoreCase(word, "plane"))  { out = ObjectKind::Plane;  return true; }
    return false;
}

const char* objectKindName(ObjectKind kind)
{
    switch (kind) {
    case ObjectKind::Sketch: return "sketch";
    case ObjectKind::Body:   return "body";
    case ObjectKind::Plane:  return "plane";
    }
    return "object";
}

namespace {

/// (name, id) of every object of a kind, in list order.
std::vector<std::pair<std::string, int>> objectsOfKind(const Project& project, ObjectKind kind)
{
    std::vector<std::pair<std::string, int>> out;
    switch (kind) {
    case ObjectKind::Sketch:
        for (const auto& x : project.sketches()) out.emplace_back(x.name, x.id);
        break;
    case ObjectKind::Body:
        for (const auto& x : project.bodies()) out.emplace_back(x.name, x.id);
        break;
    case ObjectKind::Plane:
        for (const auto& x : project.constructionPlanes()) out.emplace_back(x.name, x.id);
        break;
    }
    return out;
}

bool wholeInt(const std::string& s, int& out)
{
    if (s.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace

int objectCount(const Project& project, ObjectKind kind)
{
    return static_cast<int>(objectsOfKind(project, kind).size());
}

std::string objectNameAt(const Project& project, ObjectKind kind, int index)
{
    const auto objs = objectsOfKind(project, kind);
    if (index < 0 || index >= static_cast<int>(objs.size())) return {};
    return objs[static_cast<size_t>(index)].first;
}

int objectIdAt(const Project& project, ObjectKind kind, int index)
{
    const auto objs = objectsOfKind(project, kind);
    if (index < 0 || index >= static_cast<int>(objs.size())) return -1;
    return objs[static_cast<size_t>(index)].second;
}

bool objectNameTaken(const Project& project, ObjectKind kind, const std::string& name, int skipIndex)
{
    const auto objs = objectsOfKind(project, kind);
    for (size_t i = 0; i < objs.size(); ++i) {
        if (static_cast<int>(i) != skipIndex && objs[i].first == name) return true;
    }
    return false;
}

ObjectRef resolveObjectRef(const Project& project, ObjectKind kind, const std::string& token)
{
    ObjectRef ref;
    const auto objs = objectsOfKind(project, kind);
    if (objs.empty()) { ref.problem = ObjectRefProblem::NoneOfKind; return ref; }

    auto indexOfId = [&objs](int id) {
        for (size_t i = 0; i < objs.size(); ++i) if (objs[i].second == id) return static_cast<int>(i);
        return -1;
    };

    // Explicit "id=N": unambiguous by construction.
    if (token.size() >= 3 && equalsIgnoreCase(token.substr(0, 3), "id=")) {
        int wanted = 0;
        if (!wholeInt(token.substr(3), wanted)) { ref.problem = ObjectRefProblem::BadIdNumber; return ref; }
        ref.id = wanted;
        ref.index = indexOfId(wanted);
        if (ref.index < 0) ref.problem = ObjectRefProblem::NoSuchId;
        return ref;
    }

    int byName = -1;
    for (size_t i = 0; i < objs.size(); ++i) if (objs[i].first == token) { byName = static_cast<int>(i); break; }
    int asId = 0;
    const int byId = wholeInt(token, asId) ? indexOfId(asId) : -1;

    if (byName >= 0 && byId >= 0 && byName != byId) {
        ref.problem = ObjectRefProblem::Ambiguous;
        ref.id = asId;
        return ref;
    }
    if (byName >= 0) { ref.index = byName; ref.id = objs[static_cast<size_t>(byName)].second; return ref; }
    if (byId >= 0)   { ref.index = byId;   ref.id = asId; return ref; }
    ref.problem = ObjectRefProblem::NoSuchName;
    return ref;
}

// ---- Parameter bridge ---------------------------------------------------

namespace {

Parameter toEngineParameter(const ParameterData& pd)
{
    Parameter pp;
    pp.name = pd.name; pp.expression = pd.expression; pp.value = pd.value;
    pp.unit = pd.unit; pp.comment = pd.comment; pp.isUserParam = pd.isUserParam;
    pp.isReference = pd.isReference; pp.referenceSource = pd.referenceSource;
    return pp;
}

void fillFromEngine(ParameterData& pd, const Parameter& ep)
{
    pd.name = ep.name; pd.expression = ep.expression; pd.value = ep.value;
    pd.unit = ep.unit; pd.comment = ep.comment; pd.isUserParam = ep.isUserParam;
    pd.isReference = ep.isReference; pd.referenceSource = ep.referenceSource;
}

}  // namespace

ParameterEngine parameterEngineFrom(const std::vector<ParameterData>& params)
{
    ParameterEngine engine;
    std::vector<Parameter> cur;
    cur.reserve(params.size());
    for (const ParameterData& pd : params) cur.push_back(toEngineParameter(pd));
    engine.setParameters(cur);
    return engine;
}

std::vector<ParameterData> parametersFromEngine(const ParameterEngine& engine,
                                                const std::vector<ParameterData>& before,
                                                int designIdForNew)
{
    std::vector<ParameterData> after;
    after.reserve(before.size() + 1);
    for (const ParameterData& b : before) {
        const Parameter* ep = engine.parameter(b.name);
        if (!ep) continue;                 // the engine dropped it
        ParameterData pd = b;              // keeps designId and list order
        fillFromEngine(pd, *ep);
        after.push_back(pd);
    }
    for (const Parameter& ep : engine.parameters()) {
        bool known = false;
        for (const ParameterData& b : before) if (b.name == ep.name) { known = true; break; }
        if (known) continue;
        ParameterData pd;
        pd.designId = designIdForNew;
        fillFromEngine(pd, ep);
        after.push_back(pd);
    }
    return after;
}

const NamedPointData* Project::namedPointByName(const std::string& name, int design) const
{
    const int d = design != 0 ? design : m_activeDesignId;
    for (const auto& np : m_namedPoints)
        if (np.name == name && np.designId == d) return &np;
    return nullptr;
}

const ParameterData* Project::parameterByName(const std::string& name,
                                              int designId) const
{
    for (const ParameterData& p : m_parameters) {
        if (p.name == name && p.designId == designId) {
            return &p;
        }
    }
    return nullptr;
}

bool Project::renameParameter(const std::string& oldName,
                              const std::string& newName,
                              int designId)
{
    if (oldName == newName) {
        return true;
    }
    if (parameterByName(oldName, designId) == nullptr) {
        return false;
    }
    if (parameterByName(newName, designId) != nullptr) {
        return false;      // would shadow an existing parameter
    }

    for (ParameterData& p : m_parameters) {
        if (p.designId != designId) {
            continue;      // another design's "width" is not this one
        }
        if (p.name == oldName) {
            p.name = newName;
        }
        // Expressions in the SAME design may reference it, including the
        // renamed parameter's own expression.
        p.expression = renameIdentifierInExpression(p.expression, oldName, newName);
    }
    setModified(true);
    return true;
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

    m_bodies.clear();
    m_constructionPlanes.clear();
    m_sketches.clear();
    m_parameters.clear();
    m_features.clear();
    m_foreignFiles.clear();
    m_geometryFiles.clear();
    m_constructionPlaneFiles.clear();
    m_sketchFiles.clear();
}

std::string projectDirForSavePath(const std::string& path)
{
    namespace fs = std::filesystem;

    // A trailing separator names the directory itself.
    fs::path p(path);
    while (p.filename().empty() && p.has_parent_path() && p != p.root_path()) {
        p = p.parent_path();
    }

    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".hcad") {
        return p.string();
    }

    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        return p.string();                  // a legacy directory named *.hcad
    }
    if (fs::is_regular_file(p, ec)) {
        return p.parent_path().string();    // an existing manifest: its own project
    }
    // A new manifest name. Taking its parent as the project, as this once did,
    // wrote the project loose into whatever folder the name was typed in.
    const fs::path parent = p.parent_path();
    if (parent.filename() == p.stem()) {
        return parent.string();
    }
    return (parent / p.stem()).string();
}

namespace {

/// Path a sketch is saved to, derived from its feature id.
///
/// The id is the sketch's stable identity, so the filename survives deletion
/// of an earlier sketch. Format version 1 numbered these positionally, which
/// meant removing sketch 1 shifted every later sketch's contents into a
/// different file: unreadable diffs under version control, and wrong
/// per-file decoration in the feature tree.
std::string sketchRelPath(const hobbycad::SketchData& sketch, size_t index,
                          const std::string& designPrefix)
{
    // addSketch() assigns an id, but m_sketches can also be filled by
    // setSketch()/load(); fall back to the position rather than writing
    // every id-less sketch to the same file.
    const int id = sketch.id >= 0 ? sketch.id : static_cast<int>(index + 1);

    // The first design's prefix is empty, so a one-design project keeps a
    // flat sketches/ directory. A second design prefixes "designs/2/" and
    // leaves the first design's files untouched.
    return designPrefix + hobbycad::format("sketches/sketch_%d.json", id);
}

}  // namespace

// =====================================================================
//  File I/O — the same code with or without Qt
// =====================================================================
//  Directory layout, manifest lookup and BREP geometry files need no
//  Qt: keeping them out of the #if is what lets the library be built
//  without Qt and still open and save a project.

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
    // A directory, a new "name.hcad" or an existing manifest: one rule,
    // shared with the front ends' Save As.
    savePath = projectDirForSavePath(savePath);

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

        // A design with a path prefix needs its own subtree. The first
        // design's prefix is empty, so this adds nothing for the common
        // single-design case.
        for (const DesignData& d : m_designs) {
            if (!d.pathPrefix.empty()) {
                fs::create_directories(savePath + "/" + d.pathPrefix + "sketches");
                fs::create_directories(savePath + "/" + d.pathPrefix + "geometry");
                fs::create_directories(savePath + "/" + d.pathPrefix + "construction");
            }
        }
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

bool Project::saveGeometry(const std::string& dir, std::string* errorMsg)
{
    m_geometryFiles.clear();

    for (const BodyData& body : m_bodies) {
        // Named by id, like sketches, so deleting one body does not shift
        // every later body's contents into a different file.
        const std::string relPath =
            designPathPrefix(body.designId) + format("geometry/body_%d.brep", body.id);
        const std::string fullPath = dir + "/" + relPath;

        if (!brep_io::writeBrep(fullPath, {body.shape}, errorMsg)) {
            return false;
        }
        m_geometryFiles.push_back(relPath);
    }

    return true;
}

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

bool Project::loadGeometry(const std::string& dir, std::string* errorMsg)
{
    namespace fs = std::filesystem;

    m_bodies.clear();

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
        for (const TopoDS_Shape& shape : shapes) {
            BodyData body;
            // Recover the id from the filename it was saved under, so a
            // body keeps its identity across a round trip.
            body.id = -1;
            const auto us = relPath.rfind("body_");
            const auto dot = relPath.rfind(".brep");
            if (us != std::string::npos && dot != std::string::npos && dot > us) {
                try {
                    body.id = std::stoi(relPath.substr(us + 5, dot - us - 5));
                } catch (const std::exception&) {
                    body.id = -1;
                }
            }
            if (body.id < 0) {
                body.id = nextBodyId();
            }
            body.name = format("Body%d", body.id);
            body.designId = 1;
            for (const DesignData& d : m_designs) {
                if (!d.pathPrefix.empty()
                    && relPath.rfind(d.pathPrefix, 0) == 0) {
                    body.designId = d.id;
                    break;
                }
            }
            body.shape = shape;
            m_bodies.push_back(body);
        }
    }

    return true;
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
    obj["design_id"] = plane.designId;
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
    obj["center_relative"] = plane.centerRelative;
    obj["center_ref_plane"] = plane.centerRefPlaneId;
    obj["visible"] = plane.visible;
    return obj;
}

ConstructionPlaneData Project::constructionPlaneFromJson(const QJsonObject& json) const
{
    ConstructionPlaneData plane;
    plane.id = json["id"].toInt();
    // A missing value reads as design 1, the only sensible reading for a
    // hand-edited file: a plane belonging to no design would be orphaned.
    plane.designId = json["design_id"].toInt(1);
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
    plane.centerRelative = json["center_relative"].toBool(false);
    plane.centerRefPlaneId = json["center_ref_plane"].toInt(-1);
    plane.visible = json["visible"].toBool(true);
    return plane;
}

// ---- JSON Serialization: Sketches ----

QJsonObject Project::sketchToJson(const SketchData& sketch) const
{
    QJsonObject obj;
    obj["feature_id"] = sketch.id;
    obj["design_id"] = sketch.designId;
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
    if (sketch.flipView) obj["flip_view"] = true;  // heads/tails; omitted when default

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
            if (pt.z != 0.0) ptArr.append(pt.z);   // 2D stays [x,y]; 3D adds z
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
            ent["ellipse_rotation"] = entity.ellipseRotation;
            ent["ellipse_start"] = entity.ellipseStart;
            ent["ellipse_sweep"] = entity.ellipseSweep;
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
        if (entity.type == SketchEntityType::Slot && !entity.pathEntityIds.empty()) {
            QJsonArray pids;
            for (int pid : entity.pathEntityIds) pids.append(pid);
            ent["path_entity_ids"] = pids;   // slot follows this centerline (unified single/multi)
        }
        if (entity.type == SketchEntityType::Spline && entity.splineBezier) {
            ent["spline_bezier"] = true;
        }
        if (entity.type == SketchEntityType::Spline && entity.splineClosed) {
            ent["spline_closed"] = true;
        }
        if (entity.type == SketchEntityType::Spline && entity.splineRational) {
            ent["spline_rational"] = true;
            QJsonArray wj;
            for (double w : entity.weights) wj.append(w);
            ent["weights"] = wj;
        }
        ent["constrained"] = entity.constrained;
        ent["is_construction"] = entity.isConstruction;
        ent["is_centerline"] = entity.isCenterline;
        ent["color"] = entity.color;
        ent["offset_parent_id"] = entity.offsetParentId;
        ent["projection_source_id"] = entity.projectionSourceId;
        ent["projection_source_sketch_id"] = entity.projectionSourceSketchId;
        ent["offset_distance"] = entity.offsetDistance;
        ent["offset_side"] = entity.offsetSide;
        if (entity.groupId >= 0) {
            ent["group_id"] = entity.groupId;
        }

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
        if (!constraint.expression.empty())
            c["expression"] = QString::fromStdString(constraint.expression);
        c["is_driving"] = constraint.isDriving;
        c["label_x"] = constraint.labelPosition.x;
        c["label_y"] = constraint.labelPosition.y;
        c["label_visible"] = constraint.labelVisible;
        c["enabled"] = constraint.enabled;
        // JSON cannot represent NaN; absence means "follow perimeter".
        if (!std::isnan(constraint.labelAngle)) {
            c["label_angle"] = constraint.labelAngle;
        }

        constraints.append(c);
    }
    obj["constraints"] = constraints;

    // Serialize groups.  Decomposed compounds (Rectangle, Polygon, ...)
    // exist only as primitives plus constraints bound by a named group,
    // so entityIds, constraintIds and each entity's group_id must all
    // round-trip or the compound loses its identity on reload.
    QJsonArray groups;
    for (const auto& g : sketch.groups) {
        QJsonObject go;
        go["id"] = g.id;
        go["name"] = QString::fromStdString(g.name);
        QJsonArray eids;
        for (int i : g.entityIds) eids.append(i);
        go["entity_ids"] = eids;
        QJsonArray cids;
        for (int i : g.constraintIds) cids.append(i);
        go["constraint_ids"] = cids;
        QJsonArray gids;
        for (int i : g.childGroupIds) gids.append(i);
        go["child_group_ids"] = gids;
        go["parent_group_id"] = g.parentGroupId;
        go["locked"] = g.locked;
        go["expanded"] = g.expanded;
        if (g.kind != sketch::GroupKind::User)
            go["kind"] = QString::fromLatin1(sketch::groupKindToken(g.kind));
        if (g.hasPivot) go["pivot"] = QJsonArray{g.pivot.x, g.pivot.y};
        groups.append(go);
    }
    obj["groups"] = groups;

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
    // Absent in format version 1; assignSketchIds() fills those in on load.
    sketch.id = json["feature_id"].toInt(-1);
    // Absent means design 1: everything in a single-design project belongs
    // to it, so nothing written before designs existed is ambiguous.
    sketch.designId = json["design_id"].toInt(1);
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
    sketch.flipView = json["flip_view"].toBool(false);

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
                const double z = ptArr.size() >= 3 ? ptArr[2].toDouble() : 0.0;
                entity.points.push_back(Point3(ptArr[0].toDouble(), ptArr[1].toDouble(), z));
            }
        }

        entity.radius = ent["radius"].toDouble();
        entity.startAngle = ent["start_angle"].toDouble();
        entity.sweepAngle = ent["sweep_angle"].toDouble();
        entity.sides = ent["sides"].toInt(6);  // Default 6 sides (hexagon)
        entity.majorRadius = ent["major_radius"].toDouble();
        entity.minorRadius = ent["minor_radius"].toDouble();
        entity.ellipseRotation = ent["ellipse_rotation"].toDouble();
        entity.ellipseStart = ent["ellipse_start"].toDouble();
        entity.ellipseSweep = ent.contains("ellipse_sweep") ? ent["ellipse_sweep"].toDouble() : 360.0;
        entity.text = ent["text"].toString().toStdString();
        entity.fontFamily = ent["font_family"].toString().toStdString();
        entity.fontSize = ent["font_size"].toDouble(12.0);
        entity.fontBold = ent["font_bold"].toBool();
        entity.fontItalic = ent["font_italic"].toBool();
        entity.textRotation = ent["text_rotation"].toDouble();
        entity.arcFlipped = ent["arc_flipped"].toBool();
        if (ent.contains("path_entity_ids")) {
            entity.pathEntityIds.clear();
            for (const auto& v : ent["path_entity_ids"].toArray())
                entity.pathEntityIds.push_back(v.toInt());
        } else if (ent.contains("path_entity_id")) {
            const int pid = ent["path_entity_id"].toInt(-1);
            if (pid >= 0) entity.pathEntityIds = { pid };
        }
        entity.splineBezier = ent["spline_bezier"].toBool();
        entity.splineRational = ent["spline_rational"].toBool();
        entity.splineClosed = ent["spline_closed"].toBool();
        { const QJsonArray wj = ent["weights"].toArray(); entity.weights.clear();
          for (const auto& v : wj) entity.weights.push_back(v.toDouble()); }
        entity.constrained = ent["constrained"].toBool();
        entity.isConstruction = ent["is_construction"].toBool();
        entity.isCenterline = ent["is_centerline"].toBool();
        entity.color = ent["color"].toInt(-1);
        entity.offsetParentId = ent["offset_parent_id"].toInt(-1);
        entity.projectionSourceId = ent["projection_source_id"].toInt(-1);
        entity.projectionSourceSketchId = ent["projection_source_sketch_id"].toInt(-1);
        entity.offsetDistance = ent["offset_distance"].toDouble();
        entity.offsetSide = ent["offset_side"].toInt();
        entity.groupId = ent["group_id"].toInt(-1);

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
        constraint.expression = c["expression"].toString().toStdString();
        constraint.isDriving = c["is_driving"].toBool(true);
        constraint.labelPosition = Point2D(
            c["label_x"].toDouble(),
            c["label_y"].toDouble()
        );
        constraint.labelVisible = c["label_visible"].toBool(true);
        constraint.enabled = c["enabled"].toBool(true);
        if (c.contains("label_angle"))
            constraint.labelAngle = c["label_angle"].toDouble();

        sketch.constraints.push_back(constraint);
    }

    // Deserialize groups
    if (json.contains("groups")) {
        QJsonArray groups = json["groups"].toArray();
        for (const auto& gVal : groups) {
            QJsonObject go = gVal.toObject();
            sketch::Group g;
            g.id = go["id"].toInt();
            g.name = go["name"].toString().toStdString();
            for (const auto& v : go["entity_ids"].toArray())
                g.entityIds.push_back(v.toInt());
            for (const auto& v : go["constraint_ids"].toArray())
                g.constraintIds.push_back(v.toInt());
            for (const auto& v : go["child_group_ids"].toArray())
                g.childGroupIds.push_back(v.toInt());
            g.parentGroupId = go["parent_group_id"].toInt(-1);
            g.locked = go["locked"].toBool(false);
            g.expanded = go["expanded"].toBool(true);
            // Older files carry no kind; the name prefix says what they were.
            if (!go.contains("kind")
                || !sketch::parseGroupKindToken(go["kind"].toString().toStdString(), g.kind))
                g.kind = sketch::inferLegacyGroupKind(g.name);
            if (go.contains("pivot") && go["pivot"].isArray() && go["pivot"].toArray().size() == 2) {
                const QJsonArray pv = go["pivot"].toArray();
                g.hasPivot = true;
                g.pivot = {pv[0].toDouble(), pv[1].toDouble()};
            }
            sketch.groups.push_back(g);
        }
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
        p["design_id"] = param.designId;
        p["name"] = QString::fromStdString(param.name);
        p["expression"] = QString::fromStdString(param.expression);
        p["value"] = param.value;
        p["unit"] = QString::fromStdString(param.unit);
        p["comment"] = QString::fromStdString(param.comment);
        p["is_user_param"] = param.isUserParam;
        if (param.isReference) {
            p["is_reference"] = true;
            if (!param.referenceSource.empty())
                p["reference_source"] = QString::fromStdString(param.referenceSource);
        }
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
        // A missing value reads as design 1, the only sensible reading for
        // a hand-edited file.
        param.designId = p["design_id"].toInt(1);
        param.name = p["name"].toString().toStdString();
        param.expression = p["expression"].toString().toStdString();
        param.value = p["value"].toDouble();
        param.unit = p["unit"].toString().toStdString();
        param.comment = p["comment"].toString().toStdString();
        param.isUserParam = p["is_user_param"].toBool(true);
        param.isReference = p["is_reference"].toBool(false);
        param.referenceSource = p["reference_source"].toString().toStdString();
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
        QJsonArray npArr;
        for (const auto& np : m_namedPoints) {
            QJsonObject o;
            o["design_id"] = np.designId;
            o["name"] = QString::fromStdString(np.name);
            o["x"] = QString::fromStdString(np.xExpr);
            o["y"] = QString::fromStdString(np.yExpr);
            o["z"] = QString::fromStdString(np.zExpr);
            npArr.append(o);
        }
        obj["named_points"] = npArr;
    }
    {
        QJsonArray skArr;
        for (const auto& f : m_sketchFiles) {
            skArr.append(QString::fromStdString(f));
        }
        obj["sketches"] = skArr;
    }

    // Always written, even for the single design a project has today. That
    // is the whole point: a later reader never has to guess whether the
    // top-level sketches/ belongs to a design, so no conversion is needed.
    {
        QJsonArray designs;
        for (const DesignData& d : m_designs) {
            QJsonObject o;
            o["id"] = d.id;
            o["name"] = QString::fromStdString(d.name);
            o["path"] = QString::fromStdString(d.pathPrefix);
            designs.append(o);
        }
        obj["designs"] = designs;
        obj["active_design"] = m_activeDesignId;
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

    // Designs. An absent list is not an old format needing conversion; it
    // IS a project with one design, which is what every project written
    // before designs existed was. Synthesizing it here is the whole reason
    // no migration is needed.
    m_designs.clear();
    for (const auto& v : json["designs"].toArray()) {
        const QJsonObject o = v.toObject();
        DesignData d;
        d.id = o["id"].toInt(1);
        d.name = o["name"].toString().toStdString();
        d.pathPrefix = o["path"].toString().toStdString();
        m_designs.push_back(d);
    }
    if (m_designs.empty()) {
        DesignData only;
        only.id = 1;
        only.name = "Design1";
        only.pathPrefix.clear();     // files stay where they are
        m_designs.push_back(only);
    }
    m_activeDesignId = json["active_design"].toInt(m_designs.front().id);
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

    m_namedPoints.clear();
    for (const auto& val : json["named_points"].toArray()) {
        const QJsonObject o = val.toObject();
        NamedPointData np;
        np.designId = o["design_id"].toInt();
        np.name = o["name"].toString().toStdString();
        np.xExpr = o["x"].toString().toStdString();
        np.yExpr = o["y"].toString().toStdString();
        np.zExpr = o["z"].toString().toStdString();
        m_namedPoints.push_back(np);
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

bool Project::saveConstructionPlanes(const std::string& dir, std::string* errorMsg)
{
    m_constructionPlaneFiles.clear();

    for (size_t i = 0; i < m_constructionPlanes.size(); ++i) {
        // Named by id, like sketches and bodies: deleting one plane must
        // not shift every later plane's contents into a different file,
        // and a sketch referencing plane 3 must keep finding plane 3.
        const ConstructionPlaneData& plane = m_constructionPlanes[i];
        std::string relPath = designPathPrefix(plane.designId)
                            + format("construction/plane_%d.json", plane.id);
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
        std::string relPath = sketchRelPath(m_sketches[i], i,
                                           designPathPrefix(m_sketches[i].designId));
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

#else  // !HOBBYCAD_HAS_QT: nlohmann/json + std::fstream fallback

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
    obj["design_id"] = plane.designId;
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
    obj["center_relative"] = plane.centerRelative;
    obj["center_ref_plane"] = plane.centerRefPlaneId;
    obj["visible"] = plane.visible;
    return obj;
}

ConstructionPlaneData Project::constructionPlaneFromJson(const nlohmann::json& json) const
{
    ConstructionPlaneData plane;
    plane.id = json.value("id", 0);
    plane.designId = json.value("design_id", 1);
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
    plane.centerRelative = json.value("center_relative", false);
    plane.centerRefPlaneId = json.value("center_ref_plane", -1);
    plane.visible = json.value("visible", true);
    return plane;
}

// ---- JSON Serialization: Sketches ----

nlohmann::json Project::sketchToJson(const SketchData& sketch) const
{
    nlohmann::json obj;
    obj["feature_id"] = sketch.id;
    obj["design_id"] = sketch.designId;
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
    if (sketch.flipView) obj["flip_view"] = true;  // heads/tails; omitted when default

    nlohmann::json entities = nlohmann::json::array();
    for (const auto& entity : sketch.entities) {
        nlohmann::json ent;
        ent["id"] = entity.id;
        ent["type"] = static_cast<int>(entity.type);

        nlohmann::json pts = nlohmann::json::array();
        for (const auto& pt : entity.points) {
            if (pt.z != 0.0) pts.push_back({pt.x, pt.y, pt.z});   // 2D stays [x,y]
            else             pts.push_back({pt.x, pt.y});
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
            ent["ellipse_rotation"] = entity.ellipseRotation;
            ent["ellipse_start"] = entity.ellipseStart;
            ent["ellipse_sweep"] = entity.ellipseSweep;
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
        if (entity.type == SketchEntityType::Slot && !entity.pathEntityIds.empty())
            ent["path_entity_ids"] = entity.pathEntityIds;
        if (entity.type == SketchEntityType::Spline && entity.splineBezier) {
            ent["spline_bezier"] = true;
        }
        if (entity.type == SketchEntityType::Spline && entity.splineClosed) {
            ent["spline_closed"] = true;
        }
        if (entity.type == SketchEntityType::Spline && entity.splineRational) {
            ent["spline_rational"] = true;
            ent["weights"] = entity.weights;
        }
        ent["constrained"] = entity.constrained;
        ent["is_construction"] = entity.isConstruction;
        ent["is_centerline"] = entity.isCenterline;
        ent["color"] = entity.color;
        ent["offset_parent_id"] = entity.offsetParentId;
        ent["projection_source_id"] = entity.projectionSourceId;
        ent["projection_source_sketch_id"] = entity.projectionSourceSketchId;
        ent["offset_distance"] = entity.offsetDistance;
        ent["offset_side"] = entity.offsetSide;
        if (entity.groupId >= 0) {
            ent["group_id"] = entity.groupId;
        }

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
        if (!constraint.expression.empty())
            c["expression"] = constraint.expression;
        c["is_driving"] = constraint.isDriving;
        c["label_x"] = constraint.labelPosition.x;
        c["label_y"] = constraint.labelPosition.y;
        c["label_visible"] = constraint.labelVisible;
        c["enabled"] = constraint.enabled;
        // JSON cannot represent NaN; absence means "follow perimeter".
        if (!std::isnan(constraint.labelAngle)) {
            c["label_angle"] = constraint.labelAngle;
        }

        constraints.push_back(c);
    }
    obj["constraints"] = constraints;

    // Serialize groups.  Decomposed compounds (Rectangle, Polygon, ...)
    // exist only as primitives plus constraints bound by a named group,
    // so entityIds, constraintIds and each entity's group_id must all
    // round-trip or the compound loses its identity on reload.
    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : sketch.groups) {
        nlohmann::json go;
        go["id"] = g.id;
        go["name"] = g.name;
        go["entity_ids"] = g.entityIds;
        go["constraint_ids"] = g.constraintIds;
        go["child_group_ids"] = g.childGroupIds;
        go["parent_group_id"] = g.parentGroupId;
        go["locked"] = g.locked;
        go["expanded"] = g.expanded;
        if (g.kind != sketch::GroupKind::User) go["kind"] = sketch::groupKindToken(g.kind);
        if (g.hasPivot) go["pivot"] = std::vector<double>{g.pivot.x, g.pivot.y};
        groups.push_back(go);
    }
    obj["groups"] = groups;

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
    // Absent in format version 1; assignSketchIds() fills those in on load.
    sketch.id = json.value("feature_id", -1);
    sketch.designId = json.value("design_id", 1);
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
    sketch.flipView = json.value("flip_view", false);

    if (json.contains("entities")) {
        for (const auto& ent : json["entities"]) {
            SketchEntityData entity;
            entity.id = ent.value("id", 0);
            entity.type = static_cast<SketchEntityType>(ent.value("type", 0));

            if (ent.contains("points")) {
                for (const auto& ptArr : ent["points"]) {
                    if (ptArr.is_array() && ptArr.size() >= 2) {
                        const double z = ptArr.size() >= 3 ? ptArr[2].get<double>() : 0.0;
                        entity.points.push_back(Point3(
                            ptArr[0].get<double>(), ptArr[1].get<double>(), z));
                    }
                }
            }

            entity.radius = ent.value("radius", 0.0);
            entity.startAngle = ent.value("start_angle", 0.0);
            entity.sweepAngle = ent.value("sweep_angle", 0.0);
            entity.sides = ent.value("sides", 6);
            entity.majorRadius = ent.value("major_radius", 0.0);
            entity.minorRadius = ent.value("minor_radius", 0.0);
            entity.ellipseRotation = ent.value("ellipse_rotation", 0.0);
            entity.ellipseStart = ent.value("ellipse_start", 0.0);
            entity.ellipseSweep = ent.value("ellipse_sweep", 360.0);
            entity.text = ent.value("text", std::string{});
            entity.fontFamily = ent.value("font_family", std::string{});
            entity.fontSize = ent.value("font_size", 12.0);
            entity.fontBold = ent.value("font_bold", false);
            entity.fontItalic = ent.value("font_italic", false);
            entity.textRotation = ent.value("text_rotation", 0.0);
            entity.arcFlipped = ent.value("arc_flipped", false);
            if (ent.contains("path_entity_ids"))
                entity.pathEntityIds = ent["path_entity_ids"].get<std::vector<int>>();
            else if (ent.contains("path_entity_id")) {
                const int pid = ent.value("path_entity_id", -1);
                if (pid >= 0) entity.pathEntityIds = { pid };
            }
            entity.splineBezier = ent.value("spline_bezier", false);
            entity.splineRational = ent.value("spline_rational", false);
            entity.splineClosed = ent.value("spline_closed", false);
            if (ent.contains("weights")) entity.weights = ent["weights"].get<std::vector<double>>();
            entity.constrained = ent.value("constrained", false);
            entity.isConstruction = ent.value("is_construction", false);
            entity.isCenterline = ent.value("is_centerline", false);
            entity.color = ent.value("color", -1);
            entity.offsetParentId = ent.value("offset_parent_id", -1);
            entity.projectionSourceId = ent.value("projection_source_id", -1);
            entity.projectionSourceSketchId = ent.value("projection_source_sketch_id", -1);
            entity.offsetDistance = ent.value("offset_distance", 0.0);
            entity.offsetSide = ent.value("offset_side", 0);
            entity.groupId = ent.value("group_id", -1);

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
            constraint.expression = c.value("expression", std::string());
            constraint.isDriving = c.value("is_driving", true);
            constraint.labelPosition = Point2D(
                c.value("label_x", 0.0),
                c.value("label_y", 0.0)
            );
            constraint.labelVisible = c.value("label_visible", true);
            constraint.enabled = c.value("enabled", true);
            if (c.contains("label_angle"))
                constraint.labelAngle = c["label_angle"].get<double>();

            sketch.constraints.push_back(constraint);
        }
    }

    // Deserialize groups
    if (json.contains("groups")) {
        for (const auto& go : json["groups"]) {
            sketch::Group g;
            g.id = go.value("id", 0);
            g.name = go.value("name", std::string{});
            if (go.contains("entity_ids"))
                g.entityIds = go["entity_ids"].get<std::vector<int>>();
            if (go.contains("constraint_ids"))
                g.constraintIds = go["constraint_ids"].get<std::vector<int>>();
            if (go.contains("child_group_ids"))
                g.childGroupIds = go["child_group_ids"].get<std::vector<int>>();
            g.parentGroupId = go.value("parent_group_id", -1);
            g.locked = go.value("locked", false);
            g.expanded = go.value("expanded", true);
            // Older files carry no kind; the name prefix says what they were.
            if (!go.contains("kind")
                || !sketch::parseGroupKindToken(go.value("kind", std::string{}), g.kind))
                g.kind = sketch::inferLegacyGroupKind(g.name);
            if (go.contains("pivot") && go["pivot"].is_array() && go["pivot"].size() == 2) {
                g.hasPivot = true;
                g.pivot = {go["pivot"][0].get<double>(), go["pivot"][1].get<double>()};
            }
            sketch.groups.push_back(g);
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
        p["design_id"] = param.designId;
        p["name"] = param.name;
        p["expression"] = param.expression;
        p["value"] = param.value;
        p["unit"] = param.unit;
        p["comment"] = param.comment;
        p["is_user_param"] = param.isUserParam;
        if (param.isReference) {
            p["is_reference"] = true;
            if (!param.referenceSource.empty())
                p["reference_source"] = param.referenceSource;
        }
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
        param.designId = p.value("design_id", 1);
        param.name = p.value("name", std::string{});
        param.expression = p.value("expression", std::string{});
        param.value = p.value("value", 0.0);
        param.unit = p.value("unit", std::string{});
        param.comment = p.value("comment", std::string{});
        param.isUserParam = p.value("is_user_param", true);
        param.isReference = p.value("is_reference", false);
        param.referenceSource = p.value("reference_source", std::string{});
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
        nlohmann::json npArr = nlohmann::json::array();
        for (const auto& np : m_namedPoints) {
            nlohmann::json o;
            o["design_id"] = np.designId;
            o["name"] = np.name;
            o["x"] = np.xExpr;
            o["y"] = np.yExpr;
            o["z"] = np.zExpr;
            npArr.push_back(o);
        }
        obj["named_points"] = npArr;
    }
    {
        nlohmann::json skArr = nlohmann::json::array();
        for (const auto& f : m_sketchFiles) {
            skArr.push_back(f);
        }
        obj["sketches"] = skArr;
    }

    // See the Qt path: designs are always listed, single or not.
    {
        nlohmann::json designs = nlohmann::json::array();
        for (const DesignData& d : m_designs) {
            nlohmann::json o;
            o["id"] = d.id;
            o["name"] = d.name;
            o["path"] = d.pathPrefix;
            designs.push_back(o);
        }
        obj["designs"] = designs;
        obj["active_design"] = m_activeDesignId;
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

    // See the Qt path: an absent list means one design, not an old format.
    m_designs.clear();
    if (json.contains("designs")) {
        for (const auto& o : json["designs"]) {
            DesignData d;
            d.id = o.value("id", 1);
            d.name = o.value("name", std::string{});
            d.pathPrefix = o.value("path", std::string{});
            m_designs.push_back(d);
        }
    }
    if (m_designs.empty()) {
        DesignData only;
        only.id = 1;
        only.name = "Design1";
        m_designs.push_back(only);
    }
    m_activeDesignId = json.value("active_design", m_designs.front().id);
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

    m_namedPoints.clear();
    if (json.contains("named_points")) {
        for (const auto& o : json["named_points"]) {
            NamedPointData np;
            np.designId = o.value("design_id", 0);
            np.name = o.value("name", std::string());
            np.xExpr = o.value("x", std::string());
            np.yExpr = o.value("y", std::string());
            np.zExpr = o.value("z", std::string());
            m_namedPoints.push_back(np);
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
        // Named by id, like sketches and bodies: deleting one plane must
        // not shift every later plane's contents into a different file,
        // and a sketch referencing plane 3 must keep finding plane 3.
        const ConstructionPlaneData& plane = m_constructionPlanes[i];
        std::string relPath = designPathPrefix(plane.designId)
                            + format("construction/plane_%d.json", plane.id);
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
        std::string relPath = sketchRelPath(m_sketches[i], i,
                                           designPathPrefix(m_sketches[i].designId));
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
