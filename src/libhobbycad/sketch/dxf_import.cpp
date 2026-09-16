// =====================================================================
//  src/libhobbycad/sketch/dxf_import.cpp — DXF import (parser)
// =====================================================================
//
//  See dxf_import.h. ASCII DXF only; robust (non-throwing) group-code
//  reader; HEADER units; OCS via the Arbitrary Axis Algorithm; warnings for
//  under-specified/unsupported constructs.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#include <cstdio>
#include <hobbycad/sketch/queries.h>
#include <hobbycad/units.h>
#include "hobbycad/sketch/dxf_import.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>

namespace hobbycad {
namespace sketch {

namespace {

// ---- small string helpers -------------------------------------------

std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Non-throwing numeric parses. On failure they return the default AND report
// through `ok` so the caller can raise a warning; the parser never throws.
double toDouble(const std::string& s, double def, bool* ok = nullptr) {
    const char* p = s.c_str();
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    const bool good = (end != p);
    if (ok) *ok = good;
    return good ? v : def;
}

long toLong(const std::string& s, long def, bool* ok = nullptr) {
    const char* p = s.c_str();
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    const bool good = (end != p);
    if (ok) *ok = good;
    return good ? v : def;
}

// $INSUNITS -> millimeters per unit, or 0 when the code is unitless/unknown.
double insUnitsToMm(long u) {
    switch (u) {
    case 1:  return 25.4;        // inches
    case 2:  return 304.8;       // feet
    case 4:  return 1.0;         // millimeters
    case 5:  return 10.0;        // centimeters
    case 6:  return 1000.0;      // meters
    case 8:  return 25.4e-6;     // microinches
    case 9:  return 25.4e-3;     // mils
    case 10: return 914.4;       // yards
    case 13: return 1e-6;        // nanometers
    case 14: return 100.0;       // decimeters  (1 dm = 100 mm)
    default: return 0.0;         // 0 = unitless, or a unit we do not convert
    }
}

// ---- OCS via the Arbitrary Axis Algorithm ---------------------------
//
// A DXF entity stores 2D coordinates in its Object Coordinate System, whose
// axes are derived from the extrusion vector N. This is the AutoCAD
// Arbitrary Axis Algorithm (DXF spec). We flatten to the world XY plane for
// a 2D sketch: the common N=(0,0,1) is identity and N=(0,0,-1) is the classic
// X mirror. A non-planar extrusion (|Nz| != 1) is flattened with a warning.
struct Ocs {
    double ax[3] = {1, 0, 0};
    double ay[3] = {0, 1, 0};
    double n[3]  = {0, 0, 1};
    bool orientationReversed = false;  // does the XY flatten reverse 2D winding?
    bool planarToXY = true;            // |Nz| ~ 1 (nothing lost by flattening)

