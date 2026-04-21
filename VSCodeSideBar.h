#ifndef VSCODESIDEBAR_H
#define VSCODESIDEBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStackedWidget>
#include <QSplitter>
#include <QLabel>
#include <QIcon>
#include <QPropertyAnimation>
#include <QStyle>
#include <QPainter>
#include <QPixmap>
#include <QMouseEvent>
#include <QSet>

// ============================================================
// 平台像素尺寸辅助（仅在 VSCodeSideBar.h 内使用）
// macOS Retina 物理密度高，同等逻辑像素视觉偏大；
// Windows 普通屏需要放大约 1.3x 才有相近视觉效果。
// ============================================================
namespace SideBarPlatform {
#if defined(Q_OS_WIN)
    static constexpr int kActivityBarWidth  = 52;   // macOS: 44
    static constexpr int kButtonSize        = 48;   // macOS: 40
    static constexpr int kIconSize          = 28;   // macOS: 22
    static constexpr int kPanelMinWidth     = 180;  // macOS: 150
    static constexpr int kPanelDefaultWidth = 300;  // macOS: 280
#else
    static constexpr int kActivityBarWidth  = 44;
    static constexpr int kButtonSize        = 40;
    static constexpr int kIconSize          = 22;
    static constexpr int kPanelMinWidth     = 150;
    static constexpr int kPanelDefaultWidth = 280;
#endif
} // namespace SideBarPlatform

// ============================================================
// SideBarIcons — 程序化生成侧边栏图标（避免与工具栏图标冲突）
// ============================================================
namespace SideBarIcons {

inline QIcon makeIcon(std::function<void(QPainter &, int)> drawFunc) {
    QIcon icon;
    for (int sz : {22, 44, 64}) {
        QPixmap pix(sz, sz);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        drawFunc(p, sz);
        p.end();
        icon.addPixmap(pix);
    }
    return icon;
}

// 文件浏览器图标：两个重叠的文档
inline QIcon fileBrowser() {
    return makeIcon([](QPainter &p, int sz) {
        qreal s = sz / 22.0;
        QPen pen(QColor("#5F6368"), 1.4 * s);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        // 后面的文档
        p.drawRoundedRect(QRectF(3*s, 1*s, 11*s, 14*s), 1.5*s, 1.5*s);
        // 前面的文档（偏移）
        p.setPen(QPen(QColor("#5F6368"), 1.4 * s));
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawRoundedRect(QRectF(7*s, 5*s, 11*s, 14*s), 1.5*s, 1.5*s);
        // 前面文档的横线
        p.setPen(QPen(QColor("#9AA0A6"), 1.0 * s));
        p.drawLine(QPointF(9.5*s, 9*s), QPointF(15.5*s, 9*s));
        p.drawLine(QPointF(9.5*s, 12*s), QPointF(15.5*s, 12*s));
        p.drawLine(QPointF(9.5*s, 15*s), QPointF(13.5*s, 15*s));
    });
}

// 分析结果图标：带勾的列表
inline QIcon analysisList() {
    return makeIcon([](QPainter &p, int sz) {
        qreal s = sz / 22.0;
        QPen pen(QColor("#5F6368"), 1.4 * s);
        p.setPen(pen);
        // 列表横线
        for (double y : {5.0, 9.5, 14.0, 18.5}) {
            p.drawLine(QPointF(8*s, y*s), QPointF(19*s, y*s));
        }
        // 圆点
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1A73E8"));
        for (double y : {5.0, 9.5, 14.0, 18.5}) {
            p.drawEllipse(QPointF(4.5*s, y*s), 1.5*s, 1.5*s);
        }
    });
}

// 状态面板图标：仪表盘/柱状图
inline QIcon statusChart() {
    return makeIcon([](QPainter &p, int sz) {
        qreal s = sz / 22.0;
        p.setPen(Qt::NoPen);
        // 三根柱子，不同高度
        p.setBrush(QColor("#5F6368"));
        p.drawRoundedRect(QRectF(3*s, 12*s, 4*s, 8*s), 1*s, 1*s);
        p.setBrush(QColor("#1A73E8"));
        p.drawRoundedRect(QRectF(9*s, 6*s, 4*s, 14*s), 1*s, 1*s);
        p.setBrush(QColor("#5F6368"));
        p.drawRoundedRect(QRectF(15*s, 2*s, 4*s, 18*s), 1*s, 1*s);
    });
}

// 动态图表图标：折线图
inline QIcon dynamicChart() {
    return makeIcon([](QPainter &p, int sz) {
        qreal s = sz / 22.0;
        // 坐标轴
        QPen axisPen(QColor("#9AA0A6"), 1.2 * s);
        p.setPen(axisPen);
        p.drawLine(QPointF(3*s, 3*s), QPointF(3*s, 19*s));   // Y 轴
        p.drawLine(QPointF(3*s, 19*s), QPointF(19*s, 19*s));  // X 轴
        // 折线
        QPen linePen(QColor("#1A73E8"), 1.6 * s);
        linePen.setCapStyle(Qt::RoundCap);
        linePen.setJoinStyle(Qt::RoundJoin);
        p.setPen(linePen);
        QPointF pts[] = {
            QPointF(5*s, 15*s), QPointF(8*s, 9*s),
            QPointF(11*s, 13*s), QPointF(14*s, 6*s), QPointF(17*s, 10*s)
        };
        p.drawPolyline(pts, 5);
        // 数据点
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1A73E8"));
        for (int i = 0; i < 5; ++i)
            p.drawEllipse(pts[i], 1.5*s, 1.5*s);
    });
}

} // namespace SideBarIcons

