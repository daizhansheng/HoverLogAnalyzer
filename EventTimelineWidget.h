#ifndef EVENTTIMELINEWIDGET_H
#define EVENTTIMELINEWIDGET_H

#include <QWidget>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QCursor>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"

// ============================================================
// EventTimelineWidget
//
// 水平时间轴展示两类事件：
//   - Camera 状态段（彩色矩形块）
//   - 心跳丢失事件（红色倒三角标记）
//
// 交互：
//   - 鼠标悬停：显示 tooltip 气泡 + 垂直准线
//   - 左键点击：emit jumpToLine(lineNumber)
//   - 滚轮：以鼠标位置为中心缩放（放大/缩小时间范围）
//   - 左键拖动：左右平移视图
// ============================================================

struct TimelineEvent {
    int       lineNumber;
    QDateTime timestamp;
    QString   display;
    QString   category;   // "camera" | "heartbeat"
    QString   note;       // camera 状态名
};

class EventTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit EventTimelineWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumHeight(80);
        setMinimumWidth(200);
        setCursor(Qt::ArrowCursor);
    }

    void setEvents(const QList<TimelineEvent> &events)
    {
        m_events = events;
        m_hoverIdx = -1;
        // 计算全局 tMin/tMax
        m_tMin = QDateTime(); m_tMax = QDateTime();
        for (const auto &ev : m_events) {
            if (!ev.timestamp.isValid()) continue;
            if (!m_tMin.isValid() || ev.timestamp < m_tMin) m_tMin = ev.timestamp;
            if (!m_tMax.isValid() || ev.timestamp > m_tMax) m_tMax = ev.timestamp;
        }
        // 重置视图到全量
        m_viewStart = 0;
        m_viewEnd   = totalSpanMs();
        if (m_viewEnd == 0) m_viewEnd = 1000;
        m_dragging  = false;
        update();
    }

    void clear()
    {
        m_events.clear();
        m_hoverIdx  = -1;
        m_tMin      = QDateTime();
        m_tMax      = QDateTime();
        m_viewStart = 0;
        m_viewEnd   = 0;
        m_dragging  = false;
        update();
    }

signals:
    void jumpToLine(int lineNumber);

