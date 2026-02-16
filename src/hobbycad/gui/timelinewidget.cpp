// =====================================================================
//  src/hobbycad/gui/timelinewidget.cpp — Feature timeline widget
// =====================================================================

#include "timelinewidget.h"

#include <QAction>
#include <QDrag>
#include <QEnterEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace hobbycad {

// ---- TimelineItem (draggable icon button) ----------------------------

class TimelineItem : public QToolButton {
public:
    TimelineItem(int index, TimelineWidget* timeline, QWidget* parent = nullptr)
        : QToolButton(parent), m_index(index), m_timeline(timeline)
    {
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QToolButton::customContextMenuRequested,
                this, &TimelineItem::showContextMenu);
    }

    int index() const { return m_index; }
    void setIndex(int idx) { m_index = idx; }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragStartPos = event->pos();
            m_dragging = false;
            setCursor(Qt::ClosedHandCursor);  // Change cursor immediately on press
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton))
            return;

        if (!m_dragging) {
            if ((event->pos() - m_dragStartPos).manhattanLength() < 8)
                return;
            m_dragging = true;
            m_timeline->startDrag(m_index);
        }

        // Map mouse position to timeline content widget
        QPoint globalPos = mapToGlobal(event->pos());
        m_timeline->updateDrag(globalPos);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            unsetCursor();  // Restore default cursor on release
        }
        if (m_dragging) {
            m_dragging = false;
            m_timeline->endDrag();
        }
        QToolButton::mouseReleaseEvent(event);
    }

    void enterEvent(QEnterEvent* event) override {
        QToolButton::enterEvent(event);
        // Highlight dependencies when hovering
        m_timeline->highlightDependencies(m_index);
    }

    void leaveEvent(QEvent* event) override {
        QToolButton::leaveEvent(event);
        // Clear dependency highlights when leaving
        m_timeline->clearDependencyHighlights();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            // Double-click sets rollback to this position
            m_timeline->setRollbackPosition(m_index);
            emit m_timeline->itemDoubleClicked(m_index);
            event->accept();
            return;
        }
        QToolButton::mouseDoubleClickEvent(event);
    }

private slots:
    void showContextMenu(const QPoint& pos) {
        m_timeline->showItemContextMenu(m_index, mapToGlobal(pos));
    }

private:
    int m_index;
    QPoint m_dragStartPos;
    bool m_dragging = false;
    TimelineWidget* m_timeline;
};

// ---- TimelineArrowButton (narrow triangle button) --------------------

class TimelineArrowButton : public QWidget {
public:
    enum Direction { Left, Right };

    TimelineArrowButton(Direction dir, QWidget* parent = nullptr)
        : QWidget(parent), m_direction(dir)
    {
        setFixedWidth(12);
        setCursor(Qt::PointingHandCursor);
    }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Fill background
        p.fillRect(rect(), m_hovered ? QColor("#4a4a4a") : QColor("#2a2a2a"));

        // Draw triangle
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#aaa"));

        int margin = 3;
        int triWidth = width() - margin * 2;
        int triHeight = 16;
        int cy = height() / 2;

        QPolygon tri;
        if (m_direction == Left) {
            tri << QPoint(margin + triWidth, cy - triHeight / 2)
                << QPoint(margin, cy)
                << QPoint(margin + triWidth, cy + triHeight / 2);
        } else {
            tri << QPoint(margin, cy - triHeight / 2)
                << QPoint(margin + triWidth, cy)
                << QPoint(margin, cy + triHeight / 2);
        }
        p.drawPolygon(tri);
    }

    void enterEvent(QEnterEvent*) override {
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        m_hovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_pressed && event->button() == Qt::LeftButton) {
            m_pressed = false;
            if (rect().contains(event->pos())) {
                // Emit clicked - find parent TimelineWidget
                if (auto* timeline = qobject_cast<TimelineWidget*>(parentWidget())) {
                    if (m_direction == Left)
                        QMetaObject::invokeMethod(timeline, "scrollLeft");
                    else
                        QMetaObject::invokeMethod(timeline, "scrollRight");
                }
            }
        }
    }

private:
    Direction m_direction;
    bool m_hovered = false;
    bool m_pressed = false;
};

// ---- RollbackBar (draggable marker) -----------------------------------

class RollbackBar : public QWidget {
public:
    explicit RollbackBar(TimelineWidget* timeline, QWidget* parent = nullptr)
        : QWidget(parent), m_timeline(timeline)
    {
        setFixedWidth(8);
        setCursor(Qt::SplitHCursor);
        setMouseTracking(true);
    }