// ============================================================
// ActivityBarButton — 单个侧边栏图标按钮（VS Code 风格）
// ============================================================
class ActivityBarButton : public QToolButton {
    Q_OBJECT
public:
    explicit ActivityBarButton(const QIcon &icon,
                               const QString &tooltip,
                               QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setIcon(icon);
        setToolTip(tooltip);
        setCheckable(true);
        setAutoExclusive(false);   // 手动管理互斥
        setIconSize(QSize(SideBarPlatform::kIconSize, SideBarPlatform::kIconSize));
        setFixedSize(SideBarPlatform::kButtonSize, SideBarPlatform::kButtonSize);
        setCursor(Qt::PointingHandCursor);
        updateStyle(false);
    }

    void setActive(bool active) {
        setChecked(active);
        updateStyle(active);
    }

private:
    void updateStyle(bool active) {
        if (active) {
            setStyleSheet(
                "QToolButton {"
                "  background-color: #E8F0FE;"
                "  border: none;"
                "  border-left: 2px solid #1A73E8;"
                "  border-radius: 0px;"
                "  padding: 6px;"
                "}"
                "QToolButton:hover {"
                "  background-color: #D2E3FC;"
                "}"
            );
        } else {
            setStyleSheet(
                "QToolButton {"
                "  background-color: transparent;"
                "  border: none;"
                "  border-left: 2px solid transparent;"
                "  border-radius: 0px;"
                "  padding: 6px;"
                "}"
                "QToolButton:hover {"
                "  background-color: #F0F0F0;"
                "}"
            );
        }
    }
};

// ============================================================
// ActivityBar — 左侧窄竖条，包含图标按钮
// ============================================================
class ActivityBar : public QWidget {
    Q_OBJECT
public:
    explicit ActivityBar(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(SideBarPlatform::kActivityBarWidth);
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(2, 6, 2, 6);
        m_layout->setSpacing(2);
        m_layout->addStretch();

        setStyleSheet(
            "ActivityBar {"
            "  background-color: #F3F3F3;"
            "  border-right: 1px solid #E0E0E0;"
            "}"
        );
    }

    // 添加按钮（插入到 stretch 之前）
    int addButton(const QIcon &icon, const QString &tooltip) {
        int index = m_buttons.size();
        auto *btn = new ActivityBarButton(icon, tooltip, this);
        m_buttons.append(btn);
        // Insert before the stretch item (last item)
        m_layout->insertWidget(m_layout->count() - 1, btn, 0, Qt::AlignHCenter);

        connect(btn, &QToolButton::clicked, this, [this, index]() {
            emit buttonClicked(index);
        });
        return index;
    }