protected:
    // ---- paint ----
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int W = width(), H = height();
        const int mL = 8, mR = 8, mT = 22, mB = 26;
        const int cW = W - mL - mR;
        const int cH = H - mT - mB;
        if (cW <= 0 || cH <= 0) return;

        p.fillRect(rect(), ChartStyleManager::getChartBackground());
        p.setPen(ChartStyleManager::getAxisPen());
        p.drawLine(mL, mT + cH, mL + cW, mT + cH);

        // 标题
        {
            auto ft = ChartStyleManager::getFontTheme();
            p.setFont(ft.title);
            p.setPen(ChartStyleManager::getColorTheme().primary);
            p.drawText(mL, mT - 5, "事件时间轴");
        }

        if (m_events.isEmpty()) {
            p.setPen(ChartStyleManager::getColorTheme().text);
            p.setFont(ChartStyleManager::getFontTheme().axis);
            p.drawText(QRect(mL, mT, cW, cH), Qt::AlignCenter, "暂无事件数据");
            return;
        }

        // ---- 全局时间范围 ----
        qint64 span = totalSpanMs();
        // 视图窗口（clamp）
        qint64 vStart = m_viewStart;
        qint64 vEnd   = (m_viewEnd > vStart) ? m_viewEnd : (vStart + qMax(span, (qint64)1000));

        // 把 ms 偏移 → widget x 坐标
        auto msToX = [&](qint64 ms) -> int {
            if (vEnd == vStart) return mL + cW / 2;
            return mL + int((ms - vStart) * cW / double(vEnd - vStart));
        };

        auto xOfEvent = [&](int i) -> int {
            const auto &ev = m_events[i];
            if (!ev.timestamp.isValid() || !m_tMin.isValid()) {
                // 按索引均匀
                int n = m_events.size();
                return mL + (n > 1 ? int(i * cW / double(n - 1)) : cW / 2);
            }
            return msToX(m_tMin.msecsTo(ev.timestamp));
        };

        // ---- camera 状态色块 ----
        const int blockTop = mT + 2;
        const int blockH   = cH * 6 / 10;
        for (int i = 0; i < m_events.size(); ++i) {
            if (m_events[i].category != "camera") continue;
            int x1 = xOfEvent(i);
            int x2 = mL + cW;
            for (int j = i + 1; j < m_events.size(); ++j) {
                if (m_events[j].category == "camera") {
                    x2 = xOfEvent(j);
                    break;
                }
            }
            // 裁剪到可视区
            int rx1 = qMax(x1, mL);
            int rx2 = qMin(x2, mL + cW);
            if (rx2 <= rx1) continue;
            QColor blk = cameraStateColor(m_events[i].note);
            blk.setAlpha(190);
            p.fillRect(rx1, blockTop, rx2 - rx1, blockH, blk);
            // 左边线（仅在可见范围内）
            if (x1 >= mL && x1 <= mL + cW) {
                p.setPen(QPen(blk.darker(150), 1));
                p.drawLine(x1, blockTop, x1, blockTop + blockH);
                // 状态标签（块宽度足够时）
                if (rx2 - rx1 > 30) {
                    p.setFont(ChartStyleManager::getFontTheme().axis);
                    p.setPen(Qt::black);
                    p.drawText(QRect(rx1 + 3, blockTop + 1, rx2 - rx1 - 4, blockH - 2),
                               Qt::AlignLeft | Qt::AlignVCenter,
                               m_events[i].note);
                }
            }
        }

        // ---- 心跳丢失标记（倒三角） ----
        const int triCY = mT + blockH + 6;
        for (int i = 0; i < m_events.size(); ++i) {
            if (m_events[i].category != "heartbeat") continue;
            int x = xOfEvent(i);
            if (x < mL - 6 || x > mL + cW + 6) continue;
            QPolygon tri;
            tri << QPoint(x,     triCY + 8)
                << QPoint(x - 5, triCY)
                << QPoint(x + 5, triCY);
            p.setBrush(QColor(220, 50, 50, 220));
            p.setPen(Qt::NoPen);
            p.drawPolygon(tri);
        }

        // ---- X 轴时间刻度 ----
        {
            auto ft = ChartStyleManager::getFontTheme();
            p.setFont(ft.axis);
            p.setPen(ChartStyleManager::getAxisPen());
            QFontMetrics fm(p.font());
            int labelW = fm.horizontalAdvance("00:00:00") + 8;
            int maxLabels = qMax(2, cW / labelW);
            // 计算刻度间隔（ms）
            qint64 windowMs  = vEnd - vStart;
            qint64 niceStep  = niceTimeStepMs(windowMs, maxLabels);
            qint64 firstTick = (vStart / niceStep) * niceStep;
            if (firstTick < vStart) firstTick += niceStep;
            int lastLabelRight = mL - labelW; // 碰撞检测：上一个标签右边界
            for (qint64 t = firstTick; t <= vEnd; t += niceStep) {
                int x = msToX(t);
                if (x < mL || x > mL + cW) continue;
                int labelLeft = x - labelW / 2;
                p.drawLine(x, mT + cH, x, mT + cH + 4);
                if (labelLeft >= lastLabelRight + 4) {
                    QDateTime ts = m_tMin.isValid()
                                       ? m_tMin.addMSecs(t)
                                       : QDateTime();
                    QString label = ts.isValid() ? ts.toString("HH:mm:ss") : QString::number(t / 1000) + "s";
                    p.drawText(labelLeft, mT + cH + 18, label);
                    lastLabelRight = labelLeft + labelW;
                }
            }
        }

        // ---- 悬停准线 + tooltip ----
        if (m_hoverIdx >= 0 && m_hoverIdx < m_events.size()) {
            int hx = xOfEvent(m_hoverIdx);
            p.setPen(ChartStyleManager::getHoverPen());
            p.drawLine(hx, mT, hx, mT + cH);
            ChartWidgetBase::drawTooltipBubble(p, m_events[m_hoverIdx].display,
                                               QPointF(hx, mT + cH / 2), W, H);
        }

        // ---- 缩放提示（始终显示右下角） ----
        if (span > 0 && (m_viewEnd - m_viewStart) < span) {
            auto ft = ChartStyleManager::getFontTheme();
            QFont smallF = ft.axis;
            smallF.setPointSize(smallF.pointSize() - 1);
            p.setFont(smallF);
            p.setPen(QColor(150, 150, 150));
            p.drawText(mL + cW - 80, mT + cH - 2, "滚轮缩放/拖动");
        }
    }

    // ---- wheel → zoom ----
    void wheelEvent(QWheelEvent *e) override
    {
        qint64 span = totalSpanMs();
        if (span <= 0) { e->ignore(); return; }

        const int mL = 8, mR = 8;
        const int cW = width() - mL - mR;
        if (cW <= 0) { e->ignore(); return; }

        // 鼠标位置对应的时间偏移
        double frac = (e->position().x() - mL) / double(cW);
        frac = qBound(0.0, frac, 1.0);

        qint64 vStart = m_viewStart;
        qint64 vEnd   = m_viewEnd;
        qint64 window = vEnd - vStart;
        if (window <= 0) window = span;

        // 每次缩放 25%
        double factor = (e->angleDelta().y() > 0) ? 0.75 : 1.33;
        qint64 newWindow = qBound((qint64)500, (qint64)(window * factor), span);

        qint64 pivot = vStart + (qint64)(frac * window);
        qint64 newStart = pivot - (qint64)(frac * newWindow);
        qint64 newEnd   = newStart + newWindow;

        // clamp
        if (newStart < 0) { newEnd -= newStart; newStart = 0; }
        if (newEnd > span) { newStart -= (newEnd - span); newEnd = span; }
        newStart = qMax((qint64)0, newStart);
        newEnd   = qMin(span, newEnd);

        m_viewStart = newStart;
        m_viewEnd   = newEnd;
        m_hoverIdx  = hitTest(e->position().toPoint());
        update();
        e->accept();
    }

    // ---- drag → pan ----
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            // 先判断是否点击了事件（在 drag 之前）
            int idx = hitTest(e->pos());
            if (idx >= 0) {
                // 短按会触发 mouseRelease，在那里再 emit
                m_clickCandidate = idx;
            }
            m_dragging    = true;
            m_dragLastX   = e->pos().x();
            m_dragMoved   = false;
            setCursor(Qt::SizeHorCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            int dx = e->pos().x() - m_dragLastX;
            if (qAbs(dx) > 2) m_dragMoved = true;
            if (m_dragMoved) {
                panBy(-dx);
                m_dragLastX = e->pos().x();
                m_hoverIdx  = -1;
            }
        } else {
            m_hoverIdx = hitTest(e->pos());
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            setCursor(Qt::ArrowCursor);
            if (!m_dragMoved && m_clickCandidate >= 0) {
                emit jumpToLine(m_events[m_clickCandidate].lineNumber);
            }
            m_dragging       = false;
            m_dragMoved      = false;
            m_clickCandidate = -1;
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        // 双击重置视图
        m_viewStart = 0;
        m_viewEnd   = totalSpanMs();
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hoverIdx = -1;
        update();
    }