    void setPosition(int x) {
        move(x - width() / 2, 0);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int centerX = width() / 2;

        // Draw vertical line
        p.setPen(QPen(QColor("#ff6b6b"), 2));
        p.drawLine(centerX, 4, centerX, height() - 4);

        // Draw top triangle handle
        QPolygonF topTriangle;
        topTriangle << QPointF(centerX - 4, 0)
                    << QPointF(centerX + 4, 0)
                    << QPointF(centerX, 6);
        p.setBrush(QColor("#ff6b6b"));
        p.setPen(Qt::NoPen);
        p.drawPolygon(topTriangle);

        // Draw bottom triangle handle
        QPolygonF bottomTriangle;
        bottomTriangle << QPointF(centerX - 4, height())
                       << QPointF(centerX + 4, height())
                       << QPointF(centerX, height() - 6);
        p.drawPolygon(bottomTriangle);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = event->pos().x();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging) {
            // Calculate new position relative to parent
            QPoint globalPos = mapToGlobal(event->pos());
            QPoint parentPos = parentWidget()->mapFromGlobal(globalPos);
            int newX = parentPos.x() - m_dragOffset;

            // Find which item position this corresponds to
            m_timeline->updateRollbackFromDrag(newX);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            m_dragging = false;
            setCursor(Qt::SplitHCursor);
        }
    }

    void enterEvent(QEnterEvent*) override {
        setCursor(m_dragging ? Qt::ClosedHandCursor : Qt::SplitHCursor);
    }

private:
    TimelineWidget* m_timeline;
    bool m_dragging = false;
    int m_dragOffset = 0;
};

// ---- ScaleWidget (tick marks below icons) ----------------------------

class TimelineScaleWidget : public QWidget {
public:
    explicit TimelineScaleWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(12);
    }

    void setTickPositions(const QVector<int>& positions) {
        m_tickPositions = positions;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Draw baseline at bottom
        p.setPen(QPen(QColor("#888"), 1));
        p.drawLine(0, height() - 2, width(), height() - 2);

        // Draw tick marks pointing up toward icons
        p.setPen(QPen(QColor("#aaa"), 1));
        for (int x : m_tickPositions) {
            p.drawLine(x, 0, x, height() - 2);
        }
    }

private:
    QVector<int> m_tickPositions;
};

// ---- TimelineWidget --------------------------------------------------

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void TimelineWidget::setupUi()
{
    // Dark background for the timeline
    setStyleSheet(QStringLiteral(
        "TimelineWidget { background-color: #3a3a3a; }"
    ));

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left arrow (scroll left / show earlier items)
    m_leftArrow = new TimelineArrowButton(TimelineArrowButton::Left, this);
    m_leftArrow->setVisible(false);
    mainLayout->addWidget(m_leftArrow);

    // Scroll area (no visible scrollbar)
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; }"
    ));
    mainLayout->addWidget(m_scrollArea, 1);

    // Content widget inside scroll area (vertical layout: icons + scale)
    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* contentVLayout = new QVBoxLayout(m_contentWidget);
    contentVLayout->setContentsMargins(4, 2, 4, 0);
    contentVLayout->setSpacing(0);

    // Icon row
    m_iconRowWidget = new QWidget();
    m_contentLayout = new QHBoxLayout(m_iconRowWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(2);
    m_contentLayout->addStretch();
    contentVLayout->addWidget(m_iconRowWidget);

    // Scale bar with tick marks
    m_scaleWidget = new TimelineScaleWidget();
    contentVLayout->addWidget(m_scaleWidget);

    m_scrollArea->setWidget(m_contentWidget);

    // Rollback bar (initially hidden, overlays the icon row)
    m_rollbackBar = new RollbackBar(this, m_iconRowWidget);
    m_rollbackBar->setFixedHeight(m_iconRowWidget->height() > 0 ? m_iconRowWidget->height() : 36);
    m_rollbackBar->hide();  // Hidden until rollback position is set

    // Right arrow (scroll right / show later items)
    m_rightArrow = new TimelineArrowButton(TimelineArrowButton::Right, this);
    m_rightArrow->setVisible(false);
    mainLayout->addWidget(m_rightArrow);

    // Update arrows when scrolling
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &TimelineWidget::updateArrows);
    connect(m_scrollArea->horizontalScrollBar(), &QScrollBar::rangeChanged,
            this, &TimelineWidget::updateArrows);

    // Set height to accommodate icons + scale
    setFixedHeight(50);
}