    // 标记某个按钮为"外部"按钮（不参与面板单选切换）
    void setButtonExternal(int index, bool external) {
        if (external) m_externalIndices.insert(index);
        else m_externalIndices.remove(index);
    }

    bool isExternal(int index) const {
        return m_externalIndices.contains(index);
    }

    // 设置面板按钮的激活状态（只影响非外部按钮）
    void setActiveButton(int index) {
        for (int i = 0; i < m_buttons.size(); ++i) {
            if (!m_externalIndices.contains(i)) {
                m_buttons[i]->setActive(i == index);
            }
        }
    }

    // 清除所有面板按钮的激活状态（只影响非外部按钮）
    void clearActive() {
        for (int i = 0; i < m_buttons.size(); ++i) {
            if (!m_externalIndices.contains(i)) {
                m_buttons[i]->setActive(false);
            }
        }
    }

    // 独立设置外部按钮的激活状态
    void setExternalActive(int index, bool active) {
        if (index >= 0 && index < m_buttons.size()) {
            m_buttons[index]->setActive(active);
        }
    }

signals:
    void buttonClicked(int index);

private:
    QVBoxLayout *m_layout = nullptr;
    QList<ActivityBarButton *> m_buttons;
    QSet<int> m_externalIndices;
};

// ============================================================
// SidePanelResizeHandle — 面板右侧拖拽手柄
// ============================================================
class SidePanelResizeHandle : public QWidget {
    Q_OBJECT
public:
    explicit SidePanelResizeHandle(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedWidth(4);
        setCursor(Qt::SplitHCursor);
    }

signals:
    void resized(int delta);

protected:
    void mousePressEvent(QMouseEvent *e) override {
        m_dragStartX = e->globalX();
        m_dragging = true;
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging) {
            int delta = e->globalX() - m_dragStartX;
            m_dragStartX = e->globalX();
            emit resized(delta);
            e->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        e->accept();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#E0E0E0"));
    }

    void enterEvent(QEvent *) override {
        QPalette pal = palette();
        // 鼠标悬浮时变亮
        update();
    }

private:
    int m_dragStartX = 0;
    bool m_dragging = false;
};

// ============================================================
// SidePanel — 可展开/折叠的内容面板
// ============================================================
class SidePanel : public QWidget {
    Q_OBJECT
public:
    explicit SidePanel(QWidget *parent = nullptr) : QWidget(parent) {
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);

        // 标题栏
        m_titleLabel = new QLabel(this);
        m_titleLabel->setFixedHeight(32);
        m_titleLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #F3F3F3;"
            "  border-bottom: 1px solid #E0E0E0;"
            "  padding-left: 10px;"
#if defined(Q_OS_WIN)
            "  font-size: 11px;"    // Windows Segoe UI 在 12px 下偏大
#else
            "  font-size: 12px;"
#endif
            "  font-weight: bold;"
            "  color: #333333;"
            "}"
        );

        m_stack = new QStackedWidget(this);
        m_layout->addWidget(m_titleLabel);
        m_layout->addWidget(m_stack, 1);

        setMinimumWidth(SideBarPlatform::kPanelMinWidth);
        setStyleSheet(
            "SidePanel {"
            "  border-right: 1px solid #E0E0E0;"
            "}"
        );
    }

    int addPage(QWidget *page, const QString &title) {
        int index = m_stack->addWidget(page);
        m_titles.append(title);
        if (index == 0) {
            m_titleLabel->setText(title);
        }
        return index;
    }

    void setCurrentPage(int index) {
        if (index >= 0 && index < m_stack->count()) {
            m_stack->setCurrentIndex(index);
            if (index < m_titles.size()) {
                m_titleLabel->setText(m_titles[index]);
            }
        }
    }

    int currentPage() const {
        return m_stack->currentIndex();
    }

    QStackedWidget *stackedWidget() const {
        return m_stack;
    }

private:
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_titleLabel = nullptr;
    QStackedWidget *m_stack = nullptr;
    QStringList m_titles;
};

