// =====================================================================
//  src/hobbycad/gui/full/aiscanvas2d.h — 2D drawing canvas for OCCT
// =====================================================================
//
//  A reusable AIS_InteractiveObject that lets subclasses draw 2D
//  primitives (arcs, lines, filled triangles) in screen-space.
//
//  Uses Graphic3d_TMF_2d transform persistence so geometry is truly
//  camera-fixed — it does not rotate when the scene rotates.
//
//  All 2D coordinates use screen conventions:
//    x = horizontal (positive = right)
//    y = vertical   (positive = up)
//
//  The canvas is anchored to a screen corner with the same API as
//  OCCT's TriedronPers (corner + pixel offset), but uses TMF_2d
//  instead so the geometry does not rotate with the camera.
//
//  Subclasses override onPaint() and call the draw*() / addSensitive*()
//  methods.  The base class handles all OCCT plumbing.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_AISCANVAS2D_H
#define HOBBYCAD_AISCANVAS2D_H

#include <AIS_InteractiveObject.hxx>
#include <Aspect_TypeOfTriedronPosition.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <gp_Pnt.hxx>

#include <string>
#include <utility>
#include <vector>

class Graphic3d_Group;
class SelectMgr_Selection;

namespace hobbycad {

/// A 2D drawing surface rendered as flat geometry in OCCT's
/// Graphic3d_TMF_2d screen-space coordinate system.
///
/// Subclasses override onPaint() to describe what to draw.
class AIS_Canvas2D : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(AIS_Canvas2D, AIS_InteractiveObject)

public:
    /// Construct with screen-corner anchor and pixel offset.
    /// Default matches the ViewCube position.
    AIS_Canvas2D(Aspect_TypeOfTriedronPosition theCorner
                     = Aspect_TOTP_RIGHT_UPPER,
                 int theOffsetX = 85,
                 int theOffsetY = 85);

protected:
    // ---- Subclass interface -------------------------------------------

    /// Override to draw content.  Called during Compute() and
    /// ComputeSelection().
    virtual void onPaint() = 0;

    // ---- 2D drawing primitives (call from onPaint) --------------------

    /// Draw a circular arc (polyline) centered at (cx, cy).
    /// @param startDeg  Arc start angle in degrees (CCW from east).
    /// @param sweepDeg  Arc sweep in degrees (positive = CCW).
    void drawArc(double cx, double cy, double radius,
                 double startDeg, double sweepDeg,
                 const Quantity_Color& color, double lineWidth,
                 int segments = 36);

    /// Draw a straight line from (x1,y1) to (x2,y2).
    void drawLine(double x1, double y1, double x2, double y2,
                  const Quantity_Color& color, double lineWidth);

    /// Draw a filled triangle.
    void drawFilledTriangle(double x1, double y1,
                            double x2, double y2,
                            double x3, double y3,
                            const Quantity_Color& color);

    /// Draw a filled circle.
    void drawFilledCircle(double cx, double cy, double radius,
                          const Quantity_Color& color,
                          int segments = 24);

    /// Draw a text label.
    /// @param x, y       Lower-left corner of the text.
    /// @param text        The string to display (ASCII).
    /// @param color       Text color.
    /// @param height      Font height in pixels.
    /// @param font        Font family name (e.g. "Arial", "Courier").
    void drawText(double x, double y,
                  const char* text,
                  const Quantity_Color& color,
                  double height = 12.0,
                  const char* font = "Arial");

    /// Estimate the pixel width of a text string at a given height.
    /// Uses a fixed-width approximation (~0.6 × height per char).
    static double estimateTextWidth(const char* text, double height);

    // ---- Sensitive region helpers (call from onPaint) ------------------

    /// Add a clickable polygon region defined by 2D vertices.
    void addSensitivePoly(
        const Handle(SelectMgr_EntityOwner)& owner,
        const std::vector<std::pair<double, double>>& poly2d);

    // ---- Coordinate conversion ----------------------------------------

    /// Convert 2D offset from anchor to 3D point in TMF_2d space.
    /// x = right, y = up (relative to anchor center).
    gp_Pnt to3D(double x, double y) const;

    // ---- AIS overrides ------------------------------------------------

    void Compute(const Handle(PrsMgr_PresentationManager)& thePM,
                 const Handle(Prs3d_Presentation)& thePrs,
                 const Standard_Integer theMode) override;

    void ComputeSelection(const Handle(SelectMgr_Selection)& theSel,
                          const Standard_Integer theMode) override;

private:
    void clearPrimitives();
    void renderVisuals(const Handle(Prs3d_Presentation)& thePrs);
    void renderSensitives(const Handle(SelectMgr_Selection)& theSel);

    struct ArcCmd {
        double cx, cy, radius;
        double startRad, sweepRad;
        Quantity_Color color;
        double lineWidth;
        int segments;
    };

    struct LineCmd {
        double x1, y1, x2, y2;
        Quantity_Color color;
        double lineWidth;
    };

    struct TriCmd {
        double x1, y1, x2, y2, x3, y3;
        Quantity_Color color;
    };

    struct CircleCmd {
        double cx, cy, radius;
        Quantity_Color color;
        int segments;
    };

    struct TextCmd {
        double x, y;
        std::string text;
        Quantity_Color color;
        double height;
        std::string font;
    };

    struct SensitiveCmd {
        Handle(SelectMgr_EntityOwner) owner;
        std::vector<gp_Pnt> pts3d;
    };

    std::vector<ArcCmd>       m_arcs;
    std::vector<LineCmd>      m_lines;
    std::vector<TriCmd>       m_tris;
    std::vector<CircleCmd>    m_circles;
    std::vector<TextCmd>      m_texts;
    std::vector<SensitiveCmd> m_sensitives;

    // Corner + offset for positioning (mirrors TriedronPers API).
    Aspect_TypeOfTriedronPosition m_corner;
    int m_offsetX;
    int m_offsetY;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_AISCANVAS2D_H
