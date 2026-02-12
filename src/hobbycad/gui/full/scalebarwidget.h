// =====================================================================
//  src/hobbycad/gui/full/scalebarwidget.h — 2D scale bar overlay
// =====================================================================
//
//  A transparent overlay widget that draws a horizontal scale bar with
//  tick marks and a unit label in the bottom-left corner of the
//  viewport.  Updates automatically when the camera zoom changes.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_SCALEBARWIDGET_H
#define HOBBYCAD_SCALEBARWIDGET_H

#include <QWidget>

#include <V3d_View.hxx>

namespace hobbycad {

class ScaleBarWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScaleBarWidget(QWidget* parent = nullptr);

    /// Set the V3d_View used to compute world-space scale.
    void setView(const Handle(V3d_View)& view);

    /// Call after zoom/pan/resize to recalculate the scale bar.
    void updateScale();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// Choose a "nice" round number for the scale bar length.
    double niceNumber(double value) const;

    /// Step down to the next smaller nice number (1-2-5 sequence).
    double niceNumberBelow(double value) const;

    Handle(V3d_View) m_view;
    double m_worldLength = 100.0;   // world-space length of bar (mm)
    int    m_pixelLength = 100;     // screen-space length of bar (px)
    QString m_label;                // e.g. "100 mm"
};

}  // namespace hobbycad

#endif  // HOBBYCAD_SCALEBARWIDGET_H
