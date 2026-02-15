// =====================================================================
//  src/libhobbycad/brep/operations.cpp — 3D BREP operations
// =====================================================================
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <hobbycad/brep/operations.h>

// OpenCASCADE includes
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

// Wire/Edge building
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

// 3D operations
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>

// Boolean operations
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>

// Shape modification
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>

// Geometry
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Ax2.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <GeomAPI_Interpolate.hxx>

// Lists for thick solid
#include <TopTools_ListOfShape.hxx>

#include <QtMath>

namespace hobbycad {
namespace brep {

// =====================================================================
//  Helper Functions
// =====================================================================

namespace {

/// Find entity by ID in entity list
const sketch::Entity* findEntity(int id, const QVector<sketch::Entity>& entities)
{
    for (const sketch::Entity& e : entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

/// Convert 2D sketch point to 3D point (on XY plane at Z=0)
gp_Pnt toPoint3D(const QPointF& p2d, double z = 0.0)
{
    return gp_Pnt(p2d.x(), p2d.y(), z);
}

/// Build an edge from a sketch entity
/// Returns null edge if entity type not supported
TopoDS_Edge buildEdge(const sketch::Entity& entity, bool reversed = false)
{
    TopoDS_Edge edge;

    switch (entity.type) {
    case sketch::EntityType::Line:
        if (entity.points.size() >= 2) {
            gp_Pnt p1 = toPoint3D(entity.points[reversed ? 1 : 0]);
            gp_Pnt p2 = toPoint3D(entity.points[reversed ? 0 : 1]);
            edge = BRepBuilderAPI_MakeEdge(p1, p2);
        }
        break;

    case sketch::EntityType::Arc:
        if (!entity.points.isEmpty()) {
            gp_Pnt center = toPoint3D(entity.points[0]);
            gp_Dir zDir(0, 0, 1);
            gp_Ax2 axis(center, zDir);
            gp_Circ circle(axis, entity.radius);

            double startRad = qDegreesToRadians(entity.startAngle);
            double endRad = qDegreesToRadians(entity.startAngle + entity.sweepAngle);

            if (reversed) {
                std::swap(startRad, endRad);
            }

            BRepBuilderAPI_MakeEdge makeEdge(circle, startRad, endRad);
            if (makeEdge.IsDone()) {
                edge = makeEdge.Edge();
            }
        }
        break;

    case sketch::EntityType::Circle:
        if (!entity.points.isEmpty()) {
            gp_Pnt center = toPoint3D(entity.points[0]);
            gp_Dir zDir(0, 0, 1);
            gp_Ax2 axis(center, zDir);
            gp_Circ circle(axis, entity.radius);

            BRepBuilderAPI_MakeEdge makeEdge(circle);
            if (makeEdge.IsDone()) {
                edge = makeEdge.Edge();
            }
        }
        break;

    case sketch::EntityType::Ellipse:
        if (!entity.points.isEmpty()) {
            gp_Pnt center = toPoint3D(entity.points[0]);
            gp_Dir zDir(0, 0, 1);
            gp_Ax2 axis(center, zDir);
            // gp_Elips requires major >= minor
            double majorR = qMax(entity.majorRadius, entity.minorRadius);
            double minorR = qMin(entity.majorRadius, entity.minorRadius);
            gp_Elips ellipse(axis, majorR, minorR);

            BRepBuilderAPI_MakeEdge makeEdge(ellipse);
            if (makeEdge.IsDone()) {
                edge = makeEdge.Edge();
            }
        }
        break;

    case sketch::EntityType::Spline:
        if (entity.points.size() >= 2) {
            // Build B-Spline through control points
            int nPts = entity.points.size();
            TColgp_Array1OfPnt pts(1, nPts);
            for (int i = 0; i < nPts; ++i) {
                int idx = reversed ? (nPts - 1 - i) : i;
                pts.SetValue(i + 1, toPoint3D(entity.points[idx]));
            }

            Handle(TColgp_HArray1OfPnt) hPts = new TColgp_HArray1OfPnt(pts);
            GeomAPI_Interpolate interp(hPts, Standard_False, 1e-6);
            interp.Perform();

            if (interp.IsDone()) {
                Handle(Geom_BSplineCurve) curve = interp.Curve();
                BRepBuilderAPI_MakeEdge makeEdge(curve);
                if (makeEdge.IsDone()) {
                    edge = makeEdge.Edge();
                }
            }
        }
        break;

    case sketch::EntityType::Rectangle:
        // Rectangle is actually 4 edges - handled specially in wire building
        break;

    case sketch::EntityType::Polygon:
        // Polygon is multiple edges - handled specially in wire building
        break;

    case sketch::EntityType::Slot:
        // Slot is 2 arcs + 2 lines - handled specially in wire building
        break;

    default:
        break;
    }

    return edge;
}

/// Build multiple edges for rectangle entity
QVector<TopoDS_Edge> buildRectangleEdges(const sketch::Entity& entity, bool reversed = false)
{
    QVector<TopoDS_Edge> edges;

    if (entity.type != sketch::EntityType::Rectangle || entity.points.size() < 2)
        return edges;

    QPointF p1 = entity.points[0];
    QPointF p3 = entity.points[1];
    QPointF p2(p3.x(), p1.y());
    QPointF p4(p1.x(), p3.y());

    QVector<QPointF> corners = {p1, p2, p3, p4};
    if (reversed) {
        std::reverse(corners.begin(), corners.end());
    }

    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        gp_Pnt pt1 = toPoint3D(corners[i]);
        gp_Pnt pt2 = toPoint3D(corners[j]);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(pt1, pt2);
        edges.append(edge);
    }

    return edges;
}

/// Build multiple edges for polygon entity
QVector<TopoDS_Edge> buildPolygonEdges(const sketch::Entity& entity, bool reversed = false)
{
    QVector<TopoDS_Edge> edges;

    if (entity.type != sketch::EntityType::Polygon || entity.points.isEmpty())
        return edges;

    QPointF center = entity.points[0];
    int n = entity.sides;
    double r = entity.radius;

    QVector<QPointF> vertices;
    for (int i = 0; i < n; ++i) {
        double angle = 2.0 * M_PI * i / n - M_PI / 2.0;  // Start at top
        vertices.append(center + QPointF(r * qCos(angle), r * qSin(angle)));
    }

    if (reversed) {
        std::reverse(vertices.begin(), vertices.end());
    }

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        gp_Pnt pt1 = toPoint3D(vertices[i]);
        gp_Pnt pt2 = toPoint3D(vertices[j]);
        TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(pt1, pt2);
        edges.append(edge);
    }

    return edges;
}

/// Build multiple edges for slot entity
QVector<TopoDS_Edge> buildSlotEdges(const sketch::Entity& entity, bool reversed = false)
{
    QVector<TopoDS_Edge> edges;

    if (entity.type != sketch::EntityType::Slot || entity.points.size() < 2)
        return edges;

    QPointF c1 = entity.points[0];
    QPointF c2 = entity.points[1];
    double r = entity.radius;

    // Direction from c1 to c2
    QPointF dir = c2 - c1;
    double len = qSqrt(dir.x() * dir.x() + dir.y() * dir.y());
    if (len < 1e-6) return edges;

    dir /= len;
    QPointF perp(-dir.y(), dir.x());

    // Four key points on the slot outline
    QPointF p1 = c1 + perp * r;
    QPointF p2 = c2 + perp * r;
    QPointF p3 = c2 - perp * r;
    QPointF p4 = c1 - perp * r;

    // Two straight lines and two arcs
    // Arc angles
    double angle1 = qRadiansToDegrees(qAtan2(perp.y(), perp.x()));
    double angle2 = angle1 + 180.0;

    if (!reversed) {
        // Line from p1 to p2
        edges.append(BRepBuilderAPI_MakeEdge(toPoint3D(p1), toPoint3D(p2)));

        // Arc at c2 from p2 to p3
        gp_Ax2 axis2(toPoint3D(c2), gp_Dir(0, 0, 1));
        gp_Circ circ2(axis2, r);
        double start2 = qDegreesToRadians(angle1);
        double end2 = qDegreesToRadians(angle2);
        edges.append(BRepBuilderAPI_MakeEdge(circ2, start2, end2));

        // Line from p3 to p4
        edges.append(BRepBuilderAPI_MakeEdge(toPoint3D(p3), toPoint3D(p4)));

        // Arc at c1 from p4 to p1
        gp_Ax2 axis1(toPoint3D(c1), gp_Dir(0, 0, 1));
        gp_Circ circ1(axis1, r);
        double start1 = qDegreesToRadians(angle2);
        double end1 = qDegreesToRadians(angle1 + 360.0);
        edges.append(BRepBuilderAPI_MakeEdge(circ1, start1, end1));
    } else {
        // Reversed order
        gp_Ax2 axis1(toPoint3D(c1), gp_Dir(0, 0, 1));
        gp_Circ circ1(axis1, r);
        double start1 = qDegreesToRadians(angle1 + 360.0);
        double end1 = qDegreesToRadians(angle2);
        edges.append(BRepBuilderAPI_MakeEdge(circ1, end1, start1));

        edges.append(BRepBuilderAPI_MakeEdge(toPoint3D(p4), toPoint3D(p3)));

        gp_Ax2 axis2(toPoint3D(c2), gp_Dir(0, 0, 1));
        gp_Circ circ2(axis2, r);
        double start2 = qDegreesToRadians(angle2);
        double end2 = qDegreesToRadians(angle1);
        edges.append(BRepBuilderAPI_MakeEdge(circ2, end2, start2));

        edges.append(BRepBuilderAPI_MakeEdge(toPoint3D(p2), toPoint3D(p1)));
    }

    return edges;
}

/// Build a wire from a profile
TopoDS_Wire buildWireFromProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities)
{
    BRepBuilderAPI_MakeWire wireBuilder;

    for (int i = 0; i < profile.entityIds.size(); ++i) {
        int entityId = profile.entityIds[i];
        bool reversed = (i < profile.reversed.size()) ? profile.reversed[i] : false;

        const sketch::Entity* entity = findEntity(entityId, entities);
        if (!entity) continue;

        // Handle multi-edge entities
        if (entity->type == sketch::EntityType::Rectangle) {
            auto edges = buildRectangleEdges(*entity, reversed);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else if (entity->type == sketch::EntityType::Polygon) {
            auto edges = buildPolygonEdges(*entity, reversed);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else if (entity->type == sketch::EntityType::Slot) {
            auto edges = buildSlotEdges(*entity, reversed);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else {
            TopoDS_Edge edge = buildEdge(*entity, reversed);
            if (!edge.IsNull()) {
                wireBuilder.Add(edge);
            }
        }
    }

    if (wireBuilder.IsDone()) {
        return wireBuilder.Wire();
    }

    return TopoDS_Wire();
}

/// Build a face from a wire
TopoDS_Face buildFaceFromWire(const TopoDS_Wire& wire)
{
    if (wire.IsNull()) return TopoDS_Face();

    BRepBuilderAPI_MakeFace faceBuilder(wire, Standard_True);  // planar = true
    if (faceBuilder.IsDone()) {
        return faceBuilder.Face();
    }

    return TopoDS_Face();
}

/// Build a wire from a sequence of entities (for sweep path, etc.)
TopoDS_Wire buildWireFromEntities(const QVector<sketch::Entity>& pathEntities)
{
    BRepBuilderAPI_MakeWire wireBuilder;

    for (const sketch::Entity& entity : pathEntities) {
        if (entity.isConstruction) continue;

        if (entity.type == sketch::EntityType::Rectangle) {
            auto edges = buildRectangleEdges(entity, false);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else if (entity.type == sketch::EntityType::Polygon) {
            auto edges = buildPolygonEdges(entity, false);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else if (entity.type == sketch::EntityType::Slot) {
            auto edges = buildSlotEdges(entity, false);
            for (const auto& edge : edges) {
                if (!edge.IsNull()) wireBuilder.Add(edge);
            }
        } else {
            TopoDS_Edge edge = buildEdge(entity, false);
            if (!edge.IsNull()) {
                wireBuilder.Add(edge);
            }
        }
    }

    if (wireBuilder.IsDone()) {
        return wireBuilder.Wire();
    }

    return TopoDS_Wire();
}

}  // anonymous namespace

// =====================================================================
//  Sketch to 3D Operations
// =====================================================================

OperationResult extrudeProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Dir& direction,
    double distance)
{
    OperationResult result;

    // Build wire from profile
    TopoDS_Wire wire = buildWireFromProfile(profile, entities);
    if (wire.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build wire from profile");
        return result;
    }

    // Build face from wire
    TopoDS_Face face = buildFaceFromWire(wire);
    if (face.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build face from wire");
        return result;
    }

    // Create extrusion vector
    gp_Vec extrusionVec(direction);
    extrusionVec.Scale(distance);

    // Perform extrusion
    try {
        BRepPrimAPI_MakePrism prism(face, extrusionVec, Standard_True);  // copy = true
        if (prism.IsDone()) {
            result.shape = prism.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Extrusion operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during extrusion");
    }

    return result;
}

OperationResult extrudeProfileSymmetric(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Dir& direction,
    double distance,
    bool symmetric)
{
    OperationResult result;

    // Build wire from profile
    TopoDS_Wire wire = buildWireFromProfile(profile, entities);
    if (wire.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build wire from profile");
        return result;
    }

    // Build face from wire
    TopoDS_Face face = buildFaceFromWire(wire);
    if (face.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build face from wire");
        return result;
    }

    try {
        if (symmetric) {
            // Extrude half in each direction
            double halfDist = distance / 2.0;

            gp_Vec vec1(direction);
            vec1.Scale(halfDist);

            gp_Vec vec2(direction);
            vec2.Scale(-halfDist);

            BRepPrimAPI_MakePrism prism1(face, vec1, Standard_True);
            BRepPrimAPI_MakePrism prism2(face, vec2, Standard_True);

            if (prism1.IsDone() && prism2.IsDone()) {
                // Fuse the two halves
                BRepAlgoAPI_Fuse fuse(prism1.Shape(), prism2.Shape());
                if (fuse.IsDone()) {
                    result.shape = fuse.Shape();
                    result.success = true;
                } else {
                    result.errorMessage = QStringLiteral("Failed to fuse symmetric extrusions");
                }
            } else {
                result.errorMessage = QStringLiteral("Symmetric extrusion failed");
            }
        } else {
            // Single direction extrusion
            gp_Vec extrusionVec(direction);
            extrusionVec.Scale(distance);

            BRepPrimAPI_MakePrism prism(face, extrusionVec, Standard_True);
            if (prism.IsDone()) {
                result.shape = prism.Shape();
                result.success = true;
            } else {
                result.errorMessage = QStringLiteral("Extrusion operation failed");
            }
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during extrusion");
    }

    return result;
}

OperationResult revolveProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const gp_Ax1& axis,
    double angleDegrees)
{
    OperationResult result;

    // Build wire from profile
    TopoDS_Wire wire = buildWireFromProfile(profile, entities);
    if (wire.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build wire from profile");
        return result;
    }

    // Build face from wire
    TopoDS_Face face = buildFaceFromWire(wire);
    if (face.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build face from wire");
        return result;
    }

    // Convert angle to radians
    double angleRad = qDegreesToRadians(angleDegrees);

    // Perform revolution
    try {
        BRepPrimAPI_MakeRevol revol(face, axis, angleRad, Standard_True);  // copy = true
        if (revol.IsDone()) {
            result.shape = revol.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Revolution operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during revolution");
    }

    return result;
}

OperationResult sweepProfile(
    const sketch::Profile& profile,
    const QVector<sketch::Entity>& entities,
    const QVector<sketch::Entity>& pathEntities)
{
    OperationResult result;

    // Build profile wire
    TopoDS_Wire profileWire = buildWireFromProfile(profile, entities);
    if (profileWire.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build profile wire");
        return result;
    }

    // Build path wire
    TopoDS_Wire pathWire = buildWireFromEntities(pathEntities);
    if (pathWire.IsNull()) {
        result.errorMessage = QStringLiteral("Failed to build path wire");
        return result;
    }

    // Perform sweep (pipe)
    try {
        BRepOffsetAPI_MakePipe pipe(pathWire, profileWire);
        pipe.Build();
        if (pipe.IsDone()) {
            result.shape = pipe.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Sweep operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during sweep");
    }

    return result;
}

OperationResult loftProfiles(
    const QVector<sketch::Profile>& profiles,
    const QVector<sketch::Entity>& entities,
    bool solid)
{
    OperationResult result;

    if (profiles.size() < 2) {
        result.errorMessage = QStringLiteral("Loft requires at least 2 profiles");
        return result;
    }

    try {
        BRepOffsetAPI_ThruSections loft(solid ? Standard_True : Standard_False,
                                         Standard_False);  // ruled = false

        for (const sketch::Profile& profile : profiles) {
            TopoDS_Wire wire = buildWireFromProfile(profile, entities);
            if (wire.IsNull()) {
                result.errorMessage = QStringLiteral("Failed to build wire for profile");
                return result;
            }
            loft.AddWire(wire);
        }

        loft.Build();
        if (loft.IsDone()) {
            result.shape = loft.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Loft operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during loft");
    }

    return result;
}

// =====================================================================
//  Boolean Operations
// =====================================================================

OperationResult fuseShapes(
    const TopoDS_Shape& shape1,
    const TopoDS_Shape& shape2)
{
    OperationResult result;

    if (shape1.IsNull() || shape2.IsNull()) {
        result.errorMessage = QStringLiteral("One or both shapes are null");
        return result;
    }

    try {
        BRepAlgoAPI_Fuse fuse(shape1, shape2);
        if (fuse.IsDone()) {
            result.shape = fuse.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Fuse operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during fuse operation");
    }

    return result;
}

OperationResult cutShape(
    const TopoDS_Shape& shape,
    const TopoDS_Shape& tool)
{
    OperationResult result;

    if (shape.IsNull() || tool.IsNull()) {
        result.errorMessage = QStringLiteral("One or both shapes are null");
        return result;
    }

    try {
        BRepAlgoAPI_Cut cut(shape, tool);
        if (cut.IsDone()) {
            result.shape = cut.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Cut operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during cut operation");
    }

    return result;
}

OperationResult intersectShapes(
    const TopoDS_Shape& shape1,
    const TopoDS_Shape& shape2)
{
    OperationResult result;

    if (shape1.IsNull() || shape2.IsNull()) {
        result.errorMessage = QStringLiteral("One or both shapes are null");
        return result;
    }

    try {
        BRepAlgoAPI_Common common(shape1, shape2);
        if (common.IsDone()) {
            result.shape = common.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Intersection operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during intersection operation");
    }

    return result;
}

// =====================================================================
//  Shape Modification
// =====================================================================

OperationResult filletShape(
    const TopoDS_Shape& shape,
    double radius,
    const QVector<int>& edgeIndices)
{
    OperationResult result;

    if (shape.IsNull()) {
        result.errorMessage = QStringLiteral("Shape is null");
        return result;
    }

    if (radius <= 0) {
        result.errorMessage = QStringLiteral("Fillet radius must be positive");
        return result;
    }

    try {
        BRepFilletAPI_MakeFillet fillet(shape);

        if (edgeIndices.isEmpty()) {
            // Fillet all edges
            for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
                fillet.Add(radius, TopoDS::Edge(exp.Current()));
            }
        } else {
            // Fillet specific edges
            QVector<TopoDS_Edge> edges;
            for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
                edges.append(TopoDS::Edge(exp.Current()));
            }

            for (int idx : edgeIndices) {
                if (idx >= 0 && idx < edges.size()) {
                    fillet.Add(radius, edges[idx]);
                }
            }
        }

        fillet.Build();
        if (fillet.IsDone()) {
            result.shape = fillet.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Fillet operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during fillet operation");
    }

    return result;
}

OperationResult chamferShape(
    const TopoDS_Shape& shape,
    double distance,
    const QVector<int>& edgeIndices)
{
    OperationResult result;

    if (shape.IsNull()) {
        result.errorMessage = QStringLiteral("Shape is null");
        return result;
    }

    if (distance <= 0) {
        result.errorMessage = QStringLiteral("Chamfer distance must be positive");
        return result;
    }

    try {
        BRepFilletAPI_MakeChamfer chamfer(shape);

        // Collect all edges and their adjacent faces
        QVector<TopoDS_Edge> edges;
        QVector<TopoDS_Face> adjacentFaces;

        for (TopExp_Explorer edgeExp(shape, TopAbs_EDGE); edgeExp.More(); edgeExp.Next()) {
            TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());
            edges.append(edge);

            // Find an adjacent face for this edge
            TopoDS_Face adjacentFace;
            for (TopExp_Explorer faceExp(shape, TopAbs_FACE); faceExp.More(); faceExp.Next()) {
                TopoDS_Face face = TopoDS::Face(faceExp.Current());
                for (TopExp_Explorer edgeOnFace(face, TopAbs_EDGE); edgeOnFace.More(); edgeOnFace.Next()) {
                    if (edgeOnFace.Current().IsSame(edge)) {
                        adjacentFace = face;
                        break;
                    }
                }
                if (!adjacentFace.IsNull()) break;
            }
            adjacentFaces.append(adjacentFace);
        }

        if (edgeIndices.isEmpty()) {
            // Chamfer all edges (symmetric chamfer: same distance on both sides)
            for (int i = 0; i < edges.size(); ++i) {
                if (!adjacentFaces[i].IsNull()) {
                    chamfer.Add(distance, distance, edges[i], adjacentFaces[i]);
                }
            }
        } else {
            // Chamfer specific edges
            for (int idx : edgeIndices) {
                if (idx >= 0 && idx < edges.size() && !adjacentFaces[idx].IsNull()) {
                    chamfer.Add(distance, distance, edges[idx], adjacentFaces[idx]);
                }
            }
        }

        chamfer.Build();
        if (chamfer.IsDone()) {
            result.shape = chamfer.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Chamfer operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during chamfer operation");
    }

    return result;
}

OperationResult shellShape(
    const TopoDS_Shape& shape,
    double thickness,
    const QVector<int>& facesToRemove)
{
    OperationResult result;

    if (shape.IsNull()) {
        result.errorMessage = QStringLiteral("Shape is null");
        return result;
    }

    if (thickness <= 0) {
        result.errorMessage = QStringLiteral("Shell thickness must be positive");
        return result;
    }

    try {
        // Collect faces to remove (openings)
        TopTools_ListOfShape facesToRemoveList;

        QVector<TopoDS_Face> allFaces;
        for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
            allFaces.append(TopoDS::Face(exp.Current()));
        }

        if (facesToRemove.isEmpty()) {
            // If no faces specified, try to find a "top" face (highest Z centroid)
            // This is a heuristic - user should ideally specify faces
            if (!allFaces.isEmpty()) {
                TopoDS_Face topFace = allFaces[0];
                double maxZ = -1e9;

                for (const TopoDS_Face& face : allFaces) {
                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(face, props);
                    gp_Pnt centroid = props.CentreOfMass();
                    if (centroid.Z() > maxZ) {
                        maxZ = centroid.Z();
                        topFace = face;
                    }
                }

                facesToRemoveList.Append(topFace);
            }
        } else {
            for (int idx : facesToRemove) {
                if (idx >= 0 && idx < allFaces.size()) {
                    facesToRemoveList.Append(allFaces[idx]);
                }
            }
        }

        BRepOffsetAPI_MakeThickSolid thickSolid;
        thickSolid.MakeThickSolidByJoin(shape, facesToRemoveList, -thickness,
                                         1e-3);  // tolerance

        thickSolid.Build();
        if (thickSolid.IsDone()) {
            result.shape = thickSolid.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Shell operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during shell operation");
    }

    return result;
}

OperationResult offsetShape(
    const TopoDS_Shape& shape,
    double distance)
{
    OperationResult result;

    if (shape.IsNull()) {
        result.errorMessage = QStringLiteral("Shape is null");
        return result;
    }

    try {
        BRepOffsetAPI_MakeOffsetShape offset;
        offset.PerformByJoin(shape, distance, 1e-3);  // tolerance

        if (offset.IsDone()) {
            result.shape = offset.Shape();
            result.success = true;
        } else {
            result.errorMessage = QStringLiteral("Offset operation failed");
        }
    } catch (...) {
        result.errorMessage = QStringLiteral("Exception during offset operation");
    }

    return result;
}

// =====================================================================
//  Shape Queries (Implemented)
// =====================================================================

double shapeVolume(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0.0;

    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

double shapeSurfaceArea(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return 0.0;

    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);
    return props.Mass();
}

bool shapeBounds(
    const TopoDS_Shape& shape,
    gp_Pnt& minPt,
    gp_Pnt& maxPt)
{
    if (shape.IsNull()) return false;

    Bnd_Box box;
    BRepBndLib::Add(shape, box);

    if (box.IsVoid()) return false;

    double xmin, ymin, zmin, xmax, ymax, zmax;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    minPt = gp_Pnt(xmin, ymin, zmin);
    maxPt = gp_Pnt(xmax, ymax, zmax);
    return true;
}

gp_Pnt shapeCenterOfMass(const TopoDS_Shape& shape)
{
    if (shape.IsNull()) return gp_Pnt(0, 0, 0);

    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.CentreOfMass();
}

QVector<TopoDS_Face> shapeFaces(const TopoDS_Shape& shape)
{
    QVector<TopoDS_Face> faces;

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        faces.append(TopoDS::Face(exp.Current()));
    }

    return faces;
}

int faceCount(const TopoDS_Shape& shape)
{
    int count = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        ++count;
    }
    return count;
}

int edgeCount(const TopoDS_Shape& shape)
{
    int count = 0;
    for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
        ++count;
    }
    return count;
}

int vertexCount(const TopoDS_Shape& shape)
{
    int count = 0;
    for (TopExp_Explorer exp(shape, TopAbs_VERTEX); exp.More(); exp.Next()) {
        ++count;
    }
    return count;
}

}  // namespace brep
}  // namespace hobbycad