QIcon TimelineWidget::iconForFeature(TimelineFeature feature) const
{
    // Map features to freedesktop icon names with fallbacks
    QString iconName;
    QStyle::StandardPixmap fallback = QStyle::SP_FileIcon;

    switch (feature) {
    case TimelineFeature::Origin:
        iconName = QStringLiteral("crosshairs");
        fallback = QStyle::SP_ComputerIcon;
        break;
    case TimelineFeature::Sketch:
        iconName = QStringLiteral("draw-freehand");
        fallback = QStyle::SP_FileDialogDetailedView;
        break;
    case TimelineFeature::Extrude:
        iconName = QStringLiteral("go-up");
        fallback = QStyle::SP_ArrowUp;
        break;
    case TimelineFeature::Revolve:
        iconName = QStringLiteral("object-rotate-right");
        fallback = QStyle::SP_BrowserReload;
        break;
    case TimelineFeature::Fillet:
        iconName = QStringLiteral("format-stroke-color");
        fallback = QStyle::SP_DialogApplyButton;
        break;
    case TimelineFeature::Chamfer:
        iconName = QStringLiteral("draw-line");
        fallback = QStyle::SP_DialogOkButton;
        break;
    case TimelineFeature::Hole:
        iconName = QStringLiteral("draw-circle");
        fallback = QStyle::SP_DialogDiscardButton;
        break;
    case TimelineFeature::Mirror:
        iconName = QStringLiteral("object-flip-horizontal");
        fallback = QStyle::SP_ArrowBack;
        break;
    case TimelineFeature::Pattern:
        iconName = QStringLiteral("edit-copy");
        fallback = QStyle::SP_FileDialogDetailedView;
        break;
    case TimelineFeature::Box:
        iconName = QStringLiteral("draw-cube");
        fallback = QStyle::SP_ComputerIcon;
        break;
    case TimelineFeature::Cylinder:
        iconName = QStringLiteral("draw-cylinder");
        fallback = QStyle::SP_DriveHDIcon;
        break;
    case TimelineFeature::Sphere:
        iconName = QStringLiteral("draw-sphere");
        fallback = QStyle::SP_DialogHelpButton;
        break;
    case TimelineFeature::Move:
        iconName = QStringLiteral("transform-move");
        fallback = QStyle::SP_ArrowRight;
        break;
    case TimelineFeature::Join:
        iconName = QStringLiteral("list-add");
        fallback = QStyle::SP_DialogYesButton;
        break;
    case TimelineFeature::Cut:
        iconName = QStringLiteral("edit-cut");
        fallback = QStyle::SP_DialogNoButton;
        break;
    case TimelineFeature::Intersect:
        iconName = QStringLiteral("draw-cross");
        fallback = QStyle::SP_DialogResetButton;
        break;
    }

    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull())
        icon = style()->standardIcon(fallback);
    return icon;
}

void TimelineWidget::addItem(TimelineFeature feature, const QString& name)
{
    int index = m_items.size();

    auto* btn = new TimelineItem(index, this, m_iconRowWidget);
    btn->setIcon(iconForFeature(feature));
    btn->setIconSize(QSize(22, 22));
    btn->setToolTip(name);
    btn->setFixedSize(32, 32);
    btn->setAutoRaise(true);
    btn->setCheckable(true);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: #4a4a4a;"
        "  border: 1px solid #555;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a5a5a;"
        "  border-color: #888;"
        "}"
        "QToolButton:checked {"
        "  background: #6a8fbd;"
        "  border-color: #8ab4f8;"
        "}"
        "QToolTip {"
        "  background: #ffffcc;"
        "  color: #000;"
        "  border: 1px solid #000;"
        "  padding: 2px;"
        "}"
        "QToolButton:disabled {"
        "  background: #333;"
        "  border-color: #444;"
        "}"
    ));

    m_items.append(btn);
    m_features.append(feature);
    m_names.append(name);
    m_featureIds.append(0);          // Default ID, can be set with setFeatureId()
    m_dependencies.append(QVector<int>());       // No dependencies by default
    m_featureStates.append(FeatureState::Normal);
    m_suppressedStates.append(false);

    // Insert before the stretch
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, btn);

    // Connect click signal - handle exclusive selection (toggle on re-click)
    connect(btn, &QToolButton::clicked, this, [this, btn]() {
        int idx = btn->index();
        if (m_selectedIndex == idx) {
            // Clicking selected item deselects it
            setSelectedIndex(-1);
            emit itemClicked(-1);
        } else {
            setSelectedIndex(idx);
            emit itemClicked(idx);
        }
    });

    // Update tick marks and arrows, then scroll to show the new item
    QMetaObject::invokeMethod(this, &TimelineWidget::updateTickMarks, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &TimelineWidget::updateArrows, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &TimelineWidget::scrollToEnd, Qt::QueuedConnection);
}

void TimelineWidget::insertItem(TimelineFeature feature, const QString& name, int index)
{
    if (index < 0)
        index = 0;
    if (index > m_items.size())
        index = m_items.size();

    auto* btn = new TimelineItem(index, this, m_iconRowWidget);
    btn->setIcon(iconForFeature(feature));
    btn->setIconSize(QSize(22, 22));
    btn->setToolTip(name);
    btn->setFixedSize(32, 32);
    btn->setAutoRaise(true);
    btn->setCheckable(true);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: #4a4a4a;"
        "  border: 1px solid #555;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a5a5a;"
        "  border-color: #888;"
        "}"
        "QToolButton:checked {"
        "  background: #6a8fbd;"
        "  border-color: #8ab4f8;"
        "}"
        "QToolTip {"
        "  background: #ffffcc;"
        "  color: #000;"
        "  border: 1px solid #000;"
        "  padding: 2px;"
        "}"
        "QToolButton:disabled {"
        "  background: #333;"
        "  border-color: #444;"
        "}"
    ));

    // Insert into lists at position
    m_items.insert(index, btn);
    m_features.insert(index, feature);
    m_names.insert(index, name);
    m_featureIds.insert(index, 0);
    m_dependencies.insert(index, QVector<int>());
    m_featureStates.insert(index, FeatureState::Normal);
    m_suppressedStates.insert(index, false);

    // Insert into layout at position
    m_contentLayout->insertWidget(index, btn);

    // Update indices for all items
    for (int i = 0; i < m_items.size(); ++i)
        m_items[i]->setIndex(i);

    // Update rollback position if inserting before it
    if (m_rollbackPos >= 0 && index <= m_rollbackPos) {
        m_rollbackPos++;
    }

    // Connect click signal
    connect(btn, &QToolButton::clicked, this, [this, btn]() {
        int idx = btn->index();
        if (m_selectedIndex == idx) {
            setSelectedIndex(-1);
            emit itemClicked(-1);
        } else {
            setSelectedIndex(idx);
            emit itemClicked(idx);
        }
    });

    // Update visuals
    updateItemStyles();
    QMetaObject::invokeMethod(this, &TimelineWidget::updateTickMarks, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &TimelineWidget::updateArrows, Qt::QueuedConnection);
}

