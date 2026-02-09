// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.cpp — Full Mode window
// =====================================================================

#include "fullmodewindow.h"
#include "viewportwidget.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>

namespace hobbycad {

FullModeWindow::FullModeWindow(const OpenGLInfo& glInfo, QWidget* parent)
    : MainWindow(glInfo, parent)
{
    setObjectName(QStringLiteral("FullModeWindow"));
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);
    finalizeLayout();

    // Display the initial test solid
    displayShapes();
}

void FullModeWindow::onDocumentLoaded()
{
    displayShapes();
}

void FullModeWindow::displayShapes()
{
    if (!m_viewport || m_viewport->context().IsNull()) return;

    auto ctx = m_viewport->context();

    // Clear existing displayed objects
    ctx->RemoveAll(false);

    // Display each shape from the document
    for (const auto& shape : m_document.shapes()) {
        if (!shape.IsNull()) {
            Handle(AIS_Shape) aisShape = new AIS_Shape(shape);
            ctx->Display(aisShape, AIS_Shaded, 0, false);
        }
    }

    m_viewport->fitAll();
}

}  // namespace hobbycad

