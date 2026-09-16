// =====================================================================
//  tests/dxf/robustness.cpp — malformed / edge-case DXF must not crash
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The old scanner parsed every coordinate with an unguarded std::stod, so
//  one bad value threw straight out of the import. These pin graceful,
//  non-throwing behavior on the messy inputs real files contain.
#include <hobbycad/sketch/dxf_import.h>
#include <cstdio>
#include <string>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool hasWarn(const DXFImportResult& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true;
    return false;
}
static std::string doc(const std::string& body) {
    return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n";
}

int main() {
    std::printf("DXF robustness\n");

    { auto r = importDXFString("");
      ck(!r.success && !r.errorMessage.empty(), "empty content -> error, no crash"); }

    { auto r = importDXFString("   \n  \n");
      ck(!r.success, "whitespace-only -> error"); }

    { auto r = importDXFString("AutoCAD Binary DXF\r\n\x1a\x00", 1);
      ck(!r.success && r.errorMessage.find("Binary") != std::string::npos, "binary DXF rejected clearly"); }

    { // CRLF line endings throughout
      auto r = importDXFString("0\r\nSECTION\r\n2\r\nENTITIES\r\n0\r\nLINE\r\n8\r\n0\r\n"
                               "10\r\n0\r\n20\r\n0\r\n11\r\n5\r\n21\r\n0\r\n0\r\nENDSEC\r\n0\r\nEOF\r\n");
      ck(r.success && r.entities.size() == 1, "CRLF line endings parse"); }

    { // 999 comments interspersed
      auto r = importDXFString(doc("999\na comment\n0\nLINE\n8\n0\n999\nmid\n10\n0\n20\n0\n11\n3\n21\n0\n"));
      ck(r.entities.size() == 1, "999 comments ignored"); }

    { // stray blank lines and extra whitespace around values
      auto r = importDXFString("0\nSECTION\n2\nENTITIES\n\n0\n  LINE  \n8\n0\n10\n  1.5 \n20\n0\n"
                               "11\n2\n21\n0\n0\nENDSEC\n0\nEOF\n");
      ck(r.entities.size() == 1, "blank lines / padded values tolerated"); }

    { // Only a HEADER, no ENTITIES section
      auto r = importDXFString("0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1027\n0\nENDSEC\n0\nEOF\n");
      ck(r.success && r.entities.empty(), "no ENTITIES -> success, zero entities"); }

    { // Unknown entity type is skipped, neighbors survive
      auto r = importDXFString(doc("0\nHATCH\n8\n0\n2\nSOLID\n"
                                   "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n1\n"));
      ck(r.entities.size() == 1 && hasWarn(r, "unsupported entity"), "unknown entity skipped + warned"); }

    { // Malformed coordinate value must NOT throw; falls back to default
      auto r = importDXFString(doc("0\nLINE\n8\n0\n10\nnot_a_number\n20\n0\n11\n5\n21\n0\n"));
      ck(r.success && r.entities.size() == 1, "malformed coordinate does not crash"); }

    { // Dangling code at EOF (code with no value line)
      auto r = importDXFString("0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n10\n");
      ck(r.success, "dangling trailing code does not crash"); }

    { // A non-integer group-code line is dropped with a warning, not fatal
      auto r = importDXFString("0\nSECTION\n2\nENTITIES\nxyz\ngarbage\n0\nLINE\n8\n0\n"
                               "10\n0\n20\n0\n11\n2\n21\n0\n0\nENDSEC\n0\nEOF\n");
      ck(r.entities.size() == 1 && hasWarn(r, "non-integer group code"), "bad group-code line dropped + warned"); }

    // --- Security: the exact patterns behind real DXF-import CVEs ---

    { // dxflib CVE-2021-21897: a bulge (code 42) BEFORE any vertex (code 10)
      // drove a -1 index -> heap OOB write. We accumulate then push, so a
      // pre-vertex bulge cannot index at -1. Must not crash.
      auto r = importDXFString(doc("0\nLWPOLYLINE\n8\n0\n70\n0\n"
                                   "42\n0.5\n10\n0\n20\n0\n10\n10\n20\n0\n"));
      ck(r.success, "bulge-before-vertex (CVE-2021-21897 shape) does not crash"); }

    { // A lying vertex-count header (code 90) must not drive allocation: we
      // ignore 90 and grow from the actual 10/20 pairs. Huge count, 2 verts.
      auto r = importDXFString(doc("0\nLWPOLYLINE\n8\n0\n90\n999999999\n70\n0\n"
                                   "10\n0\n20\n0\n10\n5\n20\n0\n"));
      ck(r.success && r.entities.size() == 1, "lying huge vertex count does not over-allocate/crash"); }

    { // Truncated file: no ENDSEC, no EOF, entity cut off mid-stream. The read
      // loop must terminate on end-of-input, never hang.
      auto r = importDXFString("0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n");
      ck(r.success, "missing ENDSEC/EOF terminates, no hang"); }

    if (fails == 0) std::printf("dxf robustness: ALL PASS\n");
    else            std::printf("dxf robustness: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
