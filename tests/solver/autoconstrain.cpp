// =====================================================================
//  tests/solver/autoconstrain.cpp — geometric auto-constrain detects the
//  obvious constraints on existing geometry (coincident / H-V / parallel /
//  equal), guarded so it never over-constrains. SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/sketch/entity.h>
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/operations.h>
#include <hobbycad/sketch/solver.h>
#include <cstdio>
#include <vector>
using namespace hobbycad;
using namespace hobbycad::sketch;

static int fails = 0;
static void ck(bool ok, const char* w) { std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++fails; }
static int countType(const std::vector<Constraint>& cs, ConstraintType t) {
    int n = 0; for (const auto& c : cs) if (c.type == t) ++n; return n;
}

int main() {
    installSolverFatalHandler();
    std::printf("geometric auto-constrain\n");

    { // Scalene triangle, corners coincident by position but no constraints.
      std::vector<Entity> es{
          createLine(1, {0, 0},  {12, 0}),   // A: horizontal base
          createLine(2, {12, 0}, {4, 7}),    // B
          createLine(3, {4, 7},  {0, 0}),    // C
      };
      std::vector<Constraint> existing;
      int nextId = 1;
      auto added = autoConstrain(es, existing, nextId);
      const int coin = countType(added, ConstraintType::Coincident);
      const int horiz = countType(added, ConstraintType::Horizontal);
      std::printf("  triangle: coincident=%d horizontal=%d total=%zu\n",
                  coin, horiz, added.size());
      ck(coin == 3, "3 corner coincidences detected");
      ck(horiz == 1, "the horizontal base is detected");
      // ids are assigned and unique
      bool idsOk = true; for (const auto& c : added) if (c.id <= 0) idsOk = false;
      ck(idsOk, "added constraints get positive ids"); }

    { // Two parallel (non-axis) lines of different length.
      std::vector<Entity> es{
          createLine(1, {0, 0}, {10, 5}),   // dir (10,5)
          createLine(2, {0, 3}, {6, 6}),    // dir (6,3): same direction
      };
      std::vector<Constraint> existing;
      int nextId = 1;
      auto added = autoConstrain(es, existing, nextId);
      std::printf("  parallel pair: parallel=%d equal=%d\n",
                  countType(added, ConstraintType::Parallel), countType(added, ConstraintType::Equal));
      ck(countType(added, ConstraintType::Parallel) == 1, "parallel lines detected"); }

    { // Two horizontal equal-length lines: H+H (parallel redundant, dropped) + Equal.
      std::vector<Entity> es{
          createLine(1, {0, 0}, {10, 0}),
          createLine(2, {0, 5}, {10, 5}),
      };
      std::vector<Constraint> existing;
      int nextId = 1;
      auto added = autoConstrain(es, existing, nextId);
      std::printf("  two horizontals: horizontal=%d parallel=%d equal=%d\n",
                  countType(added, ConstraintType::Horizontal),
                  countType(added, ConstraintType::Parallel),
                  countType(added, ConstraintType::Equal));
      ck(countType(added, ConstraintType::Horizontal) == 2, "both horizontals detected");
      ck(countType(added, ConstraintType::Parallel) == 0,
         "redundant Parallel dropped by the over-constrain guard"); }

    if (fails == 0) std::printf("autoconstrain: ALL PASS\n");
    else            std::printf("autoconstrain: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
