// =====================================================================
//  tests/project/parameter_names.cpp — what makes a parameter name
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  The parameters dialog kept its own name rules: any Unicode letter, and
//  a shorter reserved list than the expression evaluator's. It accepted
//  names ("längd", "tau") that an expression could then not use. It now
//  asks ParameterEngine::checkName, which also says what is wrong so the
//  dialog keeps its specific messages.
// =====================================================================
#include <hobbycad/parameters.h>

#include <cstdio>
#include <limits>
#include <string>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main()
{
    std::printf("parameter names\n");

    const auto problem = [](const char* name) { return ParameterEngine::checkName(name).problem; };

    check(problem("width") == NameProblem::None && problem("_w2") == NameProblem::None,
          "plain names are accepted");
    check(problem("") == NameProblem::Empty, "an empty name is refused as empty");
    check(problem("2w") == NameProblem::StartsWithDigit, "a leading digit is named as such");
    check(problem("-w") == NameProblem::BadStart, "a leading symbol is refused");
    check(problem("tau") == NameProblem::Reserved && problem("PI") == NameProblem::Reserved,
          "every name the evaluator reserves is refused, in any case");

    const NameCheck umlaut = ParameterEngine::checkName("l\xC3\xA4ngd");
    check(umlaut.problem == NameProblem::BadCharacter && umlaut.character == "\xC3\xA4",
          "a non-ASCII letter is refused and reported whole");
    const NameCheck lead = ParameterEngine::checkName("\xC3\xA4");
    check(lead.problem == NameProblem::BadStart && lead.character == "\xC3\xA4",
          "including as the first character");
    check(ParameterEngine::isValidName("width") && !ParameterEngine::isValidName("a b"),
          "isValidName agrees with checkName");

    // A dependency chain longer than ten resolves in one evaluation.
    std::vector<Parameter> chain;
    for (int i = 0; i < 15; ++i) {
        Parameter p;
        p.name = "p" + std::to_string(i);
        p.expression = i == 0 ? "1" : "p" + std::to_string(i - 1) + " + 1";
        chain.push_back(p);
    }
    ParameterEngine engine;
    engine.setParameters(chain);
    const EvaluationResult result = engine.evaluate();
    check(result.success && engine.value("p14") == 15.0,
          "a fifteen-long chain evaluates fully");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
