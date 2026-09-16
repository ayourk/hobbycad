// =====================================================================
//  src/libhobbycad/hobbycad/feature.h — Modeling history feature record
// =====================================================================
//
//  The record of one feature in the modeling history: what it is, what it
//  depends on, and whether it is currently valid.
//
//  Split out of project.h so that code reasoning about the FEATURE RECIPE
//  does not have to include the geometry model. project.h pulls in
//  <TopoDS_Shape.hxx>, and a feature record has nothing to do with OCCT;
//  the document undo stack needs these types and should not drag a CAD
//  kernel in behind them.
//
//  Part of libhobbycad.
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_FEATURE_H
#define HOBBYCAD_FEATURE_H

#include "core.h"
// types.h is what DEFINES HOBBYCAD_HAS_QT, and the #if below selects
// FeatureData's `properties` member type. Without this include the macro
// is undefined (which the preprocessor reads as 0), so the struct's
// layout depended on whether some earlier header happened to pull types.h
// in first. Two translation units could then disagree about what a
// FeatureData is, which is an ODR violation and crashed as one: moving a
// QJsonObject that the other side had built as an nlohmann::json.
#include "types.h"

#include <string>
#include <vector>

#if HOBBYCAD_HAS_QT
#include <QJsonObject>
#else
#include <nlohmann/json.hpp>
#endif

namespace hobbycad {

/// Feature type in the modeling history
enum class FeatureType {
    Origin,
    Sketch,
    Extrude,
    Revolve,
    Fillet,
    Chamfer,
    Hole,
    Mirror,
    Pattern,
    Box,
    Cylinder,
    Sphere,
    Move,
    Join,
    Cut,
    Intersect
};

/// Feature state for validation status
/// Used to indicate errors or warnings from constraint solving, BREP operations, etc.
enum class FeatureState {
    Normal,     ///< Feature is valid
    Warning,    ///< Feature has a warning (e.g., over-constrained sketch)
    Error       ///< Feature has an error (e.g., failed BREP operation, conflicting constraints)
};

/// A single feature in the history tree
struct FeatureData {
    int id = 0;
    FeatureType type = FeatureType::Origin;
    std::string name;
#if HOBBYCAD_HAS_QT
    QJsonObject properties;  ///< Feature-specific properties
#else
    nlohmann::json properties = nlohmann::json::object();  ///< Feature-specific properties
#endif
    std::vector<int> dependsOn;  ///< IDs of features this depends on (parents)
    bool suppressed = false; ///< True if feature is suppressed
    FeatureState state = FeatureState::Normal;  ///< Validation state
    std::string stateMessage;    ///< Human-readable error/warning message
};


}  // namespace hobbycad

#endif  // HOBBYCAD_FEATURE_H
