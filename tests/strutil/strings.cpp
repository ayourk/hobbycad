// =====================================================================
//  tests/strutil/strings.cpp — the Qt-free string layer
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//
//  These claims are what the command layer relies on after losing Qt.
//  The number, padding and substitution ones were measured against Qt
//  first: QString::arg(double) prints "%g", a negative field width left
//  justifies, and a value inserted for one placeholder must never be
//  scanned for placeholders of its own.
// =====================================================================

#include <hobbycad/strutil.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace hobbycad;

static int failures = 0;

static void check(bool ok, const char* claim)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", claim);
    if (!ok) ++failures;
}

static void checkEq(const std::string& got, const std::string& want,
                    const char* claim)
{
    if (got == want) {
        std::printf("  [PASS] %s\n", claim);
    } else {
        std::printf("  [FAIL] %s (got \"%s\", wanted \"%s\")\n",
                    claim, got.c_str(), want.c_str());
        ++failures;
    }
}

int main()
{
    // ---- numbers read as Qt wrote them ------------------------------
    checkEq(numToString(1.0 / 3.0), "0.333333",
            "a double prints to six significant digits, as QString did");
    checkEq(numToString(123456789.0), "1.23457e+08",
            "a large double switches to exponent form, as QString did");
    checkEq(numToString(0.1 + 0.2), "0.3",
            "six digits hide the binary representation, as QString did");
    checkEq(numToString(-0.0), "0",
            "negative zero prints as zero, so a coordinate that rounds to "
            "zero is not reported as negative");
    checkEq(numToString(2.5), "2.5", "a short double keeps its digits");
    checkEq(numToString(42), "42", "an int prints without a decimal point");
    checkEq(numToString(static_cast<size_t>(7)), "7", "a size_t prints plainly");
    checkEq(numToStringFixed(3.14159, 1), "3.1",
            "fixed notation rounds to the requested decimals");
    checkEq(numToStringFixed(2.0, 3), "2.000",
            "fixed notation keeps trailing zeros");

    // ---- substitution ------------------------------------------------
    checkEq(subst("a %1 b %2", "x", 7), "a x b 7",
            "placeholders take their values in order");
    checkEq(subst("%1-%1", "z"), "z-z",
            "every occurrence of a placeholder is replaced");
    checkEq(subst("%2 then %1", "one", "two"), "two then one",
            "placeholders are positional, not sequential");
    checkEq(subst("%1 %2", "%2", "b"), "%2 b",
            "a value containing a placeholder is never substituted into");
    checkEq(subst("nothing here"), "nothing here",
            "a format with no placeholder is returned unchanged");
    checkEq(subst("%1 and %2", "only"), "only and %2",
            "a placeholder with no value is left visible, not blanked");
    checkEq(subst("100% done: %1", "yes"), "100% done: yes",
            "a percent sign that is not a placeholder survives");
    checkEq(subst("%1", 1.0 / 3.0), "0.333333",
            "a double substitutes with its Qt spelling");
    checkEq(substList("%10", {"a", "b", "c", "d", "e",
                              "f", "g", "h", "i", "tenth"}), "tenth",
            "a two digit placeholder is one number, as Qt reads it");
    checkEq(subst("%10", "x"), "%10",
            "a two digit placeholder with no value is left standing");
    checkEq(subst("%12", "one", "two"), "%12",
            "a two digit placeholder beyond the values is left standing");
    checkEq(subst("%1 0", "x"), "x 0",
            "a digit after a space is text, not part of the placeholder");

    // ---- padding keeps Qt's field-width sign -------------------------
    checkEq(pad("5", 3), "  5", "a positive field width right justifies");
    checkEq(pad("ab", -5), "ab   ", "a negative field width left justifies");
    checkEq(pad("abcdef", 3), "abcdef",
            "text at or over the field width is left alone");
    checkEq(pad("x", 0), "x", "a zero field width pads nothing");
    checkEq(padLeft("7", 3, '0'), "007", "padLeft honors the fill character");
    checkEq(padRight("7", 3, '.'), "7..", "padRight honors the fill character");

    // ---- splitting and joining ---------------------------------------
    check(split("a,b,,c", ',').size() == 4,
          "split keeps empty fields by default");
    check(split("a,b,,c", ',', false).size() == 3,
          "split can drop empty fields");
    check(split("", ',', false).empty(),
          "splitting an empty string without empties yields nothing");
    checkEq(join(split("a,b,c", ','), "-"), "a-b-c",
            "split and join round trip");
    check(splitWhitespace("  a   b\tc \n").size() == 3,
          "splitWhitespace drops the empty fields runs of space create");
    checkEq(join(splitWhitespace("one  two"), ","), "one,two",
            "splitWhitespace separates on any whitespace");
    checkEq(join({"a", "b"}, ", "), "a, b", "join inserts the separator");
    checkEq(join({"solo"}, ", "), "solo",
            "join adds no separator to a single element");
    checkEq(join({}, ", "), "", "joining nothing yields an empty string");

    // ---- trimming ----------------------------------------------------
    checkEq(trim("\t x \n"), "x", "trim removes whitespace at both ends");
    checkEq(trim("   "), "", "trimming only whitespace yields empty");
    checkEq(trim("a b"), "a b", "trim leaves inner spaces alone");
    checkEq(trim("\va\f"), "a",
            "trim covers whitespace a \" \\t\\r\\n\" set would miss");
    checkEq(simplify("a   b\t\tc"), "a b c",
            "simplify collapses internal whitespace runs to one space");
    checkEq(simplify("  padded  "), "padded", "simplify also trims the ends");

    // ---- case and membership -----------------------------------------
    checkEq(toLower("MiXeD"), "mixed", "toLower folds every character");
    checkEq(toUpper("MiXeD"), "MIXED", "toUpper folds every character");
    check(startsWith("sketch1", "sketch"), "startsWith matches a prefix");
    check(!startsWith("sk", "sketch"),
          "startsWith is false when the string is shorter than the prefix");
    check(endsWith("body.brep", ".brep"), "endsWith matches a suffix");
    check(startsWithIgnoreCase("SKETCH1", "sketch"),
          "startsWithIgnoreCase ignores case in both directions");
    check(startsWithIgnoreCase("sketch1", "SKETCH"),
          "startsWithIgnoreCase ignores case in the prefix too");
    check(!startsWithIgnoreCase("sk", "sketch"),
          "startsWithIgnoreCase is false when the string is too short");
    check(contains("circle radius 5", "radius"), "contains finds a substring");
    check(contains("a,b", ','), "contains finds a character");
    check(!contains("abc", 'z'), "contains reports a missing character");
    check(startsWith("-5", '-'), "startsWith matches a single character");
    check(!startsWith("", '-'), "startsWith on an empty string is false");
    check(endsWith("x,", ','), "endsWith matches a single character");

    // ---- text to number, matching what Qt accepted --------------------
    {
        bool ok = false;
        check(toInt("5", &ok) == 5 && ok, "a plain integer parses");
        check(toInt(" 5 ", &ok) == 5 && ok,
              "surrounding whitespace is allowed, as QString::toInt allowed it");
        check(toInt("5x", &ok) == 0 && !ok,
              "a trailing character fails the whole parse, not just the tail");
        check(toInt("", &ok) == 0 && !ok, "an empty string is not a number");
        check(toInt("0x10", &ok) == 0 && !ok,
              "hex is not accepted at base ten, as QString::toInt refused it");
        check(toInt("3.0", &ok) == 0 && !ok, "a decimal is not an integer");
        check(toInt("+3", &ok) == 3 && ok, "a leading plus is accepted");
        check(toInt("-3", &ok) == -3 && ok, "a leading minus is accepted");
        check(toInt("99999999999999999999", &ok) == 0 && !ok,
              "a value too large for int fails rather than wrapping");

        check(toDouble("2.5", &ok) == 2.5 && ok, "a decimal parses");
        check(toDouble("1e3", &ok) == 1000.0 && ok, "exponent notation parses");
        check(toDouble("3.0", &ok) == 3.0 && ok, "a double may look like an int");
        check(toDouble(" 5 ", &ok) == 5.0 && ok,
              "surrounding whitespace is allowed for doubles too");
        check(toDouble("1,5", &ok) == 0.0 && !ok,
              "a comma is not a decimal separator, as QString::toDouble held");
        check(toDouble("abc", &ok) == 0.0 && !ok, "text is not a number");
    }

    // ---- sublists ----------------------------------------------------
    {
        const std::vector<std::string> v{"a", "b", "c"};
        check(slice(v, 1).size() == 2, "slice takes everything from a position");
        checkEq(join(slice(v, 1), ","), "b,c", "slice keeps order");
        checkEq(join(slice(v, 1, 1), ","), "b", "slice honors a count");
        check(slice(v, 5).empty(),
              "slicing past the end yields nothing, so a bare command with "
              "no arguments is not an error");
        check(slice(v, 0).size() == 3, "slicing from zero keeps everything");
        checkEq(join(slice(v, 1, 99), ","), "b,c",
                "a count past the end stops at the end");
    }

    // ---- replacement --------------------------------------------------
    checkEq(replaceAll("aaa", "a", "bb"), "bbbbbb",
            "replaceAll replaces every occurrence");
    checkEq(replaceAll("aaa", "a", "a"), "aaa",
            "replaceAll terminates when the replacement contains the needle");
    checkEq(removeAll("a-b-c", "-"), "abc", "removeAll deletes every match");
    checkEq(removeAll("a b c", ' '), "abc", "removeAll accepts a character");
    {
        const std::vector<std::string> v{"all", "some"};
        check(contains(v, "all"), "contains finds a list element");
        check(!contains(v, "none"), "contains reports a missing element");
        checkEq(valueAt(v, 1), "some", "valueAt returns the element");
        checkEq(valueAt(v, 9), "",
                "valueAt past the end is empty, so an argument that was "
                "never typed reads as absent rather than crashing");
    }

    if (failures == 0) {
        std::printf("strutil: ALL PASS\n");
        return 0;
    }
    std::printf("FAILURES (%d failure(s))\n", failures);
    return 1;
}
