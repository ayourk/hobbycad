// =====================================================================
//  src/hobbycad/gui/timelinewidget.cpp — Feature timeline widget
// =====================================================================

#include "timelinewidget.h"

#include <QDrag>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
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

void TimelineWidget::clear()
{
    for (auto* btn : m_items) {
        m_contentLayout->removeWidget(btn);
        delete btn;
    }
    m_items.clear();
    m_features.clear();
    m_names.clear();
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
    emit rollbackChanged(index);
}

int TimelineWidget::rollbackPosition() const
{
    return m_rollbackPos;
}

void TimelineWidget::updateItemStyles()
{
    // Items after rollback position are "suppressed" (grayed out)
    for (int i = 0; i < m_items.size(); ++i) {
        bool suppressed = (m_rollbackPos >= 0 && i > m_rollbackPos);
        m_items[i]->setEnabled(!suppressed);
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
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateArrows();
    // Delay tick update to allow layout to settle
    QMetaObject::invokeMethod(this, &TimelineWidget::updateTickMarks, Qt::QueuedConnection);
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

    // Check left neighbor
    if (m_dragIndex > 0) {
        auto* leftItem = m_items[m_dragIndex - 1];
        QRect leftRect(leftItem->x(), leftItem->y(),
                       leftItem->width(), leftItem->height());
        if (leftRect.contains(localPos)) {
            // Swap with left neighbor
            int targetIndex = m_dragIndex - 1;
            moveItem(m_dragIndex, targetIndex);
            m_dragIndex = targetIndex;
            return;
        }
    }

    // Check right neighbor
    if (m_dragIndex < m_items.size() - 1) {
        auto* rightItem = m_items[m_dragIndex + 1];
        QRect rightRect(rightItem->x(), rightItem->y(),
                        rightItem->width(), rightItem->height());
        if (rightRect.contains(localPos)) {
            // Swap with right neighbor
            int targetIndex = m_dragIndex + 1;
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

}  // namespace hobbycad
