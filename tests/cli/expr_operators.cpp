// =====================================================================
//  tests/cli/expr_operators.cpp — Path A: comparison / logical / ternary
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The minimal Qt-free "fallback condition language" added to the shared
//  expression evaluator. Each case fails meaningfully if the operator layer
//  regresses:
//    * comparisons and logical ops yield C-style 1.0 / 0.0,
//    * precedence matches C (relational > equality > && > || > ?:),
//    * && / || / ?: SHORT-CIRCUIT so a dead branch cannot throw,
//    * suppression is SCOPED: a live div-by-zero / unknown name still errors,
//    * a syntax error inside a dead branch still propagates.
#include <cstdio>
#include <cmath>
#include <map>
#include <string>

#include "hobbycad/parameters.h"

using hobbycad::evaluateExpression;
static int fails = 0;
static void ck(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

static bool ev(const char* e, double& r,
               const std::map<std::string, double>& p = {}) {
    std::string err;
    return evaluateExpression(e, r, p, &err);
}
static bool eq(const char* e, double want,
               const std::map<std::string, double>& p = {}) {
    double r = 0.0;
    return ev(e, r, p) && std::fabs(r - want) < 1e-9;
}

int main() {
    // ---- comparisons yield 1.0 / 0.0 -----------------------------------
    ck(eq("3 < 5", 1.0), "3 < 5 == 1");
    ck(eq("5 < 3", 0.0), "5 < 3 == 0");
    ck(eq("5 >= 5", 1.0), "5 >= 5 == 1");
    ck(eq("5 > 5", 0.0), "5 > 5 == 0");
    ck(eq("4 <= 3", 0.0), "4 <= 3 == 0");
    ck(eq("2 == 2", 1.0), "2 == 2 == 1");
    ck(eq("2 != 2", 0.0), "2 != 2 == 0");
    ck(eq("2 != 3", 1.0), "2 != 3 == 1");

    // ---- logical ops + unary not ---------------------------------------
    ck(eq("1 && 1", 1.0), "1 && 1 == 1");
    ck(eq("1 && 0", 0.0), "1 && 0 == 0");
    ck(eq("0 || 0", 0.0), "0 || 0 == 0");
    ck(eq("0 || 3", 1.0), "0 || 3 == 1 (normalized)");
    ck(eq("!0", 1.0), "!0 == 1");
    ck(eq("!5", 0.0), "!5 == 0");
    ck(eq("!(2 > 3)", 1.0), "!(2 > 3) == 1");

    // ---- precedence (C-style) ------------------------------------------
    ck(eq("2 + 3 > 4", 1.0), "additive binds tighter than relational");
    ck(eq("2 < 3 && 3 < 4", 1.0), "relational binds tighter than &&");
    ck(eq("1 || 0 && 0", 1.0), "&& binds tighter than || (1 || (0&&0))");
    ck(eq("1 == 1 && 2 == 2", 1.0), "equality binds tighter than &&");

    // ---- ternary --------------------------------------------------------
    ck(eq("(5 > 3) ? 10 : 20", 10.0), "true ? 10 : 20 == 10");
    ck(eq("(5 < 3) ? 10 : 20", 20.0), "false ? 10 : 20 == 20");
    ck(eq("1 ? 2 ? 3 : 4 : 5", 3.0), "nested ternary right-assoc");
    ck(eq("a > b ? a : b", 7.0, {{"a", 3}, {"b", 7}}), "ternary max(a,b)");

    // ---- SHORT-CIRCUIT: dead branch must not throw ---------------------
    ck(eq("(x != 0) && (1/x > 0.1)", 0.0, {{"x", 0}}),
       "x==0: && short-circuits, no div-by-zero");
    ck(eq("(x == 0) || (1/x > 0.1)", 1.0, {{"x", 0}}),
       "x==0: || short-circuits, no div-by-zero");
    ck(eq("(x == 0) ? 0 : (1/x)", 0.0, {{"x", 0}}),
       "x==0: ?: skips 1/x, no throw");
    ck(eq("(x == 0) ? 0 : (1/x)", 0.5, {{"x", 2}}),
       "x==2: ?: takes 1/x == 0.5");
    ck(eq("0 && nosuch", 0.0), "&& skips unknown identifier in dead branch");
    ck(eq("1 || nosuch", 1.0), "|| skips unknown identifier in dead branch");

    // ---- Path A statement layer: variables, ; , blocks, bif/belse, loops -
    ck(eq("x = 40 ; x + 2", 42.0), "assignment + sequencing");
    ck(eq("bif (3 > 2) { 100 } belse { 200 }", 100.0), "bif true branch");
    ck(eq("bif (3 < 2) { 100 } belse { 200 }", 200.0), "bif else branch");
    ck(eq("bif (0) { 7 }", 0.0), "bif false, no belse -> 0");
    ck(eq("x = 5 ; bif (x > 0) { x = x * 2 } ; x", 10.0), "bif with side effect");
    ck(eq("s = 0 ; bfor (i = 0; i < 4; i = i + 1) { s = s + i } ; s", 6.0),
       "bfor sums 0+1+2+3 == 6");
    ck(eq("n = 0 ; bwhile (n < 5) { n = n + 1 } ; n", 5.0), "bwhile counts to 5");
    ck(eq("n = 0 ; bdo { n = n + 1 } bwhile (n < 3) ; n", 3.0),
       "bdo runs body then checks");
    ck(eq("n = 0 ; bdo { n = n + 1 } bwhile (0) ; n", 1.0),
       "bdo runs body at least once");
    ck(eq("t = 0 ; bfor (i = 1; i <= 3; i = i + 1) "
          "{ bfor (j = 1; j <= 3; j = j + 1) { t = t + 1 } } ; t", 9.0),
       "nested bfor -> 9");
    ck(eq("bfor (i = 0; i < 10; i = i + 1) { }", 0.0),
       "empty bfor body terminates, value 0");
    // if() function was removed: ?: for a value, bif for control flow
    { double r = 0.0; std::string err;
      ck(!evaluateExpression("if(1, 2, 3)", r, {}, &err),
         "removed if() function now errors"); }
    // runaway-loop guard trips instead of hanging
    { double r = 0.0; std::string err;
      ck(!evaluateExpression("bwhile (1) { }", r, {}, &err),
         "infinite bwhile hits the iteration cap"); }

    // ---- math helpers: clamp (new); min/max/floor/ceil already existed ---
    ck(eq("clamp(5, 0, 10)", 5.0), "clamp within range");
    ck(eq("clamp(-3, 0, 10)", 0.0), "clamp below -> lo");
    ck(eq("clamp(99, 0, 10)", 10.0), "clamp above -> hi");
    ck(eq("clamp(v, lo, hi)", 4.0, {{"v", 4}, {"lo", 1}, {"hi", 8}}),
       "clamp with parameters");
    ck(eq("min(3, 7)", 3.0), "min(a,b)");
    ck(eq("max(3, 7)", 7.0), "max(a,b)");
    ck(eq("min(3, 7, 1, 9)", 1.0), "variadic min");
    ck(eq("max(3, 7, 1, 9)", 9.0), "variadic max");
    ck(eq("floor(2.9)", 2.0), "floor");
    ck(eq("ceil(2.1)", 3.0), "ceil");

    // ---- hyperbolic + reciprocal trig (new) ----------------------------
    ck(eq("sinh(0)", 0.0), "sinh(0) == 0");
    ck(eq("cosh(0)", 1.0), "cosh(0) == 1");
    ck(eq("tanh(0)", 0.0), "tanh(0) == 0");
    ck(eq("cosh(1.3)*cosh(1.3) - sinh(1.3)*sinh(1.3)", 1.0),
       "cosh^2 - sinh^2 == 1 (hyperbolic identity)");
    ck(eq("sec(0)", 1.0), "sec(0 deg) == 1");
    ck(eq("sec(60)", 2.0), "sec(60 deg) == 2");
    ck(eq("csc(30)", 2.0), "csc(30 deg) == 2");
    ck(eq("csc(90)", 1.0), "csc(90 deg) == 1");
    ck(eq("cot(45)", 1.0), "cot(45 deg) == 1");

    // ---- completing the standard 24: inverse-recip / recip-hyp / inv-hyp -
    ck(eq("asec(2)", 60.0), "asec(2) == 60 deg");
    ck(eq("acsc(2)", 30.0), "acsc(2) == 30 deg");
    ck(eq("acot(1)", 45.0), "acot(1) == 45 deg");
    ck(eq("sec(asec(2))", 2.0), "sec(asec(2)) round-trips");
    ck(eq("cot(acot(3))", 3.0), "cot(acot(3)) round-trips");
    ck(eq("sech(0)", 1.0), "sech(0) == 1");
    ck(eq("sech(1)*sech(1) + tanh(1)*tanh(1)", 1.0), "sech^2 + tanh^2 == 1");
    ck(eq("coth(1.2)*coth(1.2) - csch(1.2)*csch(1.2)", 1.0), "coth^2 - csch^2 == 1");
    ck(eq("asinh(0)", 0.0), "asinh(0) == 0");
    ck(eq("acosh(1)", 0.0), "acosh(1) == 0");
    ck(eq("sinh(asinh(2))", 2.0), "sinh(asinh(2)) round-trips");
    ck(eq("cosh(acosh(3))", 3.0), "cosh(acosh(3)) round-trips");
    ck(eq("tanh(atanh(0.5))", 0.5), "tanh(atanh(0.5)) round-trips");
    ck(eq("csch(acsch(2))", 2.0), "csch(acsch(2)) round-trips");
    ck(eq("sech(asech(0.5))", 0.5), "sech(asech(0.5)) round-trips");
    ck(eq("coth(acoth(3))", 3.0), "coth(acoth(3)) round-trips");

    // ---- suppression is SCOPED: live errors still fail -----------------
    {
        double r;
        ck(!ev("1/0", r), "bare 1/0 still errors (suppression not global)");
        ck(!ev("nosuch + 1", r), "bare unknown name still errors");
        ck(!ev("1 ? 2 : (3 +", r),
           "syntax error in dead branch still propagates");
        ck(!ev("(1 > 0) ? (1/0) : 0", r),
           "div-by-zero in the TAKEN branch still errors");
    }

    if (fails == 0) std::printf("expr_operators: ALL PASS\n");
    else            std::printf("expr_operators: %d FAILURE(S)\n", fails);
    return fails ? 1 : 0;
}