    Point2D toWorld(const Point2D& p, double elev = 0.0) const {
        return { ax[0] * p.x + ay[0] * p.y + n[0] * elev,
                 ax[1] * p.x + ay[1] * p.y + n[1] * elev };
    }
};

Ocs makeOcs(double nx, double ny, double nz) {
    // Shared Arbitrary Axis Algorithm (types.h): one implementation for the
    // DXF OCS here and for no-history sketch planes.
    const PlaneBasis b = arbitraryAxisBasis(Vec3(static_cast<float>(nx),
                                                 static_cast<float>(ny),
                                                 static_cast<float>(nz)));
    Ocs o;
    o.ax[0] = b.uAxis.x; o.ax[1] = b.uAxis.y; o.ax[2] = b.uAxis.z;
    o.ay[0] = b.vAxis.x; o.ay[1] = b.vAxis.y; o.ay[2] = b.vAxis.z;
    o.n[0]  = b.normal.x; o.n[1] = b.normal.y; o.n[2] = b.normal.z;
    const double det2d = o.ax[0] * o.ay[1] - o.ax[1] * o.ay[0];
    o.orientationReversed = det2d < 0.0;
    o.planarToXY = std::fabs(o.n[2]) > 1.0 - geometry::kZeroEps;
    return o;
}

// ---- group-code stream ----------------------------------------------

struct Pair { int code; std::string value; };

// A cursor over pre-tokenized (code,value) pairs.
class Reader {
public:
    Reader(std::vector<Pair> pairs) : m_pairs(std::move(pairs)) {}
    bool atEnd() const { return m_pos >= m_pairs.size(); }
    const Pair& peek() const { return m_pairs[m_pos]; }
    const Pair& next() { return m_pairs[m_pos++]; }
    void step() { ++m_pos; }
private:
    std::vector<Pair> m_pairs;
    size_t m_pos = 0;
};

// Tokenize a DXF document into (code,value) pairs. Skips 999 comments and
// blank code lines; a non-integer code line is reported and skipped so one
// bad line cannot derail the rest.
std::vector<Pair> tokenize(const std::string& content, std::vector<std::string>& warnings) {
    std::vector<Pair> pairs;
    std::istringstream in(content);
    std::string codeLine;
    int badCodes = 0;
    while (std::getline(in, codeLine)) {
        const std::string cs = trimmed(codeLine);
        if (cs.empty()) continue;               // stray blank line
        bool ok = false;
        const long code = toLong(cs, 0, &ok);
        std::string valueLine;
        if (!std::getline(in, valueLine)) break;  // dangling code at EOF
        const std::string val = trimmed(valueLine);
        if (!ok) { ++badCodes; continue; }        // not a group code: drop the pair
        if (code == 999) continue;                // comment
        pairs.push_back({ static_cast<int>(code), val });
    }
    if (badCodes)
        warnings.push_back("skipped " + std::to_string(badCodes) +
                           " line(s) with a non-integer group code");
    return pairs;
}

// Collect the group codes of one entity: every pair up to (not including) the
// next code-0 record. The cursor is left on that next code-0.
std::vector<Pair> entityGroups(Reader& r) {
    std::vector<Pair> g;
    while (!r.atEnd() && r.peek().code != 0) g.push_back(r.next());
    return g;
}

double groupD(const std::vector<Pair>& g, int code, double def, bool* found = nullptr) {
    for (const Pair& p : g) if (p.code == code) { if (found) *found = true; return toDouble(p.value, def); }
    if (found) *found = false;
    return def;
}
long groupL(const std::vector<Pair>& g, int code, long def, bool* found = nullptr) {
    for (const Pair& p : g) if (p.code == code) { if (found) *found = true; return toLong(p.value, def); }
    if (found) *found = false;
    return def;
}
std::string groupS(const std::vector<Pair>& g, int code, const std::string& def) {
    for (const Pair& p : g) if (p.code == code) return p.value;
    return def;
}

Ocs ocsFromGroups(const std::vector<Pair>& g) {
    return makeOcs(groupD(g, 210, 0.0), groupD(g, 220, 0.0), groupD(g, 230, 1.0));
}

}  // namespace

// ---- ACI (AutoCAD Color Index) -> RGB -------------------------------------
// The fixed 256-entry AutoCAD default palette (there is no formula). Values
// transcribed from ezdxf's DXF_DEFAULT_COLORS (reference/ezdxf-master); ACI 7
// resolves to white here (the dark-background default). ACI 0 (BYBLOCK), 256
// (BYLAYER) and out-of-range indices have no fixed RGB and return -1 (default).
static int aciToRgb(int aci) {
    static const int kAci[256] = {
    0x000000, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFFFFFF,
    0x808080, 0xC0C0C0, 0xFF0000, 0xFF7F7F, 0xA50000, 0xA55252, 0x7F0000, 0x7F3F3F,
    0x4C0000, 0x4C2626, 0x260000, 0x261313, 0xFF3F00, 0xFF9F7F, 0xA52900, 0xA56752,
    0x7F1F00, 0x7F4F3F, 0x4C1300, 0x4C2F26, 0x260900, 0x261713, 0xFF7F00, 0xFFBF7F,
    0xA55200, 0xA57C52, 0x7F3F00, 0x7F5F3F, 0x4C2600, 0x4C3926, 0x261300, 0x261C13,
    0xFFBF00, 0xFFDF7F, 0xA57C00, 0xA59152, 0x7F5F00, 0x7F6F3F, 0x4C3900, 0x4C4226,
    0x261C00, 0x262113, 0xFFFF00, 0xFFFF7F, 0xA5A500, 0xA5A552, 0x7F7F00, 0x7F7F3F,
    0x4C4C00, 0x4C4C26, 0x262600, 0x262613, 0xBFFF00, 0xDFFF7F, 0x7CA500, 0x91A552,
    0x5F7F00, 0x6F7F3F, 0x394C00, 0x424C26, 0x1C2600, 0x212613, 0x7FFF00, 0xBFFF7F,
    0x52A500, 0x7CA552, 0x3F7F00, 0x5F7F3F, 0x264C00, 0x394C26, 0x132600, 0x1C2613,
    0x3FFF00, 0x9FFF7F, 0x29A500, 0x67A552, 0x1F7F00, 0x4F7F3F, 0x134C00, 0x2F4C26,
    0x092600, 0x172613, 0x00FF00, 0x7FFF7F, 0x00A500, 0x52A552, 0x007F00, 0x3F7F3F,
    0x004C00, 0x264C26, 0x002600, 0x132613, 0x00FF3F, 0x7FFF9F, 0x00A529, 0x52A567,
    0x007F1F, 0x3F7F4F, 0x004C13, 0x264C2F, 0x002609, 0x135817, 0x00FF7F, 0x7FFFBF,
    0x00A552, 0x52A57C, 0x007F3F, 0x3F7F5F, 0x004C26, 0x264C39, 0x002613, 0x13581C,
    0x00FFBF, 0x7FFFDF, 0x00A57C, 0x52A591, 0x007F5F, 0x3F7F6F, 0x004C39, 0x264C42,
    0x00261C, 0x135858, 0x00FFFF, 0x7FFFFF, 0x00A5A5, 0x52A5A5, 0x007F7F, 0x3F7F7F,
    0x004C4C, 0x264C4C, 0x002626, 0x135858, 0x00BFFF, 0x7FDFFF, 0x007CA5, 0x5291A5,
    0x005F7F, 0x3F6F7F, 0x00394C, 0x26427E, 0x001C26, 0x135858, 0x007FFF, 0x7FBFFF,
    0x0052A5, 0x527CA5, 0x003F7F, 0x3F5F7F, 0x00264C, 0x26397E, 0x001326, 0x131C58,
    0x003FFF, 0x7F9FFF, 0x0029A5, 0x5267A5, 0x001F7F, 0x3F4F7F, 0x00134C, 0x262F7E,
    0x000926, 0x131758, 0x0000FF, 0x7F7FFF, 0x0000A5, 0x5252A5, 0x00007F, 0x3F3F7F,
    0x00004C, 0x26267E, 0x000026, 0x131358, 0x3F00FF, 0x9F7FFF, 0x2900A5, 0x6752A5,
    0x1F007F, 0x4F3F7F, 0x13004C, 0x2F267E, 0x090026, 0x171358, 0x7F00FF, 0xBF7FFF,
    0x5200A5, 0x7C52A5, 0x3F007F, 0x5F3F7F, 0x26004C, 0x39267E, 0x130026, 0x1C1358,
    0xBF00FF, 0xDF7FFF, 0x7C00A5, 0x9152A5, 0x5F007F, 0x6F3F7F, 0x39004C, 0x42264C,
    0x1C0026, 0x581358, 0xFF00FF, 0xFF7FFF, 0xA500A5, 0xA552A5, 0x7F007F, 0x7F3F7F,
    0x4C004C, 0x4C264C, 0x260026, 0x581358, 0xFF00BF, 0xFF7FDF, 0xA5007C, 0xA55291,
    0x7F005F, 0x7F3F6F, 0x4C0039, 0x4C2642, 0x26001C, 0x581358, 0xFF007F, 0xFF7FBF,
    0xA50052, 0xA5527C, 0x7F003F, 0x7F3F5F, 0x4C0026, 0x4C2639, 0x260013, 0x58131C,
    0xFF003F, 0xFF7F9F, 0xA50029, 0xA55267, 0x7F001F, 0x7F3F4F, 0x4C0013, 0x4C262F,
    0x260009, 0x581317, 0x000000, 0x656565, 0x666666, 0x999999, 0xCCCCCC, 0xFFFFFF,
    };
    if (aci >= 1 && aci <= 255) return kAci[aci];
    return -1;   // 0 = BYBLOCK, 256 = BYLAYER, or unknown -> caller's default
}

// ---- DXF text decoding (bounds-safe) --------------------------------------
// Strips MTEXT inline formatting, decodes \U+XXXX unicode escapes and the old
// %%d/%%c/%%p TEXT codes to UTF-8. Every index is checked; a truncated or
// out-of-range escape yields U+FFFD rather than over-reading.
static void appendUtf8(std::string& out, unsigned cp) {
    if (cp > 0x10FFFF) cp = 0xFFFD;
    if (cp <= 0x7F) { out += static_cast<char>(cp); }
    else if (cp <= 0x7FF) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp <= 0xFFFF) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
}
static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static std::string decodeDxfText(const std::string& s) {
    std::string out; out.reserve(s.size());
    const size_t n = s.size();
    for (size_t i = 0; i < n; ) {
        const char c = s[i];
        if (c == '{' || c == '}') { i++; continue; }            // MTEXT group braces
        if (c == '%' && i + 2 < n && s[i + 1] == '%') {          // old TEXT escapes
            const char code = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i + 2])));
            i += 3;
            switch (code) {
            case 'd': appendUtf8(out, 0x00B0); break;            // degree
            case 'c': appendUtf8(out, 0x00D8); break;            // diameter
            case 'p': appendUtf8(out, 0x00B1); break;            // plus/minus
            case '%': out += '%'; break;
            default:  out += '%'; out += s[i - 1]; break;
            }
            continue;
        }
        if (c == '\\' && i + 1 < n) {
            const char nx = s[i + 1];
            if ((nx == 'U' || nx == 'u') && i + 2 < n && s[i + 2] == '+') {
                size_t j = i + 3; unsigned cp = 0; int nd = 0;
                while (j < n && nd < 4) { const int h = hexVal(s[j]); if (h < 0) break; cp = cp * 16 + static_cast<unsigned>(h); j++; nd++; }
                if (nd > 0) { appendUtf8(out, cp); i = j; } else { appendUtf8(out, 0xFFFD); i += 3; }
                continue;
            }
            if (nx == 'P') { out += '\n'; i += 2; continue; }      // paragraph
            if (nx == '~') { appendUtf8(out, 0x00A0); i += 2; continue; }  // NBSP
            if (nx == '\\' || nx == '{' || nx == '}') { out += nx; i += 2; continue; }
            if (std::string("fFHCcQWTpAS").find(nx) != std::string::npos) {   // arg up to ';'
                size_t j = i + 2;
                if (nx == 'S') {                                  // stacking: keep the parts
                    std::string frac;
                    while (j < n && s[j] != ';') { const char f = s[j]; if (f != '^' && f != '/' && f != '#') frac += f; j++; }
                    if (j < n) j++; out += frac; i = j; continue;
                }
                while (j < n && s[j] != ';') j++;
                if (j < n) j++; i = j; continue;
            }
            if (std::string("LlOoKk").find(nx) != std::string::npos) { i += 2; continue; }  // toggles
            out += nx; i += 2; continue;                          // unknown escape: drop backslash
        }
        out += c; i++;
    }
    return out;
}

