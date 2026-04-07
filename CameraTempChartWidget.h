#ifndef CAMERATEMPCHARTWIDGET_H
#define CAMERATEMPCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"
#include "ChartViewportMixin.h"

struct CameraTempSample {
    QDateTime timestamp;
    int temperature = 0; // 摄像头温度 (°C)
};

class CameraTempChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit CameraTempChartWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true);
        setMinimumSize(400, 180);
    }

    void setData(const QVector<CameraTempSample> &data) {
        samples.clear();
        samples.reserve(data.size());
        for (const auto &s : data) {
            if (s.temperature == -128) continue; // 过滤异常值
            samples.push_back(s);
        }
        // Compute tMin/tMax and reset viewport
        QDateTime mn, mx;
        for (const auto &s : samples) {
            if (!s.timestamp.isValid()) continue;
            QDateTime timestamp = s.timestamp;
            if (!mn.isValid() || timestamp < mn) mn = timestamp;
            if (!mx.isValid() || timestamp > mx) mx = timestamp;
        }
        if (mn.isValid()) {
            m_vp.setRange(mn, mx);
            if (samples.size() >= 2) {
                qint64 span = mn.msecsTo(mx);
                qint64 interval = span / (samples.size() - 1);
                m_vp.minWindowMs = qMax((qint64)500, interval * 3);
            }
        } else {
            m_vp.clear();
        }
        update();
    }

    void clear() {
        samples.clear();
        m_vp.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        if (samples.isEmpty()) return;

        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft   = layout.marginLeft;
        int marginRight  = layout.marginRight;
        int marginTop    = layout.marginTop;
        int marginBottom = layout.marginBottom;
        int w            = width()  - marginLeft - marginRight;
        int h            = height() - marginTop  - marginBottom;

        const int tMin = 0;
        const int tMax = 100;

        // ---- Slice to visible window ----
        QVector<CameraTempSample> vis = visibleSlice();
        int n = vis.size();
        if (n < 1) return; // nothing visible in current zoom window

        QVector<QDateTime> timestamps;
        timestamps.reserve(n);
        for (const auto &s : vis) timestamps.append(s.timestamp);

        QVector<double> vals;
        vals.reserve(n);
        for (const auto &s : vis) vals.append(s.temperature);

        // ---- Viewport time boundaries ----
        QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : vis.first().timestamp;
        QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : vis.last().timestamp;

        // Draw frame, axes, threshold line
        ChartWidgetBase::drawChartFrame(p, marginLeft, marginTop, w, h, "相机温度");
        ChartWidgetBase::drawYAxisTopDown(p, marginLeft, marginTop, h,
                                          marginLeft - 45, tMin, tMax, 10, "°C", 0);
        ChartWidgetBase::drawXAxisTime(p, marginLeft, marginTop, w, h, timestamps, vpStart, vpEnd);

        {
            int y70 = marginTop + (tMax - 70) * h / (tMax - tMin);
            QPen dashPen(ChartStyleManager::getColorTheme().danger, 1, Qt::DashLine);
            p.setPen(dashPen);
            p.drawLine(marginLeft, y70, marginLeft + w, y70);
        }

        ChartWidgetBase::drawPolyline(p, marginLeft, marginTop, w, h,
                                      vals, timestamps, tMin, tMax, vpStart, vpEnd,
                                      QPen(ChartStyleManager::getColorTheme().accent, 2));

        // ---- Hover crosshair ----
        if (hoverActive && n >= 1) {
            // 竖线直接跟鼠标 x 走，保证丝滑
            int hx = qBound(marginLeft, (int)hoverPos.x(), marginLeft + w);

            // 找时间上最近的数据点（用于数值气泡/横线吸附），跳过 guard 点
            qint64 vpSpan = vpStart.msecsTo(vpEnd);
            int idx = 0;
            if (vpSpan > 0) {
                qint64 targetMs = m_vp.vStart + (qint64)((hx - marginLeft) * vpSpan / double(w));
                int best = -1; qint64 bestDist = LLONG_MAX;
                for (int i = 0; i < n; ++i) {
                    qint64 ms = m_vp.tMin.msecsTo(vis[i].timestamp);
                    if (ms < m_vp.vStart || ms > m_vp.vEnd) continue;
                    qint64 dist = qAbs(ms - targetMs);
                    if (dist < bestDist) { bestDist = dist; best = i; }
                }
                if (best < 0) { // fallback
                    bestDist = LLONG_MAX;
                    for (int i = 0; i < n; ++i) {
                        qint64 ms = m_vp.tMin.msecsTo(vis[i].timestamp);
                        qint64 dist = qAbs(ms - targetMs);
                        if (dist < bestDist) { bestDist = dist; best = i; }
                    }
                }
                idx = (best >= 0) ? best : 0;
            } else {
                idx = qBound(0, int((hx - marginLeft) * (n - 1) / double(w)), n - 1);
            }
            int y = marginTop + int((tMax - vis[idx].temperature) * h / double(tMax - tMin));
            // 检查是否需要显示日期（如果数据跨越了不同日期）
            qint64 spanMs = m_vp.totalSpanMs();
            QString timeFormat = "HH:mm:ss";
            if (spanMs > 24 * 60 * 60 * 1000) { // 如果时间跨度超过24小时
                timeFormat = "MM-dd HH:mm";
            }
            ChartWidgetBase::drawCrosshair(p, marginLeft, marginTop, w, h,
                                           hx, y, vis[idx].timestamp.toString(timeFormat));
            ChartWidgetBase::drawTooltipBubble(p,
                                               QString("%1°C").arg(vis[idx].temperature),
                                               QPointF(hx, y), width(), height());
        }

        // ---- Zoom hint ----
        if (m_vp.isZoomed()) {
            auto ft = ChartStyleManager::getFontTheme();
            QFont smallF = ft.axis;
            smallF.setPointSize(smallF.pointSize() - 1);
            p.setFont(smallF);
            p.setPen(QColor(150, 150, 150));
            p.drawText(marginLeft + w - 80, marginTop + h - 2, "滚轮缩放/拖动");
        }
    }

    // ---- Zoom ----
    void wheelEvent(QWheelEvent *e) override {
        auto layout = ChartStyleManager::getLayoutTheme();
        int mL = layout.marginLeft;
        int cW = width() - mL - layout.marginRight;
        m_vp.handleWheel(e->angleDelta().y(), e->position().x(), mL, cW);
        hoverActive = false;
        update();
        e->accept();
    }

    // ---- Pan ----
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_vp.beginDrag(e->pos().x());
            setCursor(Qt::SizeHorCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (samples.isEmpty()) { hoverActive = false; return; }
        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft  = layout.marginLeft;
        int marginRight = layout.marginRight;
        int cW = width() - marginLeft - marginRight;

        if (m_vp.dragging && (event->buttons() & Qt::LeftButton)) {
            m_vp.doDrag(event->pos().x(), cW);
            hoverActive = false;
            update();
            return;
        }

        hoverPos    = event->pos();
        hoverActive = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_vp.endDrag();
            setCursor(Qt::ArrowCursor);
        }
    }

    // ---- Reset ----
    void mouseDoubleClickEvent(QMouseEvent *) override {
        m_vp.reset();
        hoverActive = false;
        update();
    }

    void leaveEvent(QEvent *) override {
        hoverActive = false;
        update();
    }

private:
    QVector<CameraTempSample> samples;
    QPointF hoverPos;
    bool hoverActive = false;
    ChartViewport m_vp;

    QVector<CameraTempSample> visibleSlice() const {
        if (!m_vp.hasData()) return samples;
        const int total = samples.size();
        if (total == 0) return samples;
        qint64 vS = m_vp.vStart, vE = m_vp.vEnd;

        int first = total, last = -1;
        for (int i = 0; i < total; ++i) {
            qint64 ms = m_vp.tMin.msecsTo(samples[i].timestamp);
            if (ms >= vS && first == total) first = i;
            if (ms <= vE) last = i;
        }
        int lo = qMax(0, first - 1);
        int hi = qMin(total - 1, last + 1);
        if (lo > hi) return samples;

        QVector<CameraTempSample> vis;
        vis.reserve(hi - lo + 1);
        for (int i = lo; i <= hi; ++i) vis.append(samples[i]);
        return vis;
    }
};

#endif // CAMERATEMPCHARTWIDGET_H