int TimelineWidget::addItemAtRollback(TimelineFeature feature, const QString& name)
{
    int insertIndex;

    if (m_rollbackPos >= 0 && m_rollbackPos < m_items.size() - 1) {
        // Insert after rollback position
        insertIndex = m_rollbackPos + 1;
        insertItem(feature, name, insertIndex);

        // Move rollback forward to include the new item
        m_rollbackPos++;
        updateItemStyles();
        updateRollbackBarPosition();
    } else {
        // No rollback or at end - just append
        insertIndex = m_items.size();
        addItem(feature, name);
    }

    return insertIndex;
}

void TimelineWidget::removeItem(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    auto* item = m_items[index];
    m_contentLayout->removeWidget(item);
    delete item;

    m_items.remove(index);
    m_features.remove(index);
    m_names.remove(index);
    if (index < m_featureIds.size())
        m_featureIds.remove(index);
    if (index < m_dependencies.size())
        m_dependencies.remove(index);
    if (index < m_featureStates.size())
        m_featureStates.remove(index);
    if (index < m_suppressedStates.size())
        m_suppressedStates.remove(index);

    // Update selection index if needed
    if (m_selectedIndex == index) {
        m_selectedIndex = -1;
    } else if (m_selectedIndex > index) {
        m_selectedIndex--;
    }

    // Update rollback position if needed
    if (m_rollbackPos >= index) {
        m_rollbackPos--;
    }

    // Update indices for remaining items
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i]->setIndex(i);
    }

    updateTickMarks();
    updateArrows();
}

void TimelineWidget::showItemContextMenu(int index, const QPoint& globalPos)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Select the item when showing context menu
    setSelectedIndex(index);
    emit itemClicked(index);

    TimelineFeature feature = m_features[index];
    QString name = m_names[index];
    bool isSuppressed = (m_rollbackPos >= 0 && index > m_rollbackPos);

    QMenu menu(this);

    // Edit action (for sketches and features that can be edited)
    QAction* editAction = menu.addAction(tr("Edit \"%1\"").arg(name));
    editAction->setEnabled(feature != TimelineFeature::Origin);
    connect(editAction, &QAction::triggered, this, [this, index]() {
        emit editFeatureRequested(index);
    });

    menu.addSeparator();

    // Rename action
    QAction* renameAction = menu.addAction(tr("Rename..."));
    renameAction->setEnabled(feature != TimelineFeature::Origin);
    connect(renameAction, &QAction::triggered, this, [this, index]() {
        emit renameFeatureRequested(index);
    });

    menu.addSeparator();

    // Suppress/Unsuppress action
    if (isSuppressed) {
        QAction* unsuppressAction = menu.addAction(tr("Unsuppress"));
        connect(unsuppressAction, &QAction::triggered, this, [this, index]() {
            emit suppressFeatureRequested(index, false);
        });
    } else {
        QAction* suppressAction = menu.addAction(tr("Suppress"));
        suppressAction->setEnabled(feature != TimelineFeature::Origin);
        connect(suppressAction, &QAction::triggered, this, [this, index]() {
            emit suppressFeatureRequested(index, true);
        });
    }

    // Rollback to here
    QAction* rollbackAction = menu.addAction(tr("Rollback to Here"));
    rollbackAction->setEnabled(feature != TimelineFeature::Origin);
    connect(rollbackAction, &QAction::triggered, this, [this, index]() {
        setRollbackPosition(index);
        emit rollbackChanged(index);
    });

    menu.addSeparator();

    // Delete action
    QAction* deleteAction = menu.addAction(tr("Delete"));
    deleteAction->setEnabled(feature != TimelineFeature::Origin);
    connect(deleteAction, &QAction::triggered, this, [this, index]() {
        emit deleteFeatureRequested(index);
    });

    menu.exec(globalPos);
}

