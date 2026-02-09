// =====================================================================
//  src/hobbycad/gui/aboutdialog.h — About HobbyCAD dialog
// =====================================================================

#ifndef HOBBYCAD_ABOUTDIALOG_H
#define HOBBYCAD_ABOUTDIALOG_H

#include <hobbycad/opengl_info.h>
#include <QDialog>

namespace hobbycad {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(const OpenGLInfo& glInfo,
                         QWidget* parent = nullptr);
};

}  // namespace hobbycad

#endif  // HOBBYCAD_ABOUTDIALOG_H

