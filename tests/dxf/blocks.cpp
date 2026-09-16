// =====================================================================
//  tests/dxf/blocks.cpp — BLOCKS table + INSERT expansion
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  INSERT is instantiated from the BLOCKS table: translation, uniform and
//  non-uniform scale, rotation, base-point subtraction, MINSERT arrays and
//  nested block references. A depth limit and an entity cap bound hostile
//  files (a block-reference cycle, a huge MINSERT array).
#include <hobbycad/sketch/dxf_import.h>
#include <cstdio>
#include <cmath>
#include <string>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static bool near(double a, double b, double e = 1e-3) { return std::fabs(a - b) < e; }
static bool hasWarn(const DXFImportResult& r, const std::string& sub) {
    for (const auto& w : r.warnings) if (w.find(sub) != std::string::npos) return true;
    return false;
}
static int countType(const DXFImportResult& r, EntityType t) {
    int n = 0; for (const auto& e : r.entities) if (e.type == t) ++n; return n;
}
// A document with a BLOCKS section then an ENTITIES section.
static std::string blockDoc(const std::string& blocks, const std::string& ents) {
    return "0\nSECTION\n2\nBLOCKS\n" + blocks + "0\nENDSEC\n"
           "0\nSECTION\n2\nENTITIES\n" + ents + "0\nENDSEC\n0\nEOF\n";
}
// A BLOCK..ENDBLK wrapper: name, base point, body.
static std::string block(const std::string& name, double bx, double by, const std::string& body) {
    return "0\nBLOCK\n2\n" + name + "\n10\n" + std::to_string(bx) + "\n20\n" +
           std::to_string(by) + "\n" + body + "0\nENDBLK\n";
}

int main() {
    std::printf("DXF BLOCKS/INSERT expansion\n");

    { // Translation: a block line, inserted at (10,0)
      auto r = importDXFString(blockDoc(
          block("MB", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n"),
          "0\nINSERT\n8\n0\n2\nMB\n10\n10\n20\n0\n"));
      ck(r.success && r.entities.size() == 1 && r.entities[0].type == EntityType::Line,
         "INSERT expands a block's LINE");
      ck(!r.entities.empty() && near(r.entities[0].points[0].x, 10) &&
         near(r.entities[0].points[1].x, 15),
         "insertion point translates the block geometry"); }

    { // Uniform scale: a circle r=3, inserted at scale 2 -> r=6
      auto r = importDXFString(blockDoc(
          block("MC", 0, 0, "0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n3\n"),
          "0\nINSERT\n8\n0\n2\nMC\n10\n20\n20\n0\n41\n2\n42\n2\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].radius, 6) &&
         near(r.entities[0].points[0].x, 20),
         "INSERT scale scales the radius and moves the center"); }

    { // Rotation 90 deg: a horizontal line becomes vertical
      auto r = importDXFString(blockDoc(
          block("MR", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n10\n21\n0\n"),
          "0\nINSERT\n8\n0\n2\nMR\n10\n0\n20\n0\n50\n90\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[1].x, 0) &&
         near(std::fabs(r.entities[0].points[1].y), 10),
         "INSERT rotation turns the geometry"); }

    { // Base point: subtracted before placement
      auto r = importDXFString(blockDoc(
          block("BP", 5, 0, "0\nLINE\n8\n0\n10\n5\n20\n0\n11\n5\n21\n5\n"),
          "0\nINSERT\n8\n0\n2\nBP\n10\n0\n20\n0\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, 0) &&
         near(r.entities[0].points[0].y, 0) && near(r.entities[0].points[1].y, 5),
         "block base point is subtracted"); }

    { // MINSERT array: 3 columns x 2 rows -> 6 copies
      auto r = importDXFString(blockDoc(
          block("MA", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n"),
          "0\nINSERT\n8\n0\n2\nMA\n10\n0\n20\n0\n70\n3\n71\n2\n44\n10\n45\n10\n"));
      ck(r.entities.size() == 6, "MINSERT array expands cols*rows copies"); }

    { // Nested blocks: OUTER inserts INNER; entities inserts OUTER
      auto r = importDXFString(blockDoc(
          block("INNER", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n2\n21\n0\n") +
          block("OUTER", 0, 0, "0\nINSERT\n8\n0\n2\nINNER\n10\n3\n20\n0\n"),
          "0\nINSERT\n8\n0\n2\nOUTER\n10\n0\n20\n0\n"));
      ck(r.entities.size() == 1 && near(r.entities[0].points[0].x, 3),
         "nested INSERT (block referencing a block) is resolved"); }

    { // Cycle gate: A inserts B, B inserts A -> terminates, warns depth
      auto r = importDXFString(blockDoc(
          block("A", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n"
                           "0\nINSERT\n8\n0\n2\nB\n10\n0\n20\n0\n") +
          block("B", 0, 0, "0\nINSERT\n8\n0\n2\nA\n10\n0\n20\n0\n"),
          "0\nINSERT\n8\n0\n2\nA\n10\n0\n20\n0\n"));
      ck(r.success && hasWarn(r, "nesting deeper"),
         "a block-reference cycle is stopped by the depth limit + warned"); }

    { // Undefined block: recorded, warned, nothing emitted
      auto r = importDXFString(blockDoc("", "0\nINSERT\n8\n0\n2\nGHOST\n10\n0\n20\n0\n"));
      ck(r.entities.empty() && hasWarn(r, "undefined block"),
         "INSERT of an undefined block is warned, not expanded"); }

    { // importBlocks = false: name recorded, not expanded
      DXFImportOptions o; o.importBlocks = false;
      auto r = importDXFString(blockDoc(
          block("MB", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n5\n21\n0\n"),
          "0\nINSERT\n8\n0\n2\nMB\n10\n0\n20\n0\n"), 1, o);
      ck(r.entities.empty() && r.blocks.size() == 1 && hasWarn(r, "block import disabled"),
         "importBlocks=false records the name but does not expand"); }

    { // Entity cap: a MINSERT bomb (1000x1000) is truncated at the cap
      auto r = importDXFString(blockDoc(
          block("MB", 0, 0, "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n"),
          "0\nINSERT\n8\n0\n2\nMB\n10\n0\n20\n0\n70\n1000\n71\n1000\n44\n1\n45\n1\n"));
      ck(r.success && r.entities.size() <= 200000 && r.entities.size() >= 100000 &&
         hasWarn(r, "safety cap"),
         "a MINSERT bomb is bounded by the entity cap + warned"); }

    { // Non-uniform scale warns
      auto r = importDXFString(blockDoc(
          block("MC", 0, 0, "0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n3\n"),
          "0\nINSERT\n8\n0\n2\nMC\n10\n0\n20\n0\n41\n2\n42\n3\n"));
      ck(hasWarn(r, "non-uniform"), "non-uniform INSERT scale is warned"); }

    { // A block ARC keeps a valid arc after a scaled+rotated INSERT
      auto r = importDXFString(blockDoc(
          block("MK", 0, 0, "0\nARC\n8\n0\n10\n0\n20\n0\n40\n5\n50\n0\n51\n90\n"),
          "0\nINSERT\n8\n0\n2\nMK\n10\n0\n20\n0\n41\n2\n42\n2\n50\n45\n"));
      ck(countType(r, EntityType::Arc) == 1 && near(r.entities[0].radius, 10),
         "a block ARC re-derives a correct radius under scale (not left stale)"); }

    if (fails == 0) std::printf("dxf blocks: ALL PASS\n");
    else            std::printf("dxf blocks: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