void TimelineWidget::moveItem(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_items.size())
        return;
    if (toIndex < 0 || toIndex >= m_items.size())
        return;
    if (fromIndex == toIndex)
        return;

    // Remove from layout
    auto* item = m_items[fromIndex];
    m_contentLayout->removeWidget(item);

    // Reorder in our lists
    m_items.remove(fromIndex);
    m_items.insert(toIndex, item);

    auto feature = m_features[fromIndex];
    m_features.remove(fromIndex);
    m_features.insert(toIndex, feature);

    auto name = m_names[fromIndex];
    m_names.remove(fromIndex);
    m_names.insert(toIndex, name);

    // Move feature ID
    if (fromIndex < m_featureIds.size()) {
        auto featureId = m_featureIds[fromIndex];
        m_featureIds.remove(fromIndex);
        m_featureIds.insert(toIndex, featureId);
    }

    // Move dependencies
    if (fromIndex < m_dependencies.size()) {
        auto deps = m_dependencies[fromIndex];
        m_dependencies.remove(fromIndex);
        m_dependencies.insert(toIndex, deps);
    }

    // Move feature states
    if (fromIndex < m_featureStates.size()) {
        auto state = m_featureStates[fromIndex];
        m_featureStates.remove(fromIndex);
        m_featureStates.insert(toIndex, state);
    }

    // Move suppressed states
    if (fromIndex < m_suppressedStates.size()) {
        auto suppressed = m_suppressedStates[fromIndex];
        m_suppressedStates.remove(fromIndex);
        m_suppressedStates.insert(toIndex, suppressed);
    }

    // Update selection index if needed
    if (m_selectedIndex == fromIndex) {
        m_selectedIndex = toIndex;
    } else if (fromIndex < toIndex) {
        if (m_selectedIndex > fromIndex && m_selectedIndex <= toIndex)
            m_selectedIndex--;
    } else {
        if (m_selectedIndex >= toIndex && m_selectedIndex < fromIndex)
            m_selectedIndex++;
    }

    // Update indices
    for (int i = 0; i < m_items.size(); ++i)
        m_items[i]->setIndex(i);

    // Re-insert into layout at new position
    m_contentLayout->insertWidget(toIndex, item);

    // Update tick marks
    updateTickMarks();

    emit itemMoved(fromIndex, toIndex);
}

// ---- Dependency tracking ----

void TimelineWidget::setFeatureId(int index, int featureId)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Ensure vector is large enough
    while (m_featureIds.size() <= index)
        m_featureIds.append(0);

    m_featureIds[index] = featureId;
}

int TimelineWidget::featureIdAt(int index) const
{
    if (index < 0 || index >= m_featureIds.size())
        return 0;
    return m_featureIds[index];
}

void TimelineWidget::setDependencies(int index, const QVector<int>& dependsOn)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Ensure vector is large enough
    while (m_dependencies.size() <= index)
        m_dependencies.append(QVector<int>());

    m_dependencies[index] = dependsOn;
}

QVector<int> TimelineWidget::dependenciesAt(int index) const
{
    if (index < 0 || index >= m_dependencies.size())
        return {};
    return m_dependencies[index];
}

int TimelineWidget::indexOfFeatureId(int featureId) const
{
    for (int i = 0; i < m_featureIds.size(); ++i) {
        if (m_featureIds[i] == featureId)
            return i;
    }
    return -1;
}

bool TimelineWidget::canMoveItem(int fromIndex, int toIndex) const
{
    if (fromIndex < 0 || fromIndex >= m_items.size())
        return false;
    if (toIndex < 0 || toIndex >= m_items.size())
        return false;
    if (fromIndex == toIndex)
        return true;

    // Can't move Origin (index 0)
    if (fromIndex == 0 || toIndex == 0)
        return false;

    int featureId = featureIdAt(fromIndex);

    if (fromIndex < toIndex) {
        // Moving down (later in timeline)
        // Check if any item between fromIndex+1 and toIndex depends on this feature
        for (int i = fromIndex + 1; i <= toIndex; ++i) {
            QVector<int> deps = dependenciesAt(i);
            if (deps.contains(featureId)) {
                // Can't move past a dependent feature
                return false;
            }
        }
    } else {
        // Moving up (earlier in timeline)
        // Check if the moved item depends on any feature between toIndex and fromIndex-1
        QVector<int> myDeps = dependenciesAt(fromIndex);
        for (int i = toIndex; i < fromIndex; ++i) {
            int otherId = featureIdAt(i);
            if (myDeps.contains(otherId)) {
                // Can't move before a feature this depends on
                return false;
            }
        }
    }

    return true;
}

void TimelineWidget::clear()
{
    for (auto* btn : m_items) {
        m_contentLayout->removeWidget(btn);
        delete btn;
    }
    m_items.clear();
    m_features.clear();
    m_names.clear();
    m_featureIds.clear();
    m_dependencies.clear();
    m_featureStates.clear();
    m_suppressedStates.clear();
    m_rollbackPos = -1;
    m_selectedIndex = -1;
    updateTickMarks();
    updateArrows();
}

int TimelineWidget::itemCount() const
{
    return m_items.size();
}

