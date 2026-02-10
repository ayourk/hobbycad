// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.cpp — Full Mode window
// =====================================================================

#include "fullmodewindow.h"
#include "viewportwidget.h"

#include <QLabel>
#include <QSettings>
#include <QStatusBar>

#include <AIS_InteractiveContext.hxx>
#include <AIS_ListOfInteractive.hxx>
#include <AIS_Shape.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Type.hxx>

namespace hobbycad {

FullModeWindow::FullModeWindow(const OpenGLInfo& glInfo, QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("FullModeWindow"));
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    // Connect View > Reset View to the viewport
    if (resetViewAction()) {
        connect(resetViewAction(), &QAction::triggered,
                m_viewport, &ViewportWidget::resetCamera);
    }

    // Connect View > Rotate Left/Right (90° around Z axis)
    if (rotateLeftAction()) {
        connect(rotateLeftAction(), &QAction::triggered,
                this, [this]() { m_viewport->rotateCamera90(-2); });
    }
    if (rotateRightAction()) {
        connect(rotateRightAction(), &QAction::triggered,
                this, [this]() { m_viewport->rotateCamera90(2); });
    }

    finalizeLayout();

    // Axis indicator in the status bar (added after finalizeLayout
    // so restoreState doesn't interfere with widget ordering)
    m_axisLabel = new QLabel(tr("Axis: X"), this);
    m_axisLabel->setObjectName(QStringLiteral("AxisLabel"));
    statusBar()->addPermanentWidget(m_axisLabel);

    connect(m_viewport, &ViewportWidget::rotationAxisChanged,
            this, [this](ViewportWidget::RotationAxis axis) {
        static const char* names[] = { "X", "Y", "Z" };
        m_axisLabel->setText(tr("Axis: %1").arg(names[axis]));
    });

    // Apply saved preferences (rotation axis, spin/snap params, grid)
    applyPreferences();
}

void FullModeWindow::onDocumentLoaded()
{
    displayShapes();
}

void FullModeWindow::onDocumentClosed()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Remove only user shapes (AIS_Shape), preserving the trihedron,
    // grid, and ViewCube.
    AIS_ListOfInteractive displayed;
    ctx->DisplayedObjects(displayed);
    for (auto it = displayed.begin(); it != displayed.end(); ++it) {
        if ((*it)->IsKind(STANDARD_TYPE(AIS_Shape)))
            ctx->Remove(*it, false);
    }

    ctx->UpdateCurrentViewer();
    m_viewport->resetCamera();
}

void FullModeWindow::applyPreferences()
{
    QSettings s;
    s.beginGroup(QStringLiteral("preferences"));

    // Rotation axis
    int axis = s.value(QStringLiteral("defaultAxis"), 0).toInt();
    m_viewport->setRotationAxis(
        static_cast<ViewportWidget::RotationAxis>(qBound(0, axis, 2)));

    // PgUp/PgDn
    int pgStep = s.value(QStringLiteral("pgUpStepDeg"), 10).toInt();
    int pgInt  = s.value(QStringLiteral("spinInterval"), 10).toInt();
    m_viewport->setSpinParams(pgStep, pgInt);

    // Arrow snap animation
    int snapStep = s.value(QStringLiteral("snapStepDeg"), 10).toInt();
    int snapInt  = s.value(QStringLiteral("snapInterval"), 10).toInt();
    m_viewport->setSnapParams(snapStep, snapInt);

    // Grid
    bool showGrid = s.value(QStringLiteral("showGrid"), true).toBool();
    m_viewport->setGridVisible(showGrid);

    s.endGroup();

    // Update axis label
    static const char* names[] = { "X", "Y", "Z" };
    m_axisLabel->setText(tr("Axis: %1").arg(names[m_viewport->rotationAxis()]));
}

void FullModeWindow::displayShapes()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Remove only user shapes (AIS_Shape), preserving the trihedron
    // and any other non-shape interactive objects.
    AIS_ListOfInteractive displayed;
    ctx->DisplayedObjects(displayed);
    for (auto it = displayed.begin(); it != displayed.end(); ++it) {
        if ((*it)->IsKind(STANDARD_TYPE(AIS_Shape)))
            ctx->Remove(*it, false);
    }

    // Display each shape from the document with edge outlines
    for (const auto& shape : m_document.shapes()) {
        if (!shape.IsNull()) {
            // Shaded body
            Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
            ctx->Display(aisShape, AIS_Shaded, 0, false);

            // Wireframe overlay for visible edge outlines
            Handle(AIS_Shape) wireShape = new AIS_Shape(shape);
            Handle(Prs3d_Drawer) wireDrw = wireShape->Attributes();
            wireDrw->SetWireAspect(
                new Prs3d_LineAspect(
                    Quantity_Color(Quantity_NOC_WHITE),
                    Aspect_TOL_SOLID,
                    1.0));
            ctx->Display(wireShape, AIS_WireFrame, 0, false);
            ctx->Deactivate(wireShape);  // not selectable
        }
    }

    m_viewport->context()->UpdateCurrentViewer();
}

}  // namespace hobbycad

