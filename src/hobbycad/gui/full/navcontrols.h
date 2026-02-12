// =====================================================================
//  src/hobbycad/gui/full/navcontrols.h — shared nav control types
// =====================================================================
//
//  Common identifiers and the custom entity-owner used by NavOrbitRing
//  and NavHomeButton (and any future viewport controls).
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_NAVCONTROLS_H
#define HOBBYCAD_NAVCONTROLS_H

#include <SelectMgr_EntityOwner.hxx>

namespace hobbycad {

// ---- Control identifiers --------------------------------------------

/// Each clickable region in the viewport navigation controls.
enum NavControlId {
    NavCtrl_None = 0,
    NavCtrl_XPlus,    ///< Animated +90° rotation around X axis
    NavCtrl_XMinus,   ///< Animated −90° rotation around X axis
    NavCtrl_YPlus,    ///< Animated +90° rotation around Y axis
    NavCtrl_YMinus,   ///< Animated −90° rotation around Y axis
    NavCtrl_ZPlus,    ///< Animated +90° rotation around Z axis
    NavCtrl_ZMinus,   ///< Animated −90° rotation around Z axis
    NavCtrl_Home,     ///< Reset camera to home view
};

// ---- Custom entity owner --------------------------------------------

/// SelectMgr_EntityOwner subclass that carries a NavControlId so the
/// viewport can identify which control was clicked.
class NavControlOwner : public SelectMgr_EntityOwner {
public:
    NavControlOwner(const Handle(SelectMgr_SelectableObject)& theObj,
                    NavControlId theCtrl,
                    Standard_Integer thePriority = 7)
        : SelectMgr_EntityOwner(theObj, thePriority)
        , m_controlId(theCtrl) {}

    NavControlId ControlId() const { return m_controlId; }

    DEFINE_STANDARD_RTTI_INLINE(NavControlOwner, SelectMgr_EntityOwner)

private:
    NavControlId m_controlId;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_NAVCONTROLS_H
