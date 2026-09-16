// =====================================================================
//  tests/dxf/harden.cpp — Tier-1 hardening: text decode, recover, linetype
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/dxf_import.h>
#include <cstdio>
#include <string>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }
static bool warn(const DXFImportResult& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true; return false; }
static std::string doc(const std::string& body) { return "0\nSECTION\n2\nENTITIES\n" + body + "0\nENDSEC\n0\nEOF\n"; }
static const Entity* firstText(const DXFImportResult& r) {
    for (const auto& e : r.entities) if (e.type == EntityType::Text) return &e; return nullptr; }

int main() {
    std::printf("DXF hardening (text decode / recover / linetype)\n");

    { // \U+XXXX unicode -> UTF-8 (bounds-safe)
      auto r = importDXFString(doc("0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n5\n1\n45\\U+00B5\n"));
      const Entity* t = firstText(r);
      ck(t && t->text == std::string("45\xC2\xB5"), "\\U+00B5 -> UTF-8 micro sign"); }

    { // %%d -> degree
      auto r = importDXFString(doc("0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n5\n1\n90%%d\n"));
      const Entity* t = firstText(r);
      ck(t && t->text == std::string("90\xC2\xB0"), "%%d -> degree sign"); }

    { // %%c and %%p
      auto r = importDXFString(doc("0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n5\n1\n%%c10 %%p1\n"));
      const Entity* t = firstText(r);
      ck(t && has(t->text, "\xC3\x98") && has(t->text, "\xC2\xB1"), "%%c/%%p -> diameter/plus-minus"); }

    { // MTEXT inline codes stripped, \P -> newline, braces dropped, \f arg skipped
      auto r = importDXFString(doc("0\nMTEXT\n8\n0\n10\n0\n20\n0\n40\n5\n1\n{\\fArial|b0;Hello}\\PWorld\n"));
      const Entity* t = firstText(r);
      ck(t && t->text == std::string("Hello\nWorld"), "MTEXT: braces/\\f stripped, \\P -> newline"); }

    { // truncated \U+ must not over-read (bounds safety)
      auto r = importDXFString(doc("0\nTEXT\n8\n0\n10\n0\n20\n0\n40\n5\n1\nX\\U+\n"));
      const Entity* t = firstText(r);
      ck(t != nullptr, "truncated \\U+ does not crash / over-read"); }

    { // recover: an entity with NO SECTION/ENTITIES framing
      auto r = importDXFString("0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n0\nEOF\n");
      ck(r.entities.size() == 1 && warn(r, "outside an ENTITIES section"),
         "entity outside ENTITIES is recovered + warned"); }

    { // CONSTRUCTION layer -> isConstruction (not skipped, default options)
      auto r = importDXFString(doc("0\nLINE\n8\nCONSTRUCTION\n10\n0\n20\n0\n11\n1\n21\n1\n"));
      ck(r.entities.size() == 1 && r.entities[0].isConstruction,
         "CONSTRUCTION layer -> isConstruction"); }

    { // CENTER linetype -> isCenterline
      auto r = importDXFString(doc("0\nLINE\n8\n0\n6\nCENTER\n10\n0\n20\n0\n11\n1\n21\n1\n"));
      ck(r.entities.size() == 1 && r.entities[0].isCenterline,
         "CENTER linetype -> isCenterline"); }

    if (fails == 0) std::printf("dxf harden: ALL PASS\n");
    else            std::printf("dxf harden: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
