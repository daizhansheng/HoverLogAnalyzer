#ifndef SOCTEMPCHARTWIDGET_H
#define SOCTEMPCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"
#include "ChartViewportMixin.h"

// ================= 数据结构 =================
struct SocTempInfo {
    QDateTime timestamp;      // 时间戳 (含日期+时间+毫秒)
    int maxTemp = 0;          // SoC 最大温度
    QVector<int> coreTemps;   // 8 核心温度
};

// ================= 绘图控件 =================
class SocTempChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit SocTempChartWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumSize(400, 300);
    }

    void addData(SocTempInfo &info) {
        socData.append(info);
        if (!info.timestamp.isValid()) { update(); return; }

        // Track accumulated tMin / tMax
        if (!m_tMinAcc.isValid() || info.timestamp < m_tMinAcc) m_tMinAcc = info.timestamp;
        if (!m_tMaxAcc.isValid() || info.timestamp > m_tMaxAcc) m_tMaxAcc = info.timestamp;

        if (!m_vp.hasData()) {
            // First load: set the viewport range from scratch.
            if (m_tMinAcc.isValid()) m_vp.setRange(m_tMinAcc, m_tMaxAcc);
        } else if (!m_vp.isZoomed()) {
            // Not zoomed: expand tMax as new data arrives so full-view stays current.
            m_vp.tMax = m_tMaxAcc;
            m_vp.vEnd = m_vp.totalSpanMs();
            // Keep minWindowMs in sync: 3× average interval
            int n = socData.size();
            if (n >= 2) {
                qint64 span = m_vp.totalSpanMs();
                qint64 interval = span / (n - 1);
                m_vp.minWindowMs = qMax((qint64)500, interval * 3);
            }
        }
        // (If zoomed, leave vStart/vEnd alone — don't snap back.)
        update();
    }

    void addData(QVector<SocTempInfo> &infos) {
        for (auto &info : infos) addData(info);
    }

    void clear() {
        socData.clear();
        m_vp.clear();
        m_tMinAcc = QDateTime();
        m_tMaxAcc = QDateTime();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        if (socData.isEmpty()) return;

        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft   = layout.marginLeft;
        int marginRight  = layout.marginRight;
        int marginTop    = layout.marginTop;
        int marginBottom = layout.marginBottom;
        int chartWidth   = width()  - marginLeft - marginRight;
        int chartHeight  = height() - marginTop  - marginBottom;

        // ---- Slice to visible window ----
        QVector<SocTempInfo> vis = visibleSlice();
        int n = vis.size();
        if (n < 1) return; // nothing visible in current zoom window

        // ---- Viewport time boundaries ----
        QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : vis.first().timestamp;
        QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : vis.last().timestamp;

        // Compute dynamic max temperature, round up to nearest 10 for clean axis labels
        int maxY = 0;
        for (const auto &d : vis) if (d.maxTemp > maxY) maxY = d.maxTemp;
        maxY = qMax(maxY, 130);
        maxY = ((maxY + 9) / 10) * 10; // ceil to multiple of 10

        // Build vectors
        QVector<QDateTime> timestamps;
        timestamps.reserve(n);
        for (const auto &d : vis) timestamps.append(d.timestamp);

        QVector<double> vals;
        vals.reserve(n);
        for (const auto &d : vis) vals.append(d.maxTemp);

        // Draw
        ChartWidgetBase::drawChartFrame(p, marginLeft, marginTop, chartWidth, chartHeight, "CPU温度");
        int ySteps = maxY / 10;
        ChartWidgetBase::drawYAxisTopDown(p, marginLeft, marginTop, chartHeight,
                                          marginLeft - 45, 0, maxY, ySteps, "°C", 0);
        ChartWidgetBase::drawXAxisTime(p, marginLeft, marginTop, chartWidth, chartHeight, timestamps, vpStart, vpEnd);

        QPen dangerDash(ChartStyleManager::getColorTheme().danger, 2, Qt::DashLine);
        ChartWidgetBase::drawPolyline(p, marginLeft, marginTop, chartWidth, chartHeight,
                                      vals, timestamps, 0, maxY, vpStart, vpEnd, dangerDash);

        // ---- Hover crosshair ----
        if (hoverActive && n >= 1) {
            qint64 vpSpan = vpStart.msecsTo(vpEnd);
            // 竖线直接跟鼠标 x 走，保证丝滑
            int hx = qBound(marginLeft, (int)hoverPos.x(), marginLeft + chartWidth);

            // 找时间上最近的数据点（用于数值气泡/信息面板），跳过 guard 点
            int idx = 0;
            if (vpSpan > 0) {
                qint64 targetMs = m_vp.vStart
                                  + (qint64)((hx - marginLeft) * vpSpan / double(chartWidth));
                int best = -1; qint64 bestDist = LLONG_MAX;
                for (int i = 0; i < n; ++i) {
                    qint64 ms = m_vp.tMin.msecsTo(vis[i].timestamp);
                    if (ms < m_vp.vStart || ms > m_vp.vEnd) continue; // 跳过 guard 点
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
                idx = qBound(0, int((hx - marginLeft) * (n - 1) / double(chartWidth)), n - 1);
            }
            const SocTempInfo &d = vis[idx];
            int py = marginTop + chartHeight - int(d.maxTemp * chartHeight / double(maxY));

            // 检查是否需要显示日期（如果数据跨越了不同日期）
            qint64 spanMs = m_vp.totalSpanMs();
            QString timeFormat = "HH:mm:ss";
            if (spanMs > 24 * 60 * 60 * 1000) { // 如果时间跨度超过24小时
                timeFormat = "MM-dd HH:mm";
            }
            ChartWidgetBase::drawCrosshair(p, marginLeft, marginTop, chartWidth, chartHeight,
                                           hx, py, d.timestamp.toString(timeFormat));
            ChartWidgetBase::drawTooltipBubble(p,
                                               QString("%1°C").arg(d.maxTemp),
                                               QPointF(hx, py), width(), height());

            QStringList lines;
            for (int i = 0; i < d.coreTemps.size(); ++i)
                lines << QString("CPU%1: %2°C").arg(i).arg(d.coreTemps[i]);
            ChartWidgetBase::drawInfoPanel(p, lines, width(), marginTop + 20,
                                           ChartStyleManager::getColorTheme().danger,
                                           height());
        }

        // ---- Zoom hint ----
        if (m_vp.isZoomed()) {
            auto ft = ChartStyleManager::getFontTheme();
            QFont smallF = ft.axis;
            smallF.setPointSize(smallF.pointSize() - 1);
            p.setFont(smallF);
            p.setPen(QColor(150, 150, 150));
            p.drawText(marginLeft + chartWidth - 80, marginTop + chartHeight - 2, "滚轮缩放/拖动");
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
        auto layout = ChartStyleManager::getLayoutTheme();
        int mL = layout.marginLeft;
        int cW = width() - mL - layout.marginRight;
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

    void leaveEvent(QEvent *event) override {
        Q_UNUSED(event);
        hoverActive = false;
        update();
    }

private:
    QVector<SocTempInfo> socData;
    QPointF hoverPos;
    bool hoverActive = false;
    ChartViewport m_vp;
    QDateTime m_tMinAcc, m_tMaxAcc;  // accumulator (unused after range set)

    QVector<SocTempInfo> visibleSlice() const {
        if (!m_vp.hasData()) return socData;
        const int total = socData.size();
        if (total == 0) return socData;
        qint64 vS = m_vp.vStart, vE = m_vp.vEnd;

        int first = total, last = -1;
        for (int i = 0; i < total; ++i) {
            qint64 ms = m_vp.tMin.msecsTo(socData[i].timestamp);
            if (ms >= vS && first == total) first = i;
            if (ms <= vE) last = i;
        }
        int lo = qMax(0, first - 1);
        int hi = qMin(total - 1, last + 1);
        if (lo > hi) return socData;

        QVector<SocTempInfo> vis;
        vis.reserve(hi - lo + 1);
        for (int i = lo; i <= hi; ++i) vis.append(socData[i]);
        return vis;
    }
};

#endif // SOCTEMPCHARTWIDGET_H