// Convert polyline vertices (already placed in sketch coordinates) plus per-
// segment bulges into Line/Arc entities. Bulge sign is expected already
// corrected for any OCS orientation flip by the caller.
static void emitPolyline(std::vector<Entity>& out, std::vector<std::string>& warnings,
                         int& nextId, const std::vector<Point2D>& verts,
                         const std::vector<double>& bulges, bool closed) {
    if (verts.size() < 2) {
        if (verts.size() == 1) warnings.push_back("polyline with a single vertex skipped");
        return;
    }
    const int n = static_cast<int>(verts.size());
    const int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i) {
        const Point2D& p1 = verts[i];
        const Point2D& p2 = verts[(i + 1) % n];
        const double bulge = (i < static_cast<int>(bulges.size())) ? bulges[i] : 0.0;
        const Point2D chord = p2 - p1;
        const double chordLen = std::sqrt(chord.x * chord.x + chord.y * chord.y);
        if (std::abs(bulge) < 1e-10 || chordLen < geometry::kExactEps) {
            out.push_back(createLine(nextId++, p1, p2));
        } else {
            // bulge = tan(theta/4); the arc's apex is the chord midpoint pushed
            // out by the sagitta along the chord normal. Three points survive
            // any earlier OCS transform.
            const Point2D mid = (p1 + p2) * 0.5;
            const Point2D perp(-chord.y / chordLen, chord.x / chordLen);
            const double sagitta = bulge * chordLen / 2.0;
            const Point2D apex = mid + perp * sagitta;
            out.push_back(createArcFromThreePoints(nextId++, p1, apex, p2));
        }
    }
}

