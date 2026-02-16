// =====================================================================
//  src/hobbycad/gui/timelinewidget.h — Feature timeline widget
// =====================================================================
//
//  A horizontal timeline showing the history of modeling operations.
//  Displays icons with tooltips.
//  Supports scrolling without visible scrollbars - arrow buttons appear
//  at the edges when content overflows.
//
//  SPDX-License-Identifier: GPL-3.0-only
//
// =====================================================================

#ifndef HOBBYCAD_TIMELINEWIDGET_H
#define HOBBYCAD_TIMELINEWIDGET_H

#include <hobbycad/project.h>

#include <QWidget>
#include <QVector>

class QHBoxLayout;
class QToolButton;
class QScrollArea;
class QFrame;

namespace hobbycad {

/// Timeline feature types with associated icons
enum class TimelineFeature {
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

/// Dependency information for a timeline item
struct TimelineDependency {
    int featureId = 0;              ///< Unique ID for this feature
    QVector<int> dependsOn;         ///< IDs of features this depends on
    QVector<int> dependents;        ///< IDs of features that depend on this
};

// Use FeatureState from the library
using FeatureState = hobbycad::FeatureState;

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    /// Add a timeline item with icon and tooltip name (appends to end).
    void addItem(TimelineFeature feature, const QString& name);

    /// Insert a timeline item at a specific position.
    /// If atRollback is true and rollback is active, inserts after rollback position.
    void insertItem(TimelineFeature feature, const QString& name, int index);

    /// Add item at the appropriate position (after rollback if active, else at end).
    /// Returns the index where the item was inserted.
    int addItemAtRollback(TimelineFeature feature, const QString& name);

    /// Clear all timeline items.
    void clear();

    /// Get the number of items.
    int itemCount() const;

    /// Get the feature type at an index.
    TimelineFeature featureAt(int index) const;

    /// Get the feature name at an index.
    QString nameAt(int index) const;

    /// Get the currently selected item index (-1 if none).
    int selectedIndex() const;

    /// Set the selected item (pass -1 to deselect all).
    void setSelectedIndex(int index);

    /// Set the rollback marker position (items after this are suppressed).
    void setRollbackPosition(int index);

    /// Get the current rollback position (-1 = end, all features active).
    int rollbackPosition() const;

    /// Move an item from one position to another (for drag reordering).
    void moveItem(int fromIndex, int toIndex);

    /// Remove an item at the given index.
    void removeItem(int index);

    // ---- Dependency tracking ----

    /// Set the feature ID for an item (for dependency tracking).
    void setFeatureId(int index, int featureId);

    /// Get the feature ID at an index.
    int featureIdAt(int index) const;

    /// Set dependencies for an item (IDs of features it depends on).
    void setDependencies(int index, const QVector<int>& dependsOn);

    /// Get dependencies for an item.
    QVector<int> dependenciesAt(int index) const;

    /// Set the state (normal/warning/error) for an item.
    void setFeatureState(int index, FeatureState state);

    /// Get the state for an item.
    FeatureState featureStateAt(int index) const;

    /// Set individual suppression for an item (independent of rollback).
    void setFeatureSuppressed(int index, bool suppressed);

    /// Check if an item is individually suppressed.
    bool isFeatureSuppressed(int index) const;

    /// Check if moving fromIndex to toIndex would violate dependencies.
    /// Returns true if the move is valid, false if it would break dependencies.
    bool canMoveItem(int fromIndex, int toIndex) const;

    /// Get the index of a feature by its ID (-1 if not found).
    int indexOfFeatureId(int featureId) const;

    /// Get indices of all features that the item at index depends on.
    QVector<int> getParentIndices(int index) const;

    /// Get indices of all features that depend on the item at index.
    QVector<int> getDependentIndices(int index) const;

    /// Highlight dependencies when hovering over an item.
    void highlightDependencies(int index);

    /// Clear dependency highlighting.
    void clearDependencyHighlights();

    /// Update rollback position from drag (called by RollbackBar).
    void updateRollbackFromDrag(int xPos);

    /// Update the visual position of the rollback bar.
    void updateRollbackBarPosition();

    /// Start dragging an item (called by TimelineItem).
    void startDrag(int index);

    /// Update drag position (called by TimelineItem during mouse move).
    void updateDrag(const QPoint& globalPos);

    /// End dragging (called by TimelineItem on mouse release).
    void endDrag();

    /// Show context menu for an item (called by TimelineItem).
    void showItemContextMenu(int index, const QPoint& globalPos);

signals:
    /// Emitted when a timeline item is clicked.
    void itemClicked(int index);

    /// Emitted when a timeline item is double-clicked (for rollback).
    void itemDoubleClicked(int index);

    /// Emitted when rollback position changes.
    void rollbackChanged(int index);

    /// Emitted when an item is moved via drag and drop.
    void itemMoved(int fromIndex, int toIndex);

    /// Emitted when user requests to edit a feature (right-click > Edit).
    void editFeatureRequested(int index);

    /// Emitted when user requests to rename a feature.
    void renameFeatureRequested(int index);

    /// Emitted when user requests to delete a feature.
    void deleteFeatureRequested(int index);

    /// Emitted when user requests to suppress/unsuppress a feature.
    void suppressFeatureRequested(int index, bool suppress);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void scrollLeft();
    void scrollRight();
    void scrollToEnd();
    void updateArrows();

private:
    void setupUi();
    QIcon iconForFeature(TimelineFeature feature) const;
    void updateItemStyles();
    void updateTickMarks();

    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QWidget* m_iconRowWidget = nullptr;
    QHBoxLayout* m_contentLayout = nullptr;
    QWidget* m_leftArrow = nullptr;
    QWidget* m_rightArrow = nullptr;
    QWidget* m_scaleWidget = nullptr;
    QVector<class TimelineItem*> m_items;
    QVector<TimelineFeature> m_features;  // Feature type for each item
    QVector<QString> m_names;             // Name for each item
    QVector<int> m_featureIds;            // Feature ID for each item (for dependencies)
    QVector<QVector<int>> m_dependencies; // Dependencies for each item (feature IDs)
    QVector<FeatureState> m_featureStates; // Error/warning state for each item
    QVector<bool> m_suppressedStates;      // Individual suppression state for each item
    int m_scrollStep = 60;    // pixels per scroll step
    int m_rollbackPos = -1;   // -1 = end (all active)
    int m_selectedIndex = -1; // Currently selected item (-1 = none)

    // Drag state
    int m_dragIndex = -1;       // Index of item being dragged
    int m_dragOrigIndex = -1;   // Original index before drag started

    // Dependency highlighting
    int m_hoveredIndex = -1;               // Currently hovered item
    QSet<int> m_highlightedParents;        // Indices highlighted as parents
    QSet<int> m_highlightedDependents;     // Indices highlighted as dependents

    // Rollback bar
    class RollbackBar* m_rollbackBar = nullptr;
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TIMELINEWIDGET_H
