// =====================================================================
//  tests/naming/names.cpp — what a valid object name is
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include <hobbycad/naming.h>
#include <cstdio>
#include <string>

using namespace hobbycad;
static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    std::printf("object names\n");

    check(isValidObjectName("Profile"), "an ordinary name is fine");
    check(isValidObjectName("Sketch1"), "digits are fine");
    check(isValidObjectName("front plate"), "an interior space is fine");
    check(isValidObjectName("a*b"), "an asterisk INSIDE a name is fine");

    // The rule that makes the prompt marker unambiguous. Without it,
    // "sketch *Profile>" could mean a sketch actually called "*Profile".
    std::string why;
    check(!isValidObjectName("*Profile", &why),
          "a name cannot START with the edit marker");
    check(why.find('*') != std::string::npos,
          "and the reason says which character");

    check(!isValidObjectName(""), "empty is refused");
    check(!isValidObjectName(" Profile"), "a leading space is refused");
    check(!isValidObjectName("Profile "), "a trailing space is refused");
    check(!isValidObjectName(std::string("Pro\nfile")),
          "a control character is refused");

    // The marker constant and the rule must agree; hard-coding '*' in one
    // and reading kEditMarker in the other is how they drift.
    check(!isValidObjectName(std::string(1, kEditMarker) + "x"),
          "the rule is keyed on kEditMarker, not a hard-coded character");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