// =====================================================================

DXFImportResult importDXFString(const std::string& dxfContent, int startId,
                                const DXFImportOptions& options) {
    DXFImportResult result;

    if (trimmed(dxfContent).empty()) {
        result.errorMessage = "Empty DXF content";
        return result;
    }
    if (dxfContent.rfind("AutoCAD Binary DXF", 0) == 0) {
        result.errorMessage = "Binary DXF is not supported; export as ASCII DXF";
        return result;
    }

    std::vector<Pair> pairs = tokenize(dxfContent, result.warnings);

    // Coordinate scale: $INSUNITS -> millimeters (HEADER precedes ENTITIES, so
    // read it up front) times the caller's scale. The format is often silent
    // about units, which is exactly the kind of gap we surface as a warning.
    double unitScale = 1.0;
    if (options.autoUnitScale) {
        // $INSUNITS is authoritative; when it is 0/absent, $MEASUREMENT
        // (0 = imperial, 1 = metric) is the fallback hint before we assume mm.
        auto headerInt = [&](const char* name, bool* found) -> long {
            for (size_t i = 0; i < pairs.size(); ++i)
                if (pairs[i].code == 9 && pairs[i].value == name)
                    for (size_t j = i + 1; j < pairs.size() && pairs[j].code != 9; ++j)
                        if (pairs[j].code == 70) { if (found) *found = true; return toLong(pairs[j].value, 0); }
            if (found) *found = false;
            return 0;
        };
        bool haveIns = false;
        const long ins = headerInt("$INSUNITS", &haveIns);
        const double mm = haveIns ? insUnitsToMm(ins) : 0.0;
        if (mm > 0) {
            unitScale = mm;
        } else {
            bool haveMeas = false;
            const long meas = headerInt("$MEASUREMENT", &haveMeas);
            if (haveMeas && meas == 0) {
                unitScale = 25.4;
                result.warnings.push_back("no $INSUNITS; $MEASUREMENT=0 (imperial): assuming inches");
            } else if (haveMeas && meas == 1) {
                result.warnings.push_back("no $INSUNITS; $MEASUREMENT=1 (metric): assuming millimeters");
            } else if (haveIns) {
                result.warnings.push_back("DXF units unitless/unknown ($INSUNITS=" +
                                          std::to_string(ins) + "); assuming millimeters");
            } else {
                result.warnings.push_back("DXF has no $INSUNITS/$MEASUREMENT; assuming millimeters");
            }
        }
    }

    // S: unit scale times the caller's scale. Applied to every coordinate and
    // to radii/heights. options.offset is applied once, at the very end, so the
    // instance transforms below stay pure (a translation would otherwise be
    // rotated/scaled by an INSERT).
    const double S = unitScale * options.scale;

    Reader r(std::move(pairs));
    int nextId = startId;
    std::string section;
    std::string curBlock;   // upper() block name while inside a BLOCK..ENDBLK

    // ---- block table & INSERT references --------------------------------
    struct InsertRef {
        std::string block;                    // referenced block name
        Point2D pos;                          // insertion point, S-scaled, in the enclosing space
        double sx = 1.0, sy = 1.0, rot = 0.0; // codes 41/42, 50 (degrees)
        int cols = 1, rows = 1;               // codes 70/71 (MINSERT array)
        double colSp = 0.0, rowSp = 0.0;      // codes 44/45, S-scaled
    };
    struct RawEnt {
        std::string kind;
        std::vector<Pair> g;                  // groups (non-polyline entities)
        bool isPoly = false;                  // POLYLINE / LWPOLYLINE
        double nx = 0, ny = 0, nz = 1;        // polyline OCS normal
        std::vector<Point2D> verts;           // raw (unplaced) polyline verts
        std::vector<double> bulges;
        bool closed = false;
    };
    struct BlockDef {
        Point2D base;                         // S-scaled base point (code 10/20 of BLOCK)
        std::vector<RawEnt> ents;
        std::vector<InsertRef> inserts;
    };
    std::map<std::string, BlockDef> blockDefs;

    auto addLayer = [&](const std::string& lyr) {
        if (std::find(result.layers.begin(), result.layers.end(), lyr) == result.layers.end())
            result.layers.push_back(lyr);
    };
    auto recordBlockName = [&](const std::string& b) {
        if (!b.empty() &&
            std::find(result.blocks.begin(), result.blocks.end(), b) == result.blocks.end())
            result.blocks.push_back(b);
    };
    bool nonPlanarWarned = false;
    bool recoverWarned = false;

    // The active instance transform for block expansion (identity for the
    // top-level ENTITIES section). place()/placeWcs() compose it and gInstScale
    // scales radii/heights, so block geometry is REBUILT under the transform:
    // arcs and circles re-derive correct centers/radii/angles rather than being
    // transformed in place (Entity::transform does not touch those scalars).
    geometry::Transform2D gInst = geometry::Transform2D::identity();
    double gInstScale = 1.0;

    auto place = [&](const Ocs& o, const Point2D& ocsPt) {
        return gInst.apply(o.toWorld(ocsPt) * S);
    };
    auto placeWcs = [&](const Point2D& wcsPt) {
        return gInst.apply(wcsPt * S);
    };

    // Layer/linetype -> the flags our model stores. Applied over the entities a
    // single record added ([before, out.size())).
    auto markFlags = [&](std::vector<Entity>& out, size_t before, const std::vector<Pair>& g) {
        const std::string entLinetype = upper(groupS(g, 6, ""));
        const std::string layerUp = upper(groupS(g, 8, "0"));
        const bool mc = (layerUp.rfind("CONSTR", 0) == 0 || layerUp == "DEFPOINTS");
        const bool ml = (entLinetype.find("CENTER") != std::string::npos ||
                         entLinetype.find("DASHDOT") != std::string::npos);
        // Color: true color (code 420, 0xRRGGBB) overrides an ACI index
        // (code 62); BYLAYER/BYBLOCK and absent leave the default (-1).
        int col = -1;
        bool has420 = false; const long tc = groupL(g, 420, 0, &has420);
        if (has420) col = static_cast<int>(tc & 0xFFFFFF);
        else { bool has62 = false; const long aci = groupL(g, 62, 0, &has62); if (has62) col = aciToRgb(static_cast<int>(aci)); }
        for (size_t k = before; k < out.size(); ++k) {
            if (mc) out[k].isConstruction = true;
            if (ml) out[k].isCenterline   = true;
            if (col >= 0) out[k].color = col;
        }
    };

    auto parseLwVerts = [&](const std::vector<Pair>& g, std::vector<Point2D>& verts,
                            std::vector<double>& bulges, bool& closed) {
        closed = (groupL(g, 70, 0) & 1) != 0;
        bool have = false; Point2D cur; double curBulge = 0;
        for (const Pair& pr : g) {
            if (pr.code == 10) {
                if (have) { verts.push_back(cur); bulges.push_back(curBulge); curBulge = 0; }
                cur.x = toDouble(pr.value, 0.0); have = true;
            } else if (pr.code == 20) {
                cur.y = toDouble(pr.value, 0.0);
            } else if (pr.code == 42) {
                curBulge = toDouble(pr.value, 0.0);
            }
        }
        if (have) { verts.push_back(cur); bulges.push_back(curBulge); }
    };

    // Emit a polyline (raw, unplaced verts) into `out`, placing through the
    // current instance transform and OCS.
    auto emitPoly = [&](const Ocs& o, std::vector<Point2D> verts,
                        std::vector<double> bulges, bool closed, std::vector<Entity>& out) {
        std::vector<Point2D> placed;
        placed.reserve(verts.size());
        for (const Point2D& v : verts) placed.push_back(place(o, v));
        if (o.orientationReversed) for (double& b : bulges) b = -b;
        emitPolyline(out, result.warnings, nextId, placed, bulges, closed);
    };

    // Parse the reader-driven old-style POLYLINE into raw (unplaced) verts.
    auto parsePolyline = [&](std::vector<Point2D>& verts, std::vector<double>& bulges,
                             bool& closed, Ocs& o, std::string& layer, std::vector<Pair>& hdrOut) {
        const std::vector<Pair> hdr = entityGroups(r);
        hdrOut = hdr;
        layer  = groupS(hdr, 8, "0");
        closed = (groupL(hdr, 70, 0) & 1) != 0;
        o = ocsFromGroups(hdr);
        if (!o.planarToXY && !nonPlanarWarned) {
            result.warnings.push_back("entity on a non-XY plane flattened to the sketch");
            nonPlanarWarned = true;
        }
        while (!r.atEnd()) {
            const Pair v = r.next();
            if (v.code != 0) continue;
            const std::string vk = upper(v.value);
            if (vk == "VERTEX") {
                const std::vector<Pair> vg = entityGroups(r);
                verts.push_back({ groupD(vg, 10, 0.0), groupD(vg, 20, 0.0) });
                bulges.push_back(groupD(vg, 42, 0.0));
            } else if (vk == "SEQEND") {
                entityGroups(r);
                break;
            } else {
                break;   // unexpected inside a POLYLINE
            }
        }
    };

    // Build an InsertRef from an INSERT's groups. pos is placed with the
    // identity instance transform, i.e. S-scaled + OCS in the enclosing space;
    // the parent transform maps it to the world during expansion.
    auto makeInsert = [&](const std::vector<Pair>& g) -> InsertRef {
        InsertRef ref;
        ref.block = groupS(g, 2, "");
        const Ocs o = ocsFromGroups(g);
        const geometry::Transform2D savedI = gInst; const double savedS = gInstScale;
        gInst = geometry::Transform2D::identity(); gInstScale = 1.0;
        ref.pos = place(o, { groupD(g, 10, 0.0), groupD(g, 20, 0.0) });
        gInst = savedI; gInstScale = savedS;
        ref.sx  = groupD(g, 41, 1.0);
        ref.sy  = groupD(g, 42, 1.0);
        ref.rot = groupD(g, 50, 0.0);
        ref.cols = static_cast<int>(std::max<long>(1, groupL(g, 70, 1)));
        ref.rows = static_cast<int>(std::max<long>(1, groupL(g, 71, 1)));
        ref.colSp = groupD(g, 44, 0.0) * S;
        ref.rowSp = groupD(g, 45, 0.0) * S;
        return ref;
    };

    // Build one geometry entity (LINE..MTEXT; not POLYLINE/LWPOLYLINE/INSERT)
    // into `out`, through the active instance transform.
    auto parseGeom = [&](const std::string& kind, const std::vector<Pair>& g,
                         std::vector<Entity>& out) {
        const Ocs o = ocsFromGroups(g);
        if (!o.planarToXY && !nonPlanarWarned &&
            (kind == "LINE" || kind == "CIRCLE" || kind == "ARC" ||
             kind == "ELLIPSE" || kind == "POINT")) {
            result.warnings.push_back("entity on a non-XY plane flattened to the sketch");
            nonPlanarWarned = true;
        }
        if (kind == "LINE") {
            const Point2D a = place(o, { groupD(g, 10, 0.0), groupD(g, 20, 0.0) });
            const Point2D b = place(o, { groupD(g, 11, 0.0), groupD(g, 21, 0.0) });
            out.push_back(createLine(nextId++, a, b));
        }
        else if (kind == "CIRCLE") {
            const Point2D c = place(o, { groupD(g, 10, 0.0), groupD(g, 20, 0.0) });
            const double rad = groupD(g, 40, 0.0) * S * gInstScale;
            if (rad > 0) out.push_back(createCircle(nextId++, c, rad));
            else result.warnings.push_back("CIRCLE with non-positive radius skipped");
        }
        else if (kind == "ARC") {
            const Point2D center(groupD(g, 10, 0.0), groupD(g, 20, 0.0));
            const double rad = groupD(g, 40, 0.0);
            const double sa = degreesToRadians(groupD(g, 50, 0.0));
            const double ea = degreesToRadians(groupD(g, 51, 360.0));
            if (rad <= 0) { result.warnings.push_back("ARC with non-positive radius skipped"); return; }
            double sweep = ea - sa;
            while (sweep <= 0) sweep += 2 * M_PI;
            const double ma = sa + sweep / 2.0;
            auto onArc = [&](double ang) {
                return place(o, { center.x + rad * std::cos(ang), center.y + rad * std::sin(ang) });
            };
            out.push_back(createArcFromThreePoints(nextId++, onArc(sa), onArc(ma), onArc(ea)));
        }
        else if (kind == "ELLIPSE") {
            const Point2D c = placeWcs({ groupD(g, 10, 0.0), groupD(g, 20, 0.0) });
            const Point2D majEndW = placeWcs({ groupD(g, 10, 0.0) + groupD(g, 11, 1.0),
                                              groupD(g, 20, 0.0) + groupD(g, 21, 0.0) });
            const double ratio = groupD(g, 40, 1.0);
            const double dx = majEndW.x - c.x, dy = majEndW.y - c.y;
            const double majorR = std::sqrt(dx * dx + dy * dy);
            const double minorR = majorR * ratio;
            const double rotDeg = radiansToDegrees(std::atan2(dy, dx));  // major-axis angle
            // Elliptical-arc range: codes 41/42 are start/end parameters in
            // radians, measured from the major axis. Stored as start + sweep
            // in degrees (full ellipse = 360).
            const double p0 = groupD(g, 41, 0.0);
            const double p1 = groupD(g, 42, 6.283185307179586);
            double sweepRad = p1 - p0;
            while (sweepRad <= geometry::kZeroEps) sweepRad += 2.0 * M_PI;   // CCW, wrap to (0, 2pi]
            if (majorR > 0) {
                Entity el = createEllipse(nextId++, c, majorR, minorR, rotDeg);
                el.ellipseStart = radiansToDegrees(p0);
                el.ellipseSweep = radiansToDegrees(sweepRad);
                out.push_back(el);
            }
        }
        else if (kind == "POINT") {
            out.push_back(createPoint(nextId++, place(o, { groupD(g, 10, 0.0), groupD(g, 20, 0.0) })));
        }
        else if (kind == "SPLINE") {
            std::vector<Point2D> ctrl, fit;
            bool haveX = false; Point2D cx; bool haveFX = false; Point2D fx;
            int degree = 0;
            for (const Pair& pr : g) {
                switch (pr.code) {
                case 10: if (haveX) ctrl.push_back(placeWcs(cx)); cx.x = toDouble(pr.value, 0.0); haveX = true; break;
                case 20: cx.y = toDouble(pr.value, 0.0); break;
                case 11: if (haveFX) fit.push_back(placeWcs(fx)); fx.x = toDouble(pr.value, 0.0); haveFX = true; break;
                case 21: fx.y = toDouble(pr.value, 0.0); break;
                case 71: degree = static_cast<int>(toDouble(pr.value, 0.0)); break;  // spline degree
                }
            }
            if (haveX) ctrl.push_back(placeWcs(cx));
            if (haveFX) fit.push_back(placeWcs(fx));
            // A degree-3 SPLINE whose control points number 3N+1 is a piecewise
            // cubic Bezier (its control points ARE the Bezier control polygon),
            // so import it as an editable Bezier spline (exact, a solver curve).
            // Anything else keeps the prior behavior (Catmull-Rom through the
            // control points, or the fit points when no control points are given).
            if (degree == 3 && ctrl.size() >= 4 && (ctrl.size() - 1) % 3 == 0) {
                out.push_back(createBezierSpline(nextId++, ctrl));
            } else {
                const std::vector<Point2D>& pts = !ctrl.empty() ? ctrl : fit;
                if (pts.size() >= 2) out.push_back(createSpline(nextId++, pts));
                else result.warnings.push_back("SPLINE with too few points skipped");
            }
        }
        else if (kind == "TEXT" || kind == "MTEXT") {
            std::string text;
            for (const Pair& pr : g) if (pr.code == 3) text += pr.value;
            text += groupS(g, 1, "");
            text = decodeDxfText(text);
            const Point2D pos = place(o, { groupD(g, 10, 0.0), groupD(g, 20, 0.0) });
            const double h = groupD(g, 40, 12.0) * S * gInstScale;
            const double rot = groupD(g, 50, 0.0);
            if (!text.empty())
                out.push_back(createText(nextId++, pos, text, "", h, false, false, rot));
        }
        else {
            result.warnings.push_back("unsupported entity '" + kind + "' skipped");
        }
    };

    // Recursively expand an INSERT into result.entities. Guarded by a nesting-
    // depth limit and a total-entity cap so a hostile MINSERT array or a
    // block-reference cycle cannot exhaust memory.
    const int kMaxDepth = 16;
    const size_t kEntityCap = 200000;
    bool capWarned = false, depthWarned = false, missingWarned = false, nonUniformWarned = false;
    std::function<void(const InsertRef&, const geometry::Transform2D&, double, int)> expandInsert =
        [&](const InsertRef& ins, const geometry::Transform2D& parent, double parentScale, int depth) {
        if (depth > kMaxDepth) {
            if (!depthWarned) {
                result.warnings.push_back("INSERT nesting deeper than " + std::to_string(kMaxDepth) +
                                          " levels; expansion truncated");
                depthWarned = true;
            }
            return;
        }
        auto it = blockDefs.find(upper(ins.block));
        if (it == blockDefs.end()) {
            if (!missingWarned) {
                result.warnings.push_back("INSERT references undefined block '" + ins.block + "'");
                missingWarned = true;
            }
            return;
        }
        const BlockDef& bd = it->second;
        if (std::fabs(ins.sx - ins.sy) > geometry::kZeroEps && !nonUniformWarned) {
            result.warnings.push_back("non-uniform INSERT scale approximated (radii use the mean scale)");
            nonUniformWarned = true;
        }
        const int cols = std::max(1, ins.cols), rows = std::max(1, ins.rows);
        const geometry::Transform2D rotOnly = geometry::Transform2D::rotation(ins.rot);
        const double cellScale = std::sqrt(std::fabs(ins.sx * ins.sy));
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                if (result.entities.size() >= kEntityCap) {
                    if (!capWarned) {
                        result.warnings.push_back("block expansion reached the " +
                            std::to_string(kEntityCap) + "-entity safety cap; truncated");
                        capWarned = true;
                    }
                    return;
                }
                const Point2D arr = rotOnly.apply({ col * ins.colSp, row * ins.rowSp });
                const geometry::Transform2D cell =
                      geometry::Transform2D::translation(ins.pos.x + arr.x, ins.pos.y + arr.y)
                    * geometry::Transform2D::rotation(ins.rot)
                    * geometry::Transform2D::scale(ins.sx, ins.sy)
                    * geometry::Transform2D::translation(-bd.base.x, -bd.base.y);
                const geometry::Transform2D total = parent * cell;
                const double totalScale = parentScale * cellScale;
                gInst = total; gInstScale = totalScale;
                for (const RawEnt& re : bd.ents) {
                    if (result.entities.size() >= kEntityCap) {
                        if (!capWarned) {
                            result.warnings.push_back("block expansion reached the " +
                                std::to_string(kEntityCap) + "-entity safety cap; truncated");
                            capWarned = true;
                        }
                        gInst = geometry::Transform2D::identity(); gInstScale = 1.0;
                        return;
                    }
                    const size_t before = result.entities.size();
                    if (re.isPoly) {
                        const Ocs o = makeOcs(re.nx, re.ny, re.nz);
                        emitPoly(o, re.verts, re.bulges, re.closed, result.entities);
                        markFlags(result.entities, before, re.g);
                    } else {
                        parseGeom(re.kind, re.g, result.entities);
                        markFlags(result.entities, before, re.g);
                    }
                }
                gInst = geometry::Transform2D::identity(); gInstScale = 1.0;
                for (const InsertRef& nested : bd.inserts)
                    expandInsert(nested, total, totalScale, depth + 1);
            }
        }
    };

    static const char* kKnown[] = {"LINE","CIRCLE","ARC","ELLIPSE","POINT",
        "LWPOLYLINE","POLYLINE","SPLINE","TEXT","MTEXT","INSERT"};

    while (!r.atEnd()) {
        const Pair p = r.next();
        if (p.code != 0) continue;   // resync to the next record boundary

        const std::string kind = upper(p.value);

        if (kind == "SECTION") {
            if (!r.atEnd() && r.peek().code == 2) section = upper(r.next().value);
            else section.clear();
            curBlock.clear();
            continue;
        }
        if (kind == "ENDSEC") { section.clear(); curBlock.clear(); continue; }
        if (kind == "EOF") break;

        // ---- BLOCKS: build the block table (no geometry emitted here) ----
        if (section == "BLOCKS") {
            if (kind == "BLOCK") {
                const std::vector<Pair> g = entityGroups(r);
                curBlock = upper(groupS(g, 2, ""));
                BlockDef& bd = blockDefs[curBlock];
                bd.base = { groupD(g, 10, 0.0) * S, groupD(g, 20, 0.0) * S };
                continue;
            }
            if (kind == "ENDBLK") { entityGroups(r); curBlock.clear(); continue; }
            if (curBlock.empty()) {                 // stray record between blocks
                if (kind != "POLYLINE") entityGroups(r);
                continue;
            }
            BlockDef& bd = blockDefs[curBlock];
            if (kind == "POLYLINE") {
                RawEnt re; re.isPoly = true; re.kind = "POLYLINE";
                Ocs o; std::string lyr;
                parsePolyline(re.verts, re.bulges, re.closed, o, lyr, re.g);
                re.nx = o.n[0]; re.ny = o.n[1]; re.nz = o.n[2];
                bd.ents.push_back(std::move(re));
                continue;
            }
            const std::vector<Pair> g = entityGroups(r);
            if (kind == "INSERT") {
                InsertRef ref = makeInsert(g);
                recordBlockName(ref.block);
                bd.inserts.push_back(ref);
                continue;
            }
            if (kind == "LWPOLYLINE") {
                RawEnt re; re.isPoly = true; re.kind = "LWPOLYLINE";
                const Ocs o = ocsFromGroups(g);
                re.nx = o.n[0]; re.ny = o.n[1]; re.nz = o.n[2];
                parseLwVerts(g, re.verts, re.bulges, re.closed);
                re.g = g;
                bd.ents.push_back(std::move(re));
                continue;
            }
            bool known = false;
            for (const char* k : kKnown) if (kind == k) { known = true; break; }
            if (known) { RawEnt re; re.kind = kind; re.g = g; bd.ents.push_back(std::move(re)); }
            continue;
        }

        // ---- ENTITIES (or a recovered stray entity) ----
        if (section != "ENTITIES") {
            bool isEntity = false;
            for (const char* k : kKnown) if (kind == k) { isEntity = true; break; }
            if (!isEntity) continue;
            if (!recoverWarned) {
                result.warnings.push_back("entity found outside an ENTITIES section; recovered");
                recoverWarned = true;
            }
        }

        if (kind == "POLYLINE") {
            std::vector<Point2D> verts; std::vector<double> bulges; bool closed = false;
            Ocs o; std::string layer; std::vector<Pair> hdr;
            parsePolyline(verts, bulges, closed, o, layer, hdr);
            addLayer(layer);
            const size_t before = result.entities.size();
            emitPoly(o, verts, bulges, closed, result.entities);
            markFlags(result.entities, before, hdr);
            continue;
        }

        const std::vector<Pair> g = entityGroups(r);
        const std::string layer = groupS(g, 8, "0");

        if (options.ignoreConstructionLayers) {
            const std::string lu = upper(layer);
            if (lu == "DEFPOINTS" || lu.rfind("CONSTR", 0) == 0) continue;
        }
        if (!options.layerFilter.empty()) {
            bool keep = false;
            for (const std::string& f : options.layerFilter)
                if (upper(f) == upper(layer)) { keep = true; break; }
            if (!keep) continue;
        }
        addLayer(layer);

        if (kind == "INSERT") {
            InsertRef ref = makeInsert(g);
            recordBlockName(ref.block);
            if (!options.importBlocks) {
                result.warnings.push_back("INSERT of block '" + ref.block +
                                          "' not expanded (block import disabled)");
            } else {
                expandInsert(ref, geometry::Transform2D::identity(), 1.0, 0);
            }
            continue;
        }

        if (kind == "LWPOLYLINE") {
            const Ocs o = ocsFromGroups(g);
            if (!o.planarToXY && !nonPlanarWarned) {
                result.warnings.push_back("entity on a non-XY plane flattened to the sketch");
                nonPlanarWarned = true;
            }
            std::vector<Point2D> verts; std::vector<double> bulges; bool closed = false;
            parseLwVerts(g, verts, bulges, closed);
            const size_t before = result.entities.size();
            emitPoly(o, verts, bulges, closed, result.entities);
            markFlags(result.entities, before, g);
            continue;
        }

        const size_t before = result.entities.size();
        parseGeom(kind, g, result.entities);
        markFlags(result.entities, before, g);
    }

    // options.offset is a single global translation applied once (see S above).
    if (options.offset.x != 0.0 || options.offset.y != 0.0) {
        const geometry::Transform2D off =
            geometry::Transform2D::translation(options.offset.x, options.offset.y);
        for (Entity& e : result.entities) e = e.transformed(off);
    }

    result.success = true;
    result.entityCount = static_cast<int>(result.entities.size());
    result.bounds = sketchBounds(result.entities);
    if (result.entityCount == 0 && result.errorMessage.empty())
        result.errorMessage = "No supported entities found in DXF";
    return result;
}

DXFImportResult importDXFFile(const std::string& filePath, int startId,
                              const DXFImportOptions& options) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        DXFImportResult r;
        r.errorMessage = "Cannot open file: " + filePath;
        return r;
    }
    std::string content((std::istreambuf_iterator<char>(file)), {});
    return importDXFString(content, startId, options);
}

}  // namespace sketch
}  // namespace hobbycad
