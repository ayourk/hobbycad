// =====================================================================
//  tests/project/dimension_fields.cpp — typed fields and display names
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  A locked dimension field used to reach constraint creation as its
//  translated label, which was then compared against English: in German
//  eleven of twelve labels changed, so a typed width or sweep angle made no
//  constraint. Fields now carry an identity (sketch::DimField) and the label
//  is display text only.
//
//  The properties panel also named constraints from a hand list indexed by
//  the enum's value; it had drifted, and Horizontal showed as "Fixed Angle".
//  Names now come from sketch::constraintDisplayName by type.
// =====================================================================
#include <hobbycad/sketch/constraint.h>
#include <hobbycad/sketch/decomposition.h>
#include <hobbycad/sketch/dimension_field.h>
#include <hobbycad/sketch/entity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace hobbycad;
using namespace hobbycad::sketch;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static DecompositionResult decomposeRect(const LockedDims& locked)
{
    const Entity rect = createRectangle(1, Point2D(0.0, 0.0), Point2D(10.0, 5.0));
    int nextE = 100, nextC = 200;
    return decomposeEntity(rect, locked, [&] { return nextE++; }, [&] { return nextC++; },
                           1, {}, "Rectangle");
}

static int countDistance(const DecompositionResult& d, double value)
{
    int n = 0;
    for (const Constraint& c : d.constraints) {
        if (c.type == ConstraintType::Distance && std::fabs(c.value - value) < 1e-9) ++n;
    }
    return n;
}

int main()
{
    std::printf("dimension fields and constraint display names\n");

    // ---- every field has a label, and labels are distinct ----------------
    {
        std::set<std::string> labels;
        bool allNamed = true;
        for (DimField f : allDimFields()) {
            const char* l = dimFieldLabel(f);
            allNamed = allNamed && l && std::strlen(l) > 0;
            labels.insert(l ? l : "");
        }
        check(allNamed, "every dimension field has a label");
        check(labels.size() == allDimFields().size(), "no two fields share a label");
        check(allDimFields().size() == 23, "all 23 fields are listed");
        check(std::strcmp(dimFieldContext(), "hobbycad::SketchCanvas") == 0,
              "labels keep the context their translations are filed under");
    }

    // ---- angle fields are exactly the angles ------------------------------
    {
        const std::set<DimField> angles = {
            DimField::Angle, DimField::SweepAngle, DimField::ChordAngle, DimField::EdgeAngle,
            DimField::Edge1Angle, DimField::Edge2Angle, DimField::ArcStart, DimField::ArcSweep};
        bool agree = true;
        for (DimField f : allDimFields()) {
            agree = agree && (isAngleDimField(f) == (angles.count(f) == 1));
        }
        check(agree, "isAngleDimField marks the angle fields and only those");
    }

    // ---- decomposition reads the field, not a label -----------------------
    {
        const DecompositionResult d = decomposeRect({{DimField::Width, 10.0},
                                                     {DimField::Height, 5.0}});
        check(d.success, "a rectangle decomposes");
        check(countDistance(d, 10.0) == 1 && countDistance(d, 5.0) == 1,
              "locked Width and Height each become one Distance");

        const DecompositionResult none = decomposeRect({});
        check(countDistance(none, 10.0) == 0 && countDistance(none, 5.0) == 0,
              "with nothing locked no Distance is made");

        const DecompositionResult radius = decomposeRect({{DimField::Radius, 10.0}});
        check(countDistance(radius, 10.0) == 0,
              "a field a rectangle does not use makes no constraint");

        const DecompositionResult angle = decomposeRect({{DimField::Edge1Angle, 30.0}});
        bool fixedAngle = false;
        for (const Constraint& c : angle.constraints) {
            fixedAngle = fixedAngle
                || (c.type == ConstraintType::FixedAngle && std::fabs(c.value - 30.0) < 1e-9);
        }
        check(fixedAngle, "a locked Edge1 Angle becomes a Fixed Angle of 30");
    }

    // ---- display names by type ---------------------------------------------
    {
        std::set<std::string> names;
        bool allNamed = true;
        for (ConstraintType t : allConstraintTypes()) {
            const char* n = constraintDisplayName(t);
            allNamed = allNamed && n && std::strlen(n) > 0;
            names.insert(n ? n : "");
        }
        check(allNamed, "every constraint type has a display name");
        check(names.size() == allConstraintTypes().size(), "no two types share a display name");
        check(std::strcmp(constraintDisplayName(ConstraintType::Horizontal), "Horizontal") == 0
                  && std::strcmp(constraintDisplayName(ConstraintType::Vertical), "Vertical") == 0
                  && std::strcmp(constraintDisplayName(ConstraintType::FixedAngle),
                                 "Fixed Angle") == 0,
              "Horizontal, Vertical and Fixed Angle are named for themselves");
        check(std::strcmp(constraintDisplayName(ConstraintType::Curvature),
                          "Curvature (G2)") == 0
                  && std::strcmp(constraintTypeName(ConstraintType::Curvature),
                                 "Curvature") == 0,
              "the display name can differ from the command-line keyword");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