TimelineFeature TimelineWidget::featureAt(int index) const
{
    if (index < 0 || index >= m_features.size())
        return TimelineFeature::Origin;  // Default fallback
    return m_features[index];
}

QString TimelineWidget::nameAt(int index) const
{
    if (index < 0 || index >= m_names.size())
        return QString();
    return m_names[index];
}

int TimelineWidget::selectedIndex() const
{
    return m_selectedIndex;
}

void TimelineWidget::setSelectedIndex(int index)
{
    if (index == m_selectedIndex)
        return;

    // Deselect previous
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size()) {
        m_items[m_selectedIndex]->setChecked(false);
    }

    m_selectedIndex = index;

    // Select new
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size()) {
        m_items[m_selectedIndex]->setChecked(true);
    }
}

void TimelineWidget::setRollbackPosition(int index)
{
    if (index == m_rollbackPos)
        return;

    m_rollbackPos = index;
    updateItemStyles();
    updateRollbackBarPosition();
    emit rollbackChanged(index);
}

int TimelineWidget::rollbackPosition() const
{
    return m_rollbackPos;
}

void TimelineWidget::updateRollbackBarPosition()
{
    if (!m_rollbackBar)
        return;

    if (m_rollbackPos < 0 || m_rollbackPos >= m_items.size()) {
        // No rollback position or at end - hide the bar
        m_rollbackBar->hide();
        return;
    }

    // Position the bar after the rollback item
    auto* item = m_items[m_rollbackPos];
    int xPos = item->x() + item->width() + 1;  // Right edge of rollback item
    m_rollbackBar->setPosition(xPos);
    m_rollbackBar->setFixedHeight(item->height());
    m_rollbackBar->show();
    m_rollbackBar->raise();  // Ensure it's on top
}

void TimelineWidget::updateRollbackFromDrag(int xPos)
{
    // Find which item position this corresponds to
    // The rollback bar should be placed AFTER the item we're rolling back to

    int newRollbackPos = -1;

    for (int i = 0; i < m_items.size(); ++i) {
        auto* item = m_items[i];
        int itemCenter = item->x() + item->width() / 2;

        if (xPos < itemCenter) {
            // We're before this item's center, so rollback to previous item
            newRollbackPos = i - 1;
            break;
        }
        newRollbackPos = i;
    }

    // Don't allow rollback before Origin (must be at least 0 or -1 for "no rollback")
    // If dragged past all items to the right, set to -1 (no rollback)
    if (newRollbackPos >= m_items.size() - 1) {
        newRollbackPos = -1;  // Rollback to end = no rollback
    }

    if (newRollbackPos != m_rollbackPos) {
        setRollbackPosition(newRollbackPos);
    }
}

void TimelineWidget::updateItemStyles()
{
    // Items after rollback position are "suppressed" (grayed out with strikethrough effect)
    QString normalStyle = QStringLiteral(
        "QToolButton {"
        "  background: #4a4a4a;"
        "  border: 1px solid #555;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a5a5a;"
        "  border-color: #888;"
        "}"
        "QToolButton:checked {"
        "  background: #6a8fbd;"
        "  border-color: #8ab4f8;"
        "}"
        "QToolTip {"
        "  background: #ffffcc;"
        "  color: #000;"
        "  border: 1px solid #000;"
        "  padding: 2px;"
        "}"
    );

    QString suppressedStyle = QStringLiteral(
        "QToolButton {"
        "  background: #2a2a2a;"
        "  border: 1px solid #383838;"
        "  border-radius: 2px;"
        "  color: #666;"
        "}"
        "QToolButton:hover {"
        "  background: #333;"
        "  border-color: #444;"
        "}"
        "QToolButton:checked {"
        "  background: #4a5a6a;"
        "  border-color: #5a6a7a;"
        "}"
    );

    // Error state styles
    QString errorStyle = QStringLiteral(
        "QToolButton {"
        "  background: #4a3030;"
        "  border: 2px solid #ff4444;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a4040;"
        "  border-color: #ff6666;"
        "}"
    );

    QString warningStyle = QStringLiteral(
        "QToolButton {"
        "  background: #4a4a30;"
        "  border: 2px solid #ffaa00;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a5a40;"
        "  border-color: #ffcc44;"
        "}"
    );

    for (int i = 0; i < m_items.size(); ++i) {
        // Check individual suppression OR rollback suppression
        bool individualSuppressed = (i < m_suppressedStates.size() && m_suppressedStates[i]);
        bool rollbackSuppressed = (m_rollbackPos >= 0 && i > m_rollbackPos);
        bool suppressed = individualSuppressed || rollbackSuppressed;

        // Check feature state for error/warning
        FeatureState state = (i < m_featureStates.size()) ? m_featureStates[i] : FeatureState::Normal;

        // Apply appropriate style
        if (suppressed) {
            m_items[i]->setEnabled(false);
            m_items[i]->setStyleSheet(suppressedStyle);
        } else if (state == FeatureState::Error) {
            m_items[i]->setEnabled(true);
            m_items[i]->setStyleSheet(errorStyle);
        } else if (state == FeatureState::Warning) {
            m_items[i]->setEnabled(true);
            m_items[i]->setStyleSheet(warningStyle);
        } else {
            m_items[i]->setEnabled(true);
            m_items[i]->setStyleSheet(normalStyle);
        }

        // Update icon opacity for suppressed items
        if (suppressed) {
            // Dim the icon by setting a semi-transparent effect
            auto* effect = new QGraphicsOpacityEffect(m_items[i]);
            effect->setOpacity(0.4);
            m_items[i]->setGraphicsEffect(effect);
        } else {
            m_items[i]->setGraphicsEffect(nullptr);
        }
    }
}

