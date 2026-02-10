// =====================================================================
//  src/hobbycad/gui/full/fullmodewindow.h — Full Mode window (OpenGL 3.3+)
// =====================================================================

#ifndef HOBBYCAD_FULLMODEWINDOW_H
#define HOBBYCAD_FULLMODEWINDOW_H

#include "gui/mainwindow.h"

class QLabel;

namespace hobbycad {

class ViewportWidget;

class FullModeWindow : public MainWindow {
    Q_OBJECT

public:
    explicit FullModeWindow(const OpenGLInfo& glInfo,
                            QWidget* parent = nullptr);

protected:
    void onDocumentLoaded() override;
    void onDocumentClosed() override;
    void applyPreferences() override;

private:
    void displayShapes();

    ViewportWidget* m_viewport = nullptr;
    QLabel*         m_axisLabel = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_FULLMODEWINDOW_H

