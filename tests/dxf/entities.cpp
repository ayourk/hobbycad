// =====================================================================
//  tests/dxf/entities.cpp — each DXF entity type decodes correctly
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/dxf_import.h>
#include <cstdio>
#include <cmath>
#include <string>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 1e-4) { return std::fabs(a - b) < e; }

// Wrap an ENTITIES body in a minimal DXF document.
static std::string doc(const std::string& body) {
    return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}
static int countType(const DXFImportResult& r, EntityType t) {
    int n = 0; for (const auto& e : r.entities) if (e.type == t) ++n; return n;
}

int main() {
    std::printf("DXF entity decoding\n");

    { // LINE
        auto r = importDXFString(doc("0\nLINE\n8\n0\n10\n0\n20\n0\n11\n10\n21\n5\n"));
        ck(r.success && r.entities.size() == 1 && r.entities[0].type == EntityType::Line, "LINE parses");
        ck(near(r.entities[0].points[0].x, 0) && near(r.entities[0].points[1].x, 10)
           && near(r.entities[0].points[1].y, 5), "LINE endpoints");
    }
    { // CIRCLE
        auto r = importDXFString(doc("0\nCIRCLE\n8\n0\n10\n3\n20\n4\n40\n2.5\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Circle, "CIRCLE parses");
        ck(near(r.entities[0].points[0].x, 3) && near(r.entities[0].points[0].y, 4)
           && near(r.entities[0].radius, 2.5), "CIRCLE center+radius");
    }
    { // ARC (quarter circle, radius 5 at origin)
        auto r = importDXFString(doc("0\nARC\n8\n0\n10\n0\n20\n0\n40\n5\n50\n0\n51\n90\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Arc, "ARC parses");
        ck(near(r.entities[0].radius, 5), "ARC radius");
    }
    { // POINT
        auto r = importDXFString(doc("0\nPOINT\n8\n0\n10\n7\n20\n8\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Point
           && near(r.entities[0].points[0].x, 7) && near(r.entities[0].points[0].y, 8), "POINT parses");
    }
    { // LWPOLYLINE open, 3 vertices -> 2 lines
        auto r = importDXFString(doc("0\nLWPOLYLINE\n8\n0\n90\n3\n70\n0\n"
                                     "10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n"));
        ck(countType(r, EntityType::Line) == 2, "open LWPOLYLINE -> 2 lines");
    }
    { // LWPOLYLINE closed square, 4 vertices -> 4 lines
        auto r = importDXFString(doc("0\nLWPOLYLINE\n8\n0\n90\n4\n70\n1\n"
                                     "10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n10\n0\n20\n10\n"));
        ck(countType(r, EntityType::Line) == 4, "closed LWPOLYLINE -> 4 lines");
    }
    { // LWPOLYLINE with a bulge (semicircle over one segment) -> 1 arc
        auto r = importDXFString(doc("0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n"
                                     "10\n0\n20\n0\n42\n1\n10\n10\n20\n0\n"));
        ck(countType(r, EntityType::Arc) == 1, "bulge segment -> 1 arc");
    }
    { // Old-style POLYLINE, 2 vertices -> 1 line
        auto r = importDXFString(doc("0\nPOLYLINE\n8\n0\n70\n0\n"
                                     "0\nVERTEX\n8\n0\n10\n0\n20\n0\n"
                                     "0\nVERTEX\n8\n0\n10\n4\n20\n3\n"
                                     "0\nSEQEND\n8\n0\n"));
        ck(countType(r, EntityType::Line) == 1, "old-style POLYLINE -> 1 line");
    }
    { // ELLIPSE axis-aligned (major along X, ratio 0.5)
        auto r = importDXFString(doc("0\nELLIPSE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n40\n0.5\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Ellipse, "ELLIPSE parses");
    }
    { // SPLINE degree 3 with 4 control points -> exact cubic Bezier
        auto r = importDXFString(doc("0\nSPLINE\n8\n0\n71\n3\n"
                                     "10\n0\n20\n0\n10\n1\n20\n2\n10\n3\n20\n2\n10\n4\n20\n0\n"));
        ck(countType(r, EntityType::Spline) == 1, "SPLINE parses");
        ck(r.entities.size() == 1 && r.entities[0].splineBezier && r.entities[0].points.size() == 4,
           "degree-3 4-control-point SPLINE imports as an editable Bezier spline");
    }
    { // SPLINE degree 3 with 5 control points (not 3N+1) -> Catmull-Rom fallback
        auto r = importDXFString(doc("0\nSPLINE\n8\n0\n71\n3\n"
                                     "10\n0\n20\n0\n10\n1\n20\n2\n10\n2\n20\n2\n10\n3\n20\n1\n10\n4\n20\n0\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Spline && !r.entities[0].splineBezier,
           "degree-3 non-3N+1 SPLINE stays Catmull-Rom");
    }
    { // TEXT
        auto r = importDXFString(doc("0\nTEXT\n8\n0\n10\n1\n20\n2\n40\n5\n1\nHello\n"));
        ck(r.entities.size() == 1 && r.entities[0].type == EntityType::Text
           && r.entities[0].text == "Hello", "TEXT parses with content");
    }

    if (fails == 0) std::printf("dxf entities: ALL PASS\n");
    else            std::printf("dxf entities: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
