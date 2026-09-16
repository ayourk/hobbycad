// =====================================================================
//  src/hobbycad/gui/sketchtheme.cpp — theme field table + user overrides
// =====================================================================
//  A generic field table (member pointers) over SketchTheme so the theme
//  editor and the override loader can walk every color by name, plus the
//  persisted per-role overrides that let a user recolor the canvas.
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "sketchtheme.h"

#include <QSettings>

namespace hobbycad {

const std::vector<SketchThemeField>& sketchThemeFields()
{
    static const std::vector<SketchThemeField> kFields = {
    { "Canvas", "Background", "background", &SketchTheme::background },
    { "Canvas", "Grid", "grid", &SketchTheme::grid },
    { "Geometry", "Under-constrained", "normalGeometry", &SketchTheme::normalGeometry },
    { "Geometry", "Fully constrained", "fullyConstrained", &SketchTheme::fullyConstrained },
    { "Geometry", "Error / inconsistent", "inconsistent", &SketchTheme::inconsistent },
    { "Geometry", "Projected (reference)", "projectedGeometry", &SketchTheme::projectedGeometry },
    { "Geometry", "Construction", "constructionNormal", &SketchTheme::constructionNormal },
    { "Geometry", "Construction (selected)", "constructionSelected", &SketchTheme::constructionSelected },
    { "Geometry", "Centerline", "centerlineNormal", &SketchTheme::centerlineNormal },
    { "Geometry", "Centerline (selected)", "centerlineSelected", &SketchTheme::centerlineSelected },
    { "Geometry", "Selection highlight", "selectionFallback", &SketchTheme::selectionFallback },
    { "Constraints", "Driving", "constraintDriving", &SketchTheme::constraintDriving },
    { "Constraints", "Reference (driven)", "constraintReference", &SketchTheme::constraintReference },
    { "Constraints", "Failed", "constraintFailed", &SketchTheme::constraintFailed },
    { "Constraints", "Redundant", "constraintRedundant", &SketchTheme::constraintRedundant },
    { "Constraints", "Selected", "constraintSelected", &SketchTheme::constraintSelected },
    { "Dimension edit", "Selected background", "dimEditSelBg", &SketchTheme::dimEditSelBg },
    { "Dimension edit", "Selected text", "dimEditSelText", &SketchTheme::dimEditSelText },
    { "Dimension edit", "Selected border", "dimEditSelBorder", &SketchTheme::dimEditSelBorder },
    { "Dimension edit", "Typing background", "dimEditTypingBg", &SketchTheme::dimEditTypingBg },
    { "Dimension edit", "Typing text", "dimEditTypingText", &SketchTheme::dimEditTypingText },
    { "Dimension edit", "Typing border", "dimEditTypingBorder", &SketchTheme::dimEditTypingBorder },
    { "Dimension edit", "Empty background", "dimEditEmptyBg", &SketchTheme::dimEditEmptyBg },
    { "Dimension edit", "Empty border", "dimEditEmptyBorder", &SketchTheme::dimEditEmptyBorder },
    { "Labels", "Halo", "labelHalo", &SketchTheme::labelHalo },
    { "Labels", "Text", "labelText", &SketchTheme::labelText },
    { "Labels", "Fill", "labelFill", &SketchTheme::labelFill },
    { "Snap & guides", "Snap marker", "snap", &SketchTheme::snap },
    { "Snap & guides", "Inference", "inference", &SketchTheme::inference },
    { "Snap & guides", "Guide", "guide", &SketchTheme::guide },
    { "Snap & guides", "Guide X", "guideX", &SketchTheme::guideX },
    { "Snap & guides", "Guide Y", "guideY", &SketchTheme::guideY },
    { "Points & handles", "Point outline", "pointDotPen", &SketchTheme::pointDotPen },
    { "Points & handles", "Point fill", "pointDotFill", &SketchTheme::pointDotFill },
    { "Points & handles", "Center handle", "handleCenter", &SketchTheme::handleCenter },
    { "Points & handles", "Center handle fill", "handleCenterFill", &SketchTheme::handleCenterFill },
    { "Points & handles", "Angle handle", "handleAngle", &SketchTheme::handleAngle },
    { "Points & handles", "Angle handle fill", "handleAngleFill", &SketchTheme::handleAngleFill },
    { "Points & handles", "Radius handle", "handleRadius", &SketchTheme::handleRadius },
    { "Points & handles", "Radius handle fill", "handleRadiusFill", &SketchTheme::handleRadiusFill },
    { "Axes & origin", "Axis X", "axisX", &SketchTheme::axisX },
    { "Axes & origin", "Axis Y", "axisY", &SketchTheme::axisY },
    { "Axes & origin", "Axis Z", "axisZ", &SketchTheme::axisZ },
    { "Axes & origin", "Origin", "origin", &SketchTheme::origin },
    { "Misc", "Preview", "preview", &SketchTheme::preview },
    { "Misc", "Tangent marker", "tangentMarker", &SketchTheme::tangentMarker },
    { "Misc", "Pivot ink", "pivotInk", &SketchTheme::pivotInk },
    { "Misc", "Pivot fill", "pivotFill", &SketchTheme::pivotFill },
    };
    return kFields;
}

QString sketchThemeOverrideKey(bool dark, const char* field)
{
    return QStringLiteral("theme/%1/%2")
        .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"),
             QString::fromLatin1(field));
}

void applySketchThemeOverrides(SketchTheme& t, bool dark)
{
    QSettings s;
    for (const SketchThemeField& f : sketchThemeFields()) {
        const QString key = sketchThemeOverrideKey(dark, f.field);
        const QVariant v = s.value(key);
        if (v.isValid()) {
            const QColor c(v.toString());   // stored as #RRGGBB
            if (c.isValid()) t.*(f.member) = c;
        }
    }
}

}  // namespace hobbycad
