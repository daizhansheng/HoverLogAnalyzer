#ifndef BATTERYCHARTWIDGET_H
#define BATTERYCHARTWIDGET_H
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <QString>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"
#include "ChartViewportMixin.h"

struct BatteryInfo {
    int soc = 0;                 // 电量 0-100
    int current = 0;             // 电流 0-1000
    int voltage = 0;             // 电压 0-1000
    double temp = 0.0;           // 温度 -20~80
    int is_abnormal = 0;         // 异常状态
    int is_charging = 0;         // 充电状态
    int heating = 0;             // 加热状态
    int can_heat = 0;            // 可加热
    QString battery_sn;          // 电池序列号
    int battery_cycles_count = 0;// 循环次数
    int battery_health = 0;      // 电池健康
};

struct BatteryTimeInfo {
    QDateTime timestamp;
    BatteryInfo info;
};

class BatteryChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BatteryChartWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true);
        setMinimumSize(400, 250);
    }

    void setData(const QVector<BatteryTimeInfo> &data) {
        batteryData = data;
        // Compute tMin/tMax and reset viewport
        QDateTime mn, mx;
        for (const auto &d : batteryData) {
            if (!d.timestamp.isValid()) continue;
            if (!mn.isValid() || d.timestamp < mn) mn = d.timestamp;
            if (!mx.isValid() || d.timestamp > mx) mx = d.timestamp;
        }
        if (mn.isValid()) {
            m_vp.setRange(mn, mx);
            // Set min zoom to 3× the typical data interval so at least 3 points are visible.
            // Use the median interval if ≥2 points, else fall back to 3000 ms.
            if (batteryData.size() >= 2) {
                qint64 span = mn.msecsTo(mx);
                qint64 interval = span / (batteryData.size() - 1);
                m_vp.minWindowMs = qMax((qint64)500, interval * 3);
            }
        } else {
            m_vp.clear();
        }
        update();
    }

    void clear() {
        batteryData.clear();
        m_vp.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), ChartStyleManager::getChartBackground());

        if (batteryData.isEmpty()) return;

        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft   = layout.marginLeft;
        int marginRight  = layout.marginRight;
        int marginTop    = layout.marginTop;
        int marginBottom = layout.marginBottom;
        int totalWidth   = width() - marginLeft - marginRight;
        int chartSpacing = layout.chartSpacing + 30;
        int chartHeight  = (height() - marginTop - marginBottom - chartSpacing) / 2;

        int chart1Top = marginTop;
        int chart2Top = chart1Top + chartHeight + chartSpacing;

        // ---- Slice data to visible viewport window ----
        QVector<BatteryTimeInfo> vis = visibleSlice();

        int n = vis.size();
        if (n < 1) return;

        // ---- Viewport time boundaries for correct axis/polyline mapping ----
        QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : vis.first().timestamp;
        QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : vis.last().timestamp;

        // ---- Build timestamp vector for sliced data ----
        QVector<QDateTime> timestamps;
        timestamps.reserve(n);
        for (const auto &d : vis) timestamps.append(d.timestamp);

        drawSocChart(painter, marginLeft, chart1Top, totalWidth, chartHeight, vis, timestamps, vpStart, vpEnd);
        drawTempChart(painter, marginLeft, chart2Top, totalWidth, chartHeight, vis, timestamps, vpStart, vpEnd);

        // ---- Hover crosshair ----
        if (hoverActive && n >= 1) {
            // 竖线直接跟鼠标 x 走，保证丝滑
            int hx = qBound(marginLeft, (int)hoverPos.x(), marginLeft + totalWidth);

            // 找时间上最近的数据点（用于数值气泡/横线吸附），跳过 guard 点
            qint64 vpSpan = vpStart.msecsTo(vpEnd);
            int idx = 0;
            if (vpSpan > 0) {
                qint64 targetMs = m_vp.vStart + (qint64)((hx - marginLeft) * vpSpan / double(totalWidth));
                int best = -1; qint64 bestDist = LLONG_MAX;
                for (int i = 0; i < n; ++i) {
                    qint64 ms = m_vp.tMin.msecsTo(vis[i].timestamp);
                    if (ms < m_vp.vStart || ms > m_vp.vEnd) continue;
                    qint64 d = qAbs(ms - targetMs);
                    if (d < bestDist) { bestDist = d; best = i; }
                }
                if (best < 0) { // fallback
                    bestDist = LLONG_MAX;
                    for (int i = 0; i < n; ++i) {
                        qint64 ms = m_vp.tMin.msecsTo(vis[i].timestamp);
                        qint64 d = qAbs(ms - targetMs);
                        if (d < bestDist) { bestDist = d; best = i; }
                    }
                }
                idx = (best >= 0) ? best : 0;
            } else {
                idx = qBound(0, int((hx - marginLeft) * (n - 1) / double(totalWidth)), n - 1);
            }
            const BatteryTimeInfo &cur = vis[idx];

            // SOC 图：竖线 + 横线吸附数据点 y
            int ySoc = chart1Top + chartHeight - int(cur.info.soc * chartHeight / 100.0);
            // 检查是否需要显示日期（如果数据跨越了不同日期）
            qint64 spanMs = m_vp.totalSpanMs();
            QString timeFormat = "HH:mm:ss";
            if (spanMs > 24 * 60 * 60 * 1000) { // 如果时间跨度超过24小时
                timeFormat = "MM-dd HH:mm";
            }
            ChartWidgetBase::drawCrosshair(painter, marginLeft, chart1Top, totalWidth, chartHeight,
                                           hx, ySoc, cur.timestamp.toString(timeFormat));
            ChartWidgetBase::drawTooltipBubble(painter,
                                               QString("%1%").arg(cur.info.soc),
                                               QPointF(hx, ySoc), width(), height());

            // 温度图：竖线 + 横线吸附数据点 y
            const double tempMin = -20, tempMax = 80;
            int yTemp = chart2Top + int((tempMax - cur.info.temp) * chartHeight / (tempMax - tempMin));
            painter.setPen(ChartStyleManager::getHoverPen());
            painter.drawLine(hx, chart2Top, hx, chart2Top + chartHeight);
            painter.drawLine(marginLeft, yTemp, marginLeft + totalWidth, yTemp);
            painter.setPen(ChartStyleManager::getAxisPen());
            painter.drawText(hx, chart2Top - 2, cur.timestamp.toString(timeFormat));
            ChartWidgetBase::drawTooltipBubble(painter,
                                               QString("%1°C").arg(cur.info.temp, 0, 'f', 1),
                                               QPointF(hx, yTemp), width(), height());

            drawBatteryInfoPanel(painter, cur.info);
        }

        // ---- Zoom hint ----
        if (m_vp.isZoomed()) {
            auto ft = ChartStyleManager::getFontTheme();
            QFont smallF = ft.axis;
            smallF.setPointSize(smallF.pointSize() - 1);
            painter.setFont(smallF);
            painter.setPen(QColor(150, 150, 150));
            painter.drawText(marginLeft + totalWidth - 80, chart2Top + chartHeight - 2, "滚轮缩放/拖动");
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

    void leaveEvent(QEvent *) override {
        hoverActive = false;
        update();
    }

private:
    QVector<BatteryTimeInfo> batteryData;
    QPointF hoverPos;
    bool hoverActive = false;
    ChartViewport m_vp;

    QVector<BatteryTimeInfo> visibleSlice() const {
        if (!m_vp.hasData()) return batteryData;
        const int total = batteryData.size();
        if (total == 0) return batteryData;
        qint64 vS = m_vp.vStart, vE = m_vp.vEnd;

        // Single-pass: find first index >= vS and last index <= vE
        int first = total, last = -1;
        for (int i = 0; i < total; ++i) {
            qint64 ms = m_vp.tMin.msecsTo(batteryData[i].timestamp);
            if (ms >= vS && first == total) first = i;
            if (ms <= vE) last = i;
        }

        // Expand by one point on each side for line continuity
        int lo = qMax(0, first - 1);
        int hi = qMin(total - 1, last + 1);
        if (lo > hi) return batteryData; // window completely outside data range — show all

        QVector<BatteryTimeInfo> vis;
        vis.reserve(hi - lo + 1);
        for (int i = lo; i <= hi; ++i) vis.append(batteryData[i]);
        return vis;
    }

    void drawSocChart(QPainter &p, int left, int top, int w, int h,
                      const QVector<BatteryTimeInfo> &data,
                      const QVector<QDateTime> &timestamps,
                      const QDateTime &vpStart, const QDateTime &vpEnd)
    {
        int n = data.size();
        ChartWidgetBase::drawChartFrame(p, left, top, w, h, "电池电量");
        ChartWidgetBase::drawYAxis(p, left, top, h, 10, 0, 100, 10, "%", 1);
        ChartWidgetBase::drawXAxisTime(p, left, top, w, h, timestamps, vpStart, vpEnd);
        QVector<double> vals;
        vals.reserve(n);
        for (const auto &d : data) vals.append(d.info.soc);
        ChartWidgetBase::drawPolyline(p, left, top, w, h, vals, timestamps, 0, 100, vpStart, vpEnd,
                                      QPen(ChartStyleManager::getColorTheme().success, 2));
    }

    void drawTempChart(QPainter &p, int left, int top, int w, int h,
                       const QVector<BatteryTimeInfo> &data,
                       const QVector<QDateTime> &timestamps,
                       const QDateTime &vpStart, const QDateTime &vpEnd)
    {
        int n = data.size();
        const double tempMin = -20, tempMax = 80;
        ChartWidgetBase::drawChartFrame(p, left, top, w, h, "电池温度");
        ChartWidgetBase::drawYAxisTopDown(p, left, top, h, left - 45,
                                          tempMin, tempMax, 10, "°C", 1);
        ChartWidgetBase::drawXAxisTime(p, left, top, w, h, timestamps, vpStart, vpEnd);
        QVector<double> vals;
        vals.reserve(n);
        for (const auto &d : data) vals.append(d.info.temp);
        ChartWidgetBase::drawPolyline(p, left, top, w, h, vals, timestamps, tempMin, tempMax, vpStart, vpEnd,
                                      QPen(ChartStyleManager::getColorTheme().accent, 2));
    }

    void drawBatteryInfoPanel(QPainter &p, const BatteryInfo &info)
    {
        QString sn = info.battery_sn;
        QStringList lines;
        lines << QString("%1: %2%").arg("SOC",  -4).arg(info.soc)
              << QString("%1: %2°C").arg("Temp", -4).arg(info.temp, 0, 'f', 1)
              << QString("%1: %2").arg("Cur",  -4).arg(info.current)
              << QString("%1: %2").arg("Vol",  -4).arg(info.voltage)
              << QString("%1: %2").arg("Abn",  -4).arg(info.is_abnormal)
              << QString("%1: %2").arg("Chrg", -4).arg(info.is_charging)
              << QString("%1: %2").arg("Htg",  -4).arg(info.heating)
              << QString("%1: %2").arg("CanH", -4).arg(info.can_heat)
              << QString("%1: %2").arg("SN_1", -4).arg(sn.left(8))
              << QString("%1: %2").arg("SN_2", -4).arg(sn.mid(8))
              << QString("%1: %2").arg("Cyc",  -4).arg(info.battery_cycles_count)
              << QString("%1: %2").arg("Heal", -4).arg(info.battery_health);

        ChartWidgetBase::drawInfoPanel(p, lines, width(),
                                       ChartStyleManager::getLayoutTheme().marginTop,
                                       ChartStyleManager::getColorTheme().danger,
                                       height());
    }
};

#endif // BATTERYCHARTWIDGET_H