private:
    // ---- camera 状态 → 颜色 ----
    static QColor cameraStateColor(const QString &note)
    {
        if (note == "close")                                      return QColor(169, 169, 169);
        if (note == "init")                                       return QColor(211, 211, 211);
        if (note.contains("recording"))                           return QColor(220,  50,  50);
        if (note.contains("stream") && note.contains("preview")) return QColor(255, 165,   0);
        if (note.contains("stream"))                              return QColor( 80, 200,  80);
        if (note.contains("preview"))                             return QColor(230, 230,  50);
        if (note.contains("snapshot"))                            return QColor( 50, 220, 220);
        return QColor(200, 200, 200);
    }

    // ---- 全量时间跨度 (ms) ----
    qint64 totalSpanMs() const
    {
        if (m_events.isEmpty() || !m_tMin.isValid() || !m_tMax.isValid()) return 0;
        return m_tMin.msecsTo(m_tMax);
    }

    // ---- 平移视图 ----
    void panBy(int dxPx)
    {
        const int mL = 8, mR = 8;
        const int cW = qMax(1, width() - mL - mR);
        qint64 span   = totalSpanMs();
        if (span <= 0) return;
        qint64 window = m_viewEnd - m_viewStart;
        if (window <= 0) return;
        qint64 dMs = (qint64)(dxPx * window / double(cW));
        qint64 ns = qBound((qint64)0, m_viewStart + dMs, span - window);
        m_viewStart = ns;
        m_viewEnd   = ns + window;
    }

    // ---- 命中测试（像素吸附，仅在可视范围内） ----
    int hitTest(const QPoint &pos) const
    {
        const int mL = 8, mR = 8;
        const int cW = width() - mL - mR;
        if (m_events.isEmpty() || cW <= 0) return -1;

        qint64 vStart = m_viewStart;
        qint64 vEnd   = m_viewEnd;
        if (vEnd <= vStart) return -1;

        int best = -1, bestDist = 14;
        for (int i = 0; i < m_events.size(); ++i) {
            int x;
            if (!m_events[i].timestamp.isValid() || !m_tMin.isValid()) {
                int n = m_events.size();
                x = mL + (n > 1 ? int(i * cW / double(n - 1)) : cW / 2);
            } else {
                qint64 ms = m_tMin.msecsTo(m_events[i].timestamp);
                x = mL + int((ms - vStart) * cW / double(vEnd - vStart));
            }
            int d = qAbs(pos.x() - x);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        return best;
    }

    // ---- 计算"好看"的时间刻度间隔 (ms) ----
    static qint64 niceTimeStepMs(qint64 windowMs, int maxLabels)
    {
        if (maxLabels <= 0) maxLabels = 5;
        // 候选步长（ms）：100ms 200ms 500ms 1s 2s 5s 10s 30s 1m 2m 5m 10m 30m 1h
        static const qint64 candidates[] = {
            100, 200, 500, 1000, 2000, 5000, 10000, 30000,
            60000, 120000, 300000, 600000, 1800000, 3600000
        };
        qint64 minStep = windowMs / maxLabels;
        for (qint64 c : candidates)
            if (c >= minStep) return c;
        return candidates[sizeof(candidates)/sizeof(candidates[0]) - 1];
    }

private:
    QList<TimelineEvent> m_events;
    QDateTime            m_tMin, m_tMax;

    // 视图窗口（相对于 tMin 的 ms 偏移）
    qint64  m_viewStart = 0;
    qint64  m_viewEnd   = 0;

    // 悬停 / 拖动状态
    int     m_hoverIdx       = -1;
    bool    m_dragging       = false;
    int     m_dragLastX      = 0;
    bool    m_dragMoved      = false;
    int     m_clickCandidate = -1;
};

#endif // EVENTTIMELINEWIDGET_H