// ============================================================
// VSCodeSideBar — 组合 ActivityBar + SidePanel + ResizeHandle
// ============================================================
class VSCodeSideBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)
public:
    explicit VSCodeSideBar(QWidget *parent = nullptr) : QWidget(parent) {
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_activityBar = new ActivityBar(this);
        m_sidePanel = new SidePanel(this);
        m_resizeHandle = new SidePanelResizeHandle(this);

        layout->addWidget(m_activityBar);
        layout->addWidget(m_sidePanel);
        layout->addWidget(m_resizeHandle);

        m_sidePanel->setVisible(false);
        m_resizeHandle->setVisible(false);
        m_currentPanelIndex = -1;
        m_panelWidth = SideBarPlatform::kPanelDefaultWidth;

        connect(m_activityBar, &ActivityBar::buttonClicked, this, &VSCodeSideBar::onButtonClicked);

        // 拖拽调整面板宽度
        connect(m_resizeHandle, &SidePanelResizeHandle::resized, this, [this](int delta) {
            int newWidth = m_sidePanel->width() + delta;
            newWidth = qBound(150, newWidth, 800);
            m_panelWidth = newWidth;
            m_sidePanel->setFixedWidth(newWidth);
        });
    }

    // 添加一个面板页面（返回按钮索引）
    int addPanel(const QIcon &icon, const QString &tooltip, QWidget *content, const QString &title) {
        int btnIdx = m_activityBar->addButton(icon, tooltip);
        int pageIdx = m_sidePanel->addPage(content, title);
        Q_ASSERT(btnIdx == pageIdx); // 保持一致
        return pageIdx;
    }

    // 添加一个外部切换按钮（不关联 SidePanel 页面，点击时发送 externalToggleClicked 信号）
    int addExternalToggle(const QIcon &icon, const QString &tooltip) {
        int btnIdx = m_activityBar->addButton(icon, tooltip);
        m_activityBar->setButtonExternal(btnIdx, true);
        m_externalIndices.insert(btnIdx);
        return btnIdx;
    }

    // 设置外部切换按钮的激活状态
    void setExternalToggleActive(int btnIndex, bool active) {
        m_activityBar->setExternalActive(btnIndex, active);
    }

    // 显示指定面板
    void showPanel(int index) {
        m_currentPanelIndex = index;
        m_sidePanel->setCurrentPage(index);
        m_sidePanel->setVisible(true);
        m_sidePanel->setFixedWidth(m_panelWidth);
        m_resizeHandle->setVisible(true);
        m_activityBar->setActiveButton(index);
        emit panelVisibilityChanged(index, true);
    }

    // 隐藏面板
    void hidePanel() {
        int prev = m_currentPanelIndex;
        m_currentPanelIndex = -1;
        m_sidePanel->setVisible(false);
        m_resizeHandle->setVisible(false);
        m_activityBar->clearActive();
        emit panelVisibilityChanged(prev, false);
    }

    // 切换面板
    void togglePanel(int index) {
        if (m_currentPanelIndex == index) {
            hidePanel();
        } else {
            showPanel(index);
        }
    }

    bool isPanelVisible() const {
        return m_sidePanel->isVisible();
    }

    int currentPanelIndex() const {
        return m_currentPanelIndex;
    }

    ActivityBar *activityBar() const { return m_activityBar; }
    SidePanel *sidePanel() const { return m_sidePanel; }

    int panelWidth() const { return m_panelWidth; }
    void setPanelWidth(int w) {
        m_panelWidth = w;
        if (m_sidePanel->isVisible()) {
            m_sidePanel->setFixedWidth(w);
        }
    }

signals:
    void panelVisibilityChanged(int index, bool visible);
    void externalToggleClicked(int buttonIndex);

private slots:
    void onButtonClicked(int index) {
        if (m_externalIndices.contains(index)) {
            emit externalToggleClicked(index);
        } else {
            togglePanel(index);
        }
    }

private:
    ActivityBar *m_activityBar = nullptr;
    SidePanel *m_sidePanel = nullptr;
    SidePanelResizeHandle *m_resizeHandle = nullptr;
    int m_currentPanelIndex = -1;
    int m_panelWidth = 280;
    QSet<int> m_externalIndices;
};

#endif // VSCODESIDEBAR_H