void TimelineWidget::updateTickMarks()
{
    QVector<int> positions;

    for (auto* btn : m_items) {
        // Get the center x position of each button relative to content widget
        int centerX = btn->x() + btn->width() / 2;
        positions.append(centerX);
    }

    if (m_scaleWidget)
        static_cast<TimelineScaleWidget*>(m_scaleWidget)->setTickPositions(positions);

    // Also update rollback bar position
    updateRollbackBarPosition();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateArrows();
    // Delay tick update to allow layout to settle
    QMetaObject::invokeMethod(this, &TimelineWidget::updateTickMarks, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, &TimelineWidget::updateRollbackBarPosition, Qt::QueuedConnection);
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    // Forward wheel events to scroll the timeline
    QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
    int delta = event->angleDelta().y();
    if (delta != 0) {
        hbar->setValue(hbar->value() - delta);
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void TimelineWidget::scrollLeft()
{
    QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
    hbar->setValue(hbar->value() - m_scrollStep);
}

void TimelineWidget::scrollRight()
{
    QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
    hbar->setValue(hbar->value() + m_scrollStep);
}

void TimelineWidget::scrollToEnd()
{
    QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
    hbar->setValue(hbar->maximum());
}

void TimelineWidget::updateArrows()
{
    QScrollBar* hbar = m_scrollArea->horizontalScrollBar();
    int min = hbar->minimum();
    int max = hbar->maximum();
    int val = hbar->value();

    // Show left arrow if we can scroll left (not at minimum)
    m_leftArrow->setVisible(val > min);

    // Show right arrow if we can scroll right (not at maximum)
    m_rightArrow->setVisible(val < max);
}

void TimelineWidget::startDrag(int index)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Don't allow dragging the Origin (always at index 0)
    if (index == 0 && m_features[0] == TimelineFeature::Origin)
        return;

    m_dragIndex = index;
    m_dragOrigIndex = index;
}

void TimelineWidget::updateDrag(const QPoint& globalPos)
{
    if (m_dragIndex < 0 || m_dragIndex >= m_items.size())
        return;

    // Convert global pos to icon row widget coordinates
    QPoint localPos = m_iconRowWidget->mapFromGlobal(globalPos);

    // Check if cursor has moved into an adjacent icon's bounds
    // Only swap with immediate neighbors for smooth dragging

    // Check left neighbor (but don't swap with Origin at index 0)
    if (m_dragIndex > 0) {
        int targetIndex = m_dragIndex - 1;

        // Don't allow moving before Origin
        if (targetIndex == 0 && m_features[0] == TimelineFeature::Origin) {
            // Skip - can't move before Origin
        } else if (!canMoveItem(m_dragIndex, targetIndex)) {
            // Skip - dependency violation
        } else {
            auto* leftItem = m_items[targetIndex];
            QRect leftRect(leftItem->x(), leftItem->y(),
                           leftItem->width(), leftItem->height());
            if (leftRect.contains(localPos)) {
                // Swap with left neighbor
                moveItem(m_dragIndex, targetIndex);
                m_dragIndex = targetIndex;
                return;
            }
        }
    }

    // Check right neighbor
    if (m_dragIndex < m_items.size() - 1) {
        int targetIndex = m_dragIndex + 1;

        // Check dependency constraints
        if (!canMoveItem(m_dragIndex, targetIndex)) {
            // Skip - dependency violation
            return;
        }

        auto* rightItem = m_items[targetIndex];
        QRect rightRect(rightItem->x(), rightItem->y(),
                        rightItem->width(), rightItem->height());
        if (rightRect.contains(localPos)) {
            // Swap with right neighbor
            moveItem(m_dragIndex, targetIndex);
            m_dragIndex = targetIndex;
            return;
        }
    }
}

void TimelineWidget::endDrag()
{
    if (m_dragIndex >= 0 && m_dragIndex < m_items.size()) {
        // Emit signal if position changed
        if (m_dragOrigIndex != m_dragIndex) {
            emit itemMoved(m_dragOrigIndex, m_dragIndex);
        }
    }

    m_dragIndex = -1;
    m_dragOrigIndex = -1;
}

// ---- Dependency visualization ----

QVector<int> TimelineWidget::getParentIndices(int index) const
{
    QVector<int> parents;
    QVector<int> deps = dependenciesAt(index);

    for (int depId : deps) {
        int parentIdx = indexOfFeatureId(depId);
        if (parentIdx >= 0) {
            parents.append(parentIdx);
        }
    }

    return parents;
}

QVector<int> TimelineWidget::getDependentIndices(int index) const
{
    QVector<int> dependents;
    int featureId = featureIdAt(index);

    if (featureId == 0)
        return dependents;  // Origin has no dependents tracked explicitly

    // Check all items to see which depend on this feature
    for (int i = 0; i < m_items.size(); ++i) {
        if (i == index)
            continue;

        QVector<int> deps = dependenciesAt(i);
        if (deps.contains(featureId)) {
            dependents.append(i);
        }
    }

    return dependents;
}

void TimelineWidget::highlightDependencies(int index)
{
    if (index == m_hoveredIndex)
        return;  // Already highlighted

    clearDependencyHighlights();
    m_hoveredIndex = index;

    if (index < 0 || index >= m_items.size())
        return;

    // Get parents (features this depends on)
    QVector<int> parents = getParentIndices(index);
    for (int parentIdx : parents) {
        m_highlightedParents.insert(parentIdx);
        if (parentIdx >= 0 && parentIdx < m_items.size()) {
            // Apply parent highlight style (e.g., blue border)
            m_items[parentIdx]->setStyleSheet(QStringLiteral(
                "QToolButton {"
                "  background: #4a4a4a;"
                "  border: 2px solid #4a90d9;"  // Blue border for parents
                "  border-radius: 2px;"
                "}"
                "QToolButton:hover {"
                "  background: #5a5a5a;"
                "  border-color: #6ab0ff;"
                "}"
                "QToolButton:checked {"
                "  background: #6a8fbd;"
                "  border-color: #8ab4f8;"
                "}"
            ));
        }
    }

    // Get dependents (features that depend on this)
    QVector<int> dependents = getDependentIndices(index);
    for (int depIdx : dependents) {
        m_highlightedDependents.insert(depIdx);
        if (depIdx >= 0 && depIdx < m_items.size()) {
            // Apply dependent highlight style (e.g., orange border)
            m_items[depIdx]->setStyleSheet(QStringLiteral(
                "QToolButton {"
                "  background: #4a4a4a;"
                "  border: 2px solid #d98a4a;"  // Orange border for dependents
                "  border-radius: 2px;"
                "}"
                "QToolButton:hover {"
                "  background: #5a5a5a;"
                "  border-color: #ffab6a;"
                "}"
                "QToolButton:checked {"
                "  background: #6a8fbd;"
                "  border-color: #8ab4f8;"
                "}"
            ));
        }
    }
}

void TimelineWidget::clearDependencyHighlights()
{
    // Reset all highlighted items to default style
    QString defaultStyle = QStringLiteral(
        "QToolButton {"
        "  background: #4a4a4a;"
        "  border: 1px solid #555;"
        "  border-radius: 2px;"
        "}"
        "QToolButton:hover {"
        "  background: #5a5a5a;"
        "  border-color: #888;"
        "}"
        "QToolButton:checked {"
        "  background: #6a8fbd;"
        "  border-color: #8ab4f8;"
        "}"
        "QToolTip {"
        "  background: #ffffcc;"
        "  color: #000;"
        "  border: 1px solid #000;"
        "  padding: 2px;"
        "}"
        "QToolButton:disabled {"
        "  background: #333;"
        "  border-color: #444;"
        "}"
    );

    for (int idx : m_highlightedParents) {
        if (idx >= 0 && idx < m_items.size()) {
            m_items[idx]->setStyleSheet(defaultStyle);
        }
    }

    for (int idx : m_highlightedDependents) {
        if (idx >= 0 && idx < m_items.size()) {
            m_items[idx]->setStyleSheet(defaultStyle);
        }
    }

    m_highlightedParents.clear();
    m_highlightedDependents.clear();
    m_hoveredIndex = -1;
}

// ---- Feature state and suppression ----

void TimelineWidget::setFeatureState(int index, FeatureState state)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Ensure vector is large enough
    while (m_featureStates.size() <= index)
        m_featureStates.append(FeatureState::Normal);

    m_featureStates[index] = state;

    // Update the item visually - add badge overlay
    // We'll repaint with error/warning indicator
    m_items[index]->update();
}

FeatureState TimelineWidget::featureStateAt(int index) const
{
    if (index < 0 || index >= m_featureStates.size())
        return FeatureState::Normal;
    return m_featureStates[index];
}

void TimelineWidget::setFeatureSuppressed(int index, bool suppressed)
{
    if (index < 0 || index >= m_items.size())
        return;

    // Ensure vector is large enough
    while (m_suppressedStates.size() <= index)
        m_suppressedStates.append(false);

    m_suppressedStates[index] = suppressed;
    updateItemStyles();
}

bool TimelineWidget::isFeatureSuppressed(int index) const
{
    if (index < 0 || index >= m_suppressedStates.size())
        return false;
    return m_suppressedStates[index];
}

}  // namespace hobbycad
