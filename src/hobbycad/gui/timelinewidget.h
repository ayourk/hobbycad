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

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    /// Add a timeline item with icon and tooltip name.
    void addItem(TimelineFeature feature, const QString& name);

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

    /// Start dragging an item (called by TimelineItem).
    void startDrag(int index);

    /// Update drag position (called by TimelineItem during mouse move).
    void updateDrag(const QPoint& globalPos);

    /// End dragging (called by TimelineItem on mouse release).
    void endDrag();

signals:
    /// Emitted when a timeline item is clicked.
    void itemClicked(int index);

    /// Emitted when a timeline item is double-clicked (for rollback).
    void itemDoubleClicked(int index);

    /// Emitted when rollback position changes.
    void rollbackChanged(int index);

    /// Emitted when an item is moved via drag and drop.
    void itemMoved(int fromIndex, int toIndex);

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
    int m_scrollStep = 60;    // pixels per scroll step
    int m_rollbackPos = -1;   // -1 = end (all active)
    int m_selectedIndex = -1; // Currently selected item (-1 = none)

    // Drag state
    int m_dragIndex = -1;       // Index of item being dragged
    int m_dragOrigIndex = -1;   // Original index before drag started
};

}  // namespace hobbycad

#endif  // HOBBYCAD_TIMELINEWIDGET_H
