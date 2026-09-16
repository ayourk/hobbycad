// =====================================================================
//  sketchtheme.h — central color palette for the sketch canvas
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//  Before this, every sketch color was a hardcoded QColor literal spread
//  across entityrenderer, constraintrenderer, snapengine and sketchcanvas,
//  so there was no way to be theme-aware. SketchTheme gathers them in one
//  place with a light() and a dark() variant. light() reproduces the exact
//  historical literals (a regression guard: tests/... checks they match),
//  dark() is derived from the same hues for a dark canvas ground.
//
//  Identifiers and prose here use American spelling ("color").
#ifndef HOBBYCAD_SKETCHTHEME_H
#define HOBBYCAD_SKETCHTHEME_H

#include <QColor>
#include <QString>

#include <vector>

namespace hobbycad {

struct SketchTheme {
    // ---- canvas ground ------------------------------------------------
    QColor background;          // painter clear / QPalette::Window
    QColor grid;

    // ---- geometry base colors ---------------------------------------
    QColor normalGeometry;      // under-constrained
    QColor fullyConstrained;    // fully constrained
    QColor inconsistent;        // failed / over-constrained sketch
    QColor projectedGeometry;   // driven by a source sketch (reference)
    QColor constructionNormal;
    QColor constructionSelected;
    QColor centerlineNormal;
    QColor centerlineSelected;

    // ---- selection ----------------------------------------------------
    // Selection is normally base.lighter(140); this is the fallback used
    // when that would give too little contrast (near-black in light,
    // near-white in dark).
    QColor selectionFallback;

    // ---- constraints / dimensions ------------------------------------
    QColor constraintDriving;
    QColor constraintReference;
    QColor constraintFailed;
    QColor constraintRedundant;
    QColor constraintSelected;

    // dimension inline-edit chip
    QColor dimEditSelBg, dimEditSelText, dimEditSelBorder;
    QColor dimEditTypingBg, dimEditTypingText, dimEditTypingBorder;
    QColor dimEditEmptyBg, dimEditEmptyBorder;
    QColor labelHalo;           // stroke halo behind dim/text labels
    QColor labelText;           // dimension label text
    QColor labelFill;           // dimension label background box

    // ---- snapping / inference ----------------------------------------
    QColor snap;
    QColor inference;
    QColor guide;
    QColor guideX;
    QColor guideY;

    // ---- points / handles --------------------------------------------
    QColor pointDotPen;
    QColor pointDotFill;
    QColor handleCenter, handleCenterFill;
    QColor handleAngle, handleAngleFill;
    QColor handleRadius, handleRadiusFill;

    // ---- origin / axes / misc ----------------------------------------
    QColor axisX;
    QColor axisY;
    QColor axisZ;
    QColor origin;
    QColor preview;
    QColor tangentMarker;
    QColor pivotInk, pivotFill;

