// =====================================================================
//  tests/project/display_names.cpp — the names a front end shows
// =====================================================================
//  SPDX-License-Identifier: GPL-3.0-only
//  Two rules meet here. A name a person TYPES stays English, because it
//  is a keyword; the name SHOWN goes through the translator seam. Mixing
//  them up gives either a German sentence carrying an English word, or a
//  command line that stops understanding its own keywords in German.
// =====================================================================
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/property_schema.h>
#include <hobbycad/sketch/undo.h>
#include <hobbycad/translate.h>

#include <cstdio>
#include <string>

using namespace hobbycad;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

/// A translator that marks what it was asked, so a caller that never
/// reaches the seam is visible.
static std::string shout(const char* context, const char* source, const char* disambiguation)
{
    (void)disambiguation;
    return std::string("[") + (context ? context : "?") + "]" + (source ? source : "");
}

int main()
{
    std::printf("display names\n");

    check(std::string(sketch::entityTypeName(sketch::EntityType::Line)) == "Line",
          "an entity type's name is English with no translator installed");
    check(sketch::entityTypeDisplayName(sketch::EntityType::Line) == "Line",
          "and so is the name shown");

    setTranslator(&shout);
    check(sketch::entityTypeDisplayName(sketch::EntityType::Line)
              == std::string("[") + sketch::entityTypeContext() + "]Line",
          "with one installed, the shown name goes through the seam, in its own context");
    check(std::string(sketch::entityTypeName(sketch::EntityType::Line)) == "Line",
          "while the marked name itself stays English, as a keyword must");
    setTranslator(nullptr);

    // The pair exists because the two differ: "Curvature" is typed,
    // "Curvature (G2)" is shown.
    bool anyDifferent = false;
    const sketch::ConstraintType types[] = {
        sketch::ConstraintType::Distance, sketch::ConstraintType::Radius,
        sketch::ConstraintType::Angle,    sketch::ConstraintType::Horizontal,
        sketch::ConstraintType::Tangent,  sketch::ConstraintType::Curvature,
        sketch::ConstraintType::PointOnSpline,
    };
    for (const sketch::ConstraintType type : types) {
        if (std::string(sketch::constraintTypeName(type))
            != std::string(sketch::constraintDisplayName(type))) {
            anyDifferent = true;
        }
    }
    check(anyDifferent,
          "at least one constraint is shown under a different name than it is typed");
    check(std::string(sketch::constraintDisplayContext()) != std::string(),
          "and the shown names have a translation context of their own");

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
