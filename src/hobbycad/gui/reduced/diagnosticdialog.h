// =====================================================================
//  src/hobbycad/gui/reduced/diagnosticdialog.h — GPU diagnostic dialog
// =====================================================================
//
//  Shown on entering Reduced Mode and when the user clicks the
//  disabled viewport.  Displays GPU info, upgrade guidance, and
//  a "Copy to Clipboard" button.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_DIAGNOSTICDIALOG_H
#define HOBBYCAD_DIAGNOSTICDIALOG_H

#include <hobbycad/opengl_info.h>

#include <QDialog>

class QCheckBox;

namespace hobbycad {

class DiagnosticDialog : public QDialog {
    Q_OBJECT

public:
    explicit DiagnosticDialog(const OpenGLInfo& glInfo,
                              QWidget* parent = nullptr);

    /// True if the user checked "don't show again."
    bool dontShowAgain() const;

protected:
    /// ESC key exits the application — continuing requires explicit action.
    void reject() override;

private:
    QString buildGuidanceText(const OpenGLInfo& glInfo) const;

    QCheckBox* m_dontShowCheck = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_DIAGNOSTICDIALOG_H