    static SketchTheme light();
    static SketchTheme dark();
};

// ---- Generic field table + user overrides ---------------------------------
// One entry per SketchTheme color, so the theme editor and the override
// loader can walk every role by name via a member pointer.
struct SketchThemeField {
    const char* category;
    const char* display;
    const char* field;                 // stable key used in QSettings
    QColor SketchTheme::* member;
};
const std::vector<SketchThemeField>& sketchThemeFields();
QString sketchThemeOverrideKey(bool dark, const char* field);
/// Overlay persisted per-role overrides (QSettings) onto a base theme.
void applySketchThemeOverrides(SketchTheme& t, bool dark);

inline SketchTheme SketchTheme::light()
{
    SketchTheme t;
    t.background          = QColor(240, 240, 240);
    t.grid                = QColor(200, 200, 200);

    t.normalGeometry      = QColor(0, 120, 215);
    t.fullyConstrained    = QColor(0, 0, 0);
    t.inconsistent        = QColor(200, 0, 0);
    t.projectedGeometry   = QColor(148, 90, 200);
    t.constructionNormal  = QColor(180, 100, 50);
    t.constructionSelected= QColor(255, 140, 0);
    t.centerlineNormal    = QColor(70, 150, 160);
    t.centerlineSelected  = QColor(60, 200, 200);

    t.selectionFallback   = QColor(90, 90, 90);

    t.constraintDriving   = QColor(0, 120, 215);
    t.constraintReference = QColor(128, 128, 128, 191);  // 75% opacity fade (driven dims)
    t.constraintFailed    = QColor(255, 0, 0);
    t.constraintRedundant = QColor(200, 0, 200);
    t.constraintSelected  = QColor(255, 140, 0);

    t.dimEditSelBg        = QColor(51, 153, 255);
    t.dimEditSelText      = QColor(255, 255, 255);
    t.dimEditSelBorder    = QColor(30, 100, 200);
    t.dimEditTypingBg     = QColor(255, 235, 160);
    t.dimEditTypingText   = QColor(0, 0, 0);
    t.dimEditTypingBorder = QColor(210, 170, 50);
    t.dimEditEmptyBg      = QColor(255, 255, 255);
    t.dimEditEmptyBorder  = QColor(100, 100, 100);
    t.labelHalo           = QColor(255, 255, 255, 230);
    t.labelText           = QColor(0, 0, 0);
    t.labelFill           = QColor(255, 255, 255);

    t.snap                = QColor(255, 140, 0);
    t.inference           = QColor(0, 160, 90);
    t.guide               = QColor(255, 140, 0);
    t.guideX              = QColor(255, 80, 80);
    t.guideY              = QColor(80, 200, 80);

    t.pointDotPen         = QColor(60, 60, 60);
    t.pointDotFill        = QColor(255, 255, 255);
    t.handleCenter        = QColor(0, 120, 215);
    t.handleCenterFill    = QColor(255, 255, 255);
    t.handleAngle         = QColor(30, 160, 30);
    t.handleAngleFill     = QColor(150, 230, 150);
    t.handleRadius        = QColor(200, 50, 50);
    t.handleRadiusFill    = QColor(255, 150, 150);

    t.axisX               = QColor(255, 0, 0);
    t.axisY               = QColor(0, 128, 0);
    t.axisZ               = QColor(0, 0, 255);
    t.origin              = QColor(0, 0, 0);
    t.preview             = QColor(0, 120, 215);
    t.tangentMarker       = QColor(200, 40, 40);
    t.pivotInk            = QColor(150, 110, 0);
    t.pivotFill           = QColor(255, 200, 0);
    return t;
}

inline SketchTheme SketchTheme::dark()
{
    // Dark mode follows the DOS AutoCAD-on-black convention: a pure-black
    // ground and the classic AutoCAD Color Index (ACI) bright primaries, one
    // per role. ACI 4 cyan = under-constrained, ACI 7 white = the default
    // drawing color (here: fully constrained), ACI 1 red = error, ACI 6
    // magenta = reference, ACI 2 yellow = construction, ACI 3 green =
    // centerline. Pure blue (ACI 5) is the one primary too dark to read on
    // black, so it is lightened where used (axis Z). The parametric-era chrome
    // below (constraint glyphs, dimension-edit boxes, handles) has no DOS
    // equivalent and stays as it was, tuned for legibility on the dark ground.
    SketchTheme t;
    t.background          = QColor(0, 0, 0);         // DOS black
    t.grid                = QColor(72, 72, 72);       // faint gray grid, visible on black

    t.normalGeometry      = QColor(0, 255, 255);     // ACI 4 cyan (under-constrained)
    t.fullyConstrained    = QColor(255, 255, 255);   // ACI 7 white (fully constrained)
    t.inconsistent        = QColor(255, 0, 0);       // ACI 1 red (error)
    t.projectedGeometry   = QColor(255, 0, 255);     // ACI 6 magenta (reference)
    t.constructionNormal  = QColor(255, 255, 0);     // ACI 2 yellow (dashed construction)
    t.constructionSelected= QColor(255, 255, 160);
    t.centerlineNormal    = QColor(0, 255, 0);       // ACI 3 green (dash-dot centerline)
    t.centerlineSelected  = QColor(160, 255, 160);

    t.selectionFallback   = QColor(255, 255, 0);     // ACI 2 yellow highlight on a white line

    t.constraintDriving   = QColor(64, 160, 255);
    t.constraintReference = QColor(160, 160, 160, 191);  // 75% opacity fade (driven dims)
    t.constraintFailed    = QColor(255, 90, 90);
    t.constraintRedundant = QColor(225, 110, 225);
    t.constraintSelected  = QColor(255, 165, 60);

    t.dimEditSelBg        = QColor(40, 110, 190);
    t.dimEditSelText      = QColor(255, 255, 255);
    t.dimEditSelBorder    = QColor(120, 180, 255);
    t.dimEditTypingBg     = QColor(90, 80, 40);
    t.dimEditTypingText   = QColor(255, 240, 190);
    t.dimEditTypingBorder = QColor(210, 180, 90);
    t.dimEditEmptyBg      = QColor(60, 60, 60);
    t.dimEditEmptyBorder  = QColor(150, 150, 150);
    t.labelHalo           = QColor(20, 20, 20, 230);  // dark halo on dark bg
    t.labelText           = QColor(228, 228, 228);
    t.labelFill           = QColor(52, 52, 52);

    t.snap                = QColor(255, 255, 0);      // ACI 2 yellow snap marker
    t.inference           = QColor(0, 255, 0);        // ACI 3 green tracking
    t.guide               = QColor(128, 128, 128);    // ACI 8 gray tracking line
    t.guideX              = QColor(255, 0, 0);        // ACI 1 red (X)
    t.guideY              = QColor(0, 255, 0);        // ACI 3 green (Y)

    t.pointDotPen         = QColor(255, 255, 255);
    t.pointDotFill        = QColor(0, 0, 0);          // filled with the ground
    t.handleCenter        = QColor(64, 160, 255);
    t.handleCenterFill    = QColor(30, 30, 30);
    t.handleAngle         = QColor(90, 210, 90);
    t.handleAngleFill     = QColor(30, 60, 30);
    t.handleRadius        = QColor(235, 100, 100);
    t.handleRadiusFill    = QColor(70, 30, 30);

    t.axisX               = QColor(255, 0, 0);       // ACI 1 red (AutoCAD X)
    t.axisY               = QColor(0, 255, 0);       // ACI 3 green (AutoCAD Y)
    t.axisZ               = QColor(90, 90, 255);     // ACI 5 blue, lightened to read on black
    t.origin              = QColor(255, 255, 255);   // ACI 7 white
    t.preview             = QColor(0, 255, 255);     // cyan: geometry being drawn reads as under-constrained
    t.tangentMarker       = QColor(255, 0, 0);
    t.pivotInk            = QColor(255, 210, 90);
    t.pivotFill           = QColor(120, 95, 0);
    return t;
}

}  // namespace hobbycad

#endif  // HOBBYCAD_SKETCHTHEME_H
