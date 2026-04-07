#ifndef MODULEUSAGECHART_H
#define MODULEUSAGECHART_H
#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMap>
#include <QString>
#include <QMouseEvent>
#include <QWheelEvent>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"
#include "ChartViewportMixin.h"

// ================= 模块占用结构体 =================
struct ModuleUsage {
    double cpu = 0.0;
    double mem = 0.0;
};

// ================= 全部模块数据结构体 =================
struct AllModuleUsage {
    QDateTime timestamp;
    ModuleUsage camera_service;
    ModuleUsage captain;
    ModuleUsage fcs;
    ModuleUsage control_engine;
    ModuleUsage drvf_msg_monito;
    ModuleUsage top;
    ModuleUsage vio_hover;
    ModuleUsage logd;
    ModuleUsage exception_manag;
    ModuleUsage bt_service;
    ModuleUsage battery_service;
    ModuleUsage gimbal_service;
    ModuleUsage kworker_u18_icp_message_q;
    ModuleUsage logcat;
    ModuleUsage kworker_u19_kgsl_events;
    ModuleUsage systemd;
    ModuleUsage kthreadd;
    ModuleUsage rcu_gp;
    ModuleUsage rcu_par_gp;
    ModuleUsage kworker_0_events;
    ModuleUsage fpv_service;
};

// ================= 曲线绘制控件 =================
class ModuleUsageChart : public QWidget {
    Q_OBJECT
public:
    explicit ModuleUsageChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumSize(400, 300);

        moduleNames = QStringList() << "camera_service" << "captain" << "fcs"
                                    << "control_engine" << "drvf_msg_monito" << "top"
                                    << "vio_hover" << "logd" << "exception_manag"
                                    << "bt_service" << "battery_service" << "gimbal_service"
                                    << "kworker_u18_icp_message_q" << "logcat"
                                    << "kworker_u19_kgsl_events" << "systemd"
                                    << "kthreadd" << "rcu_gp" << "rcu_par_gp"
                                    << "kworker_0_events" << "fpv_service";

        for (const auto &name : moduleNames)
            moduleVisible[name] = false;

        moduleColors.clear();
        for (int i = 0; i < moduleNames.size(); ++i) {
            moduleVisible[moduleNames[i]] = false;
            moduleColors[moduleNames[i]] = colors[i];
        }
    }

    void setData(const QVector<AllModuleUsage> &data) {
        allData = data;
        // Compute tMin/tMax and reset viewport
        QDateTime mn, mx;
        for (const auto &d : allData) {
            if (!d.timestamp.isValid()) continue;
            if (!mn.isValid() || d.timestamp < mn) mn = d.timestamp;
            if (!mx.isValid() || d.timestamp > mx) mx = d.timestamp;
        }
        if (mn.isValid()) {
            m_vp.setRange(mn, mx);
            if (allData.size() >= 2) {
                qint64 span = mn.msecsTo(mx);
                qint64 interval = span / (allData.size() - 1);
                m_vp.minWindowMs = qMax((qint64)500, interval * 3);
            }
        } else {
            m_vp.clear();
        }
        update();
    }

    QMap<QString,bool>& getModuleVisibility() { return moduleVisible; }

    QColor getModuleColor(QString name) { return moduleColors[name]; }

protected:
    void paintEvent(QPaintEvent *) override {
        if (allData.isEmpty()) return;

        QPainter p(this);
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        auto layout = ChartStyleManager::getLayoutTheme();
        const int marginLeft   = layout.marginLeft;
        const int marginRight  = layout.marginRight;
        const int marginTop    = layout.marginTop;
        const int marginBottom = layout.marginBottom;
        const int chartWidth   = width()  - marginLeft - marginRight;
        const int chartHeight  = height() - marginTop  - marginBottom;

        // ---- Slice to visible window ----
        QVector<AllModuleUsage> vis = visibleSlice();
        int n = vis.size();
        if (n < 1) return; // nothing visible in current zoom window

        // ---- Viewport time boundaries ----
        QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : vis.first().timestamp;
        QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : vis.last().timestamp;

        // Compute Y axis max (25% steps)
        double maxCpu = 10.0, maxMem = 10.0;
        for (int m = 0; m < moduleNames.size(); ++m) {
            if (!moduleVisible[moduleNames[m]]) continue;
            for (const auto &d : vis) {
                maxCpu = qMax(maxCpu, getCpuByIndex(d, m));
                maxMem = qMax(maxMem, getMemByIndex(d, m));
            }
        }
        auto scaleValue = [](double val) -> double {
            if (val <= 0) return 25.0;
            int step = 25;
            int scaled = int((val + step - 1) / step) * step;
            return qMin(scaled, 250);
        };
        double yAxisMax = qMax(scaleValue(maxCpu), scaleValue(maxMem));
        int ySteps = qMax(1, int(yAxisMax / 25));

        ChartWidgetBase::drawChartFrame(p, marginLeft, marginTop, chartWidth, chartHeight, "CPU/MEM占用率");
        ChartWidgetBase::drawYAxis(p, marginLeft, marginTop, chartHeight, 5, 0, yAxisMax, ySteps, "%", 1);

        QVector<QDateTime> timestamps;
        timestamps.reserve(n);
        for (const auto &d : vis) timestamps.append(d.timestamp);
        ChartWidgetBase::drawXAxisTime(p, marginLeft, marginTop, chartWidth, chartHeight, timestamps, vpStart, vpEnd);

        for (int m = 0; m < moduleNames.size(); ++m) {
            if (!moduleVisible[moduleNames[m]]) continue;
            QVector<double> cpuVals;
            cpuVals.reserve(n);
            for (int i = 0; i < n; ++i)
                cpuVals.append(getCpuByIndex(vis[i], m));
            ChartWidgetBase::drawPolyline(p, marginLeft, marginTop, chartWidth, chartHeight,
                                          cpuVals, timestamps, 0, yAxisMax, vpStart, vpEnd,
                                          QPen(colors[m % colors.size()], 2));
        }

        // ---- Hover crosshair + info box ----
        if (mousePos.x() >= marginLeft && mousePos.x() <= marginLeft + chartWidth && n >= 2) {
            qint64 vpSpan = vpStart.msecsTo(vpEnd);

            int idx;
            if (vpSpan > 0) {
                qint64 targetMs = m_vp.vStart + (qint64)((mousePos.x() - marginLeft) * vpSpan / double(chartWidth));
                int best = 0; qint64 bestDist = LLONG_MAX;
                for (int i = 0; i < n; ++i) {
                    qint64 ms   = m_vp.tMin.msecsTo(vis[i].timestamp);
                    qint64 dist = qAbs(ms - targetMs);
                    if (dist < bestDist) { bestDist = dist; best = i; }
                }
                idx = best;
            } else {
                idx = qBound(0, int((mousePos.x() - marginLeft) * (n - 1) / double(chartWidth)), n - 1);
            }

            int moduleIdx = 0;
            for (int m = 0; m < moduleNames.size(); ++m)
                if (moduleVisible[moduleNames[m]]) { moduleIdx = m; break; }

            double cpuVal = getCpuByIndex(vis[idx], moduleIdx);
            int x = (vpSpan > 0)
                ? marginLeft + int(vpStart.msecsTo(vis[idx].timestamp) * chartWidth / double(vpSpan))
                : marginLeft + int(idx * chartWidth / double(n - 1));
            int yCpu = marginTop + chartHeight - int(cpuVal * chartHeight / yAxisMax);

            // 检查是否需要显示日期（如果数据跨越了不同日期）
            qint64 spanMs = m_vp.totalSpanMs();
            QString timeFormat = "HH:mm:ss";
            if (spanMs > 24 * 60 * 60 * 1000) { // 如果时间跨度超过24小时
                timeFormat = "MM-dd HH:mm";
            }
            ChartWidgetBase::drawCrosshair(p, marginLeft, marginTop, chartWidth, chartHeight,
                                           x, yCpu, vis[idx].timestamp.toString(timeFormat));
            drawHoverInfo(p, vis[idx]);
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
        mousePos = QPoint(-1, -1);
        hoverIndex = -1;
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
        if (allData.isEmpty()) return;
        auto layout = ChartStyleManager::getLayoutTheme();
        const int marginLeft  = layout.marginLeft;
        const int chartWidth  = width() - marginLeft - layout.marginRight;
        int x = event->pos().x();

        if (m_vp.dragging && (event->buttons() & Qt::LeftButton)) {
            m_vp.doDrag(event->pos().x(), chartWidth);
            mousePos = QPoint(-1, -1);
            hoverIndex = -1;
            update();
            return;
        }

        QVector<AllModuleUsage> vis = visibleSlice();
        int n = vis.size();
        if (n < 2 || chartWidth <= 0) { mousePos = QPoint(-1,-1); hoverIndex = -1; update(); return; }

        if (!m_vp.hasData()) {
            if (x < marginLeft)                hoverIndex = 0;
            else if (x > marginLeft + chartWidth) hoverIndex = n - 1;
            else hoverIndex = qRound((x - marginLeft) * (n - 1) / double(chartWidth));
        } else {
            qint64 vpSpan   = m_vp.vEnd - m_vp.vStart;
            qint64 targetMs = m_vp.vStart + (qint64)((x - marginLeft) * vpSpan / double(chartWidth));
            int best = 0; qint64 bestDist = LLONG_MAX;
            for (int i = 0; i < n; ++i) {
                qint64 ms   = m_vp.tMin.msecsTo(vis[i].timestamp);
                qint64 dist = qAbs(ms - targetMs);
                if (dist < bestDist) { bestDist = dist; best = i; }
            }
            hoverIndex = best;
        }
        mousePos = event->pos();
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
        mousePos = QPoint(-1, -1);
        hoverIndex = -1;
        update();
    }

    void leaveEvent(QEvent *event) override {
        Q_UNUSED(event);
        hoverIndex = -1;
        mousePos   = QPoint(-1, -1);
        update();
    }

private:
    QVector<AllModuleUsage> allData;
    QStringList moduleNames;
    QMap<QString,bool>  moduleVisible;
    QMap<QString,QColor> moduleColors;
    QPoint mousePos;
    int hoverIndex = -1;
    ChartViewport m_vp;

    QVector<AllModuleUsage> visibleSlice() const {
        if (!m_vp.hasData()) return allData;
        const int total = allData.size();
        if (total == 0) return allData;
        qint64 vS = m_vp.vStart, vE = m_vp.vEnd;

        int first = total, last = -1;
        for (int i = 0; i < total; ++i) {
            qint64 ms = m_vp.tMin.msecsTo(allData[i].timestamp);
            if (ms >= vS && first == total) first = i;
            if (ms <= vE) last = i;
        }
        int lo = qMax(0, first - 1);
        int hi = qMin(total - 1, last + 1);
        if (lo > hi) return allData;

        QVector<AllModuleUsage> vis;
        vis.reserve(hi - lo + 1);
        for (int i = lo; i <= hi; ++i) vis.append(allData[i]);
        return vis;
    }

    // 模块名缩写表
    static QString moduleShortName(const QString &name) {
        static const QMap<QString,QString> table = {
            {"camera_service",            "CS"},
            {"control_engine",            "CE"},
            {"captain",                   "CP"},
            {"fcs",                       "FC"},
            {"drvf_msg_monito",           "DM"},
            {"top",                       "TP"},
            {"vio_hover",                 "VH"},
            {"logd",                      "LD"},
            {"exception_manag",           "EM"},
            {"bt_service",                "BS"},
            {"battery_service",           "BS"},
            {"gimbal_service",            "GS"},
            {"kworker_u18_icp_message_q", "K1"},
            {"logcat",                    "LC"},
            {"kworker_u19_kgsl_events",   "K2"},
            {"systemd",                   "SD"},
            {"kthreadd",                  "KT"},
            {"rcu_gp",                    "RG"},
            {"rcu_par_gp",                "RP"},
            {"kworker_0_events",          "K0"},
            {"fpv_service",               "FS"},
        };
        return table.value(name, name.left(2).toUpper());
    }

    void drawHoverInfo(QPainter &p, const AllModuleUsage &info) {
        QStringList lines;
        for (int i = 0; i < moduleNames.size(); ++i) {
            if (!moduleVisible[moduleNames[i]]) continue;
            double cpuVal = getCpuByIndex(info, i);
            double memVal = getMemByIndex(info, i);
            lines << QString("%1: CPU:%2%,MEM:%3%")
                         .arg(moduleShortName(moduleNames[i]))
                         .arg(cpuVal, 0, 'f', 1)
                         .arg(memVal, 0, 'f', 1);
        }
        if (lines.isEmpty()) return;

        auto fontTheme = ChartStyleManager::getFontTheme();
        QFont f = fontTheme.tooltip;
        f.setWeight(QFont::Light);
        p.setFont(f);
        QFontMetrics fm(p.font());
        int maxW = 0;
        for (const QString &s : lines) maxW = qMax(maxW, fm.horizontalAdvance(s));
        int wBox = maxW + 10;

        int rightX;
        if (mousePos.x() + 40 + wBox < width() - 10)
            rightX = mousePos.x() + 40 + wBox;
        else
            rightX = mousePos.x() - 40;

        ChartWidgetBase::drawInfoPanel(p, lines, rightX,
                                       mousePos.y() - 20,
                                       Qt::black, height());
    }

    QVector<QColor> colors = {
        QColor(180, 0,   0),    QColor(0,   0,   180),  QColor(0,   150, 0),
        QColor(0,   120, 120),  QColor(120, 0,   120),  QColor(180, 180, 0),
        QColor(0,   150, 150),  QColor(120, 0,   0),    QColor(0,   0,   120),
        QColor(0,   120, 0),    QColor(100, 100, 100),  QColor(90,  0,   90),
        QColor(100, 100, 0),    QColor(50,  50,  50),   QColor(160, 160, 160),
        QColor(0,   120, 120),  QColor(0,   50,  180),  QColor(180, 0,   0),
        QColor(0,   180, 0),    QColor(120, 60,  0),    QColor(180, 100, 100),
        QColor(100, 180, 100),  QColor(100, 100, 180),  QColor(180, 180, 100),
        QColor(180, 100, 180)
    };

    double getCpuByIndex(const AllModuleUsage &u, int idx) const {
        switch(idx){
        case 0:  return u.camera_service.cpu;
        case 1:  return u.captain.cpu;
        case 2:  return u.fcs.cpu;
        case 3:  return u.control_engine.cpu;
        case 4:  return u.drvf_msg_monito.cpu;
        case 5:  return u.top.cpu;
        case 6:  return u.vio_hover.cpu;
        case 7:  return u.logd.cpu;
        case 8:  return u.exception_manag.cpu;
        case 9:  return u.bt_service.cpu;
        case 10: return u.battery_service.cpu;
        case 11: return u.gimbal_service.cpu;
        case 12: return u.kworker_u18_icp_message_q.cpu;
        case 13: return u.logcat.cpu;
        case 14: return u.kworker_u19_kgsl_events.cpu;
        case 15: return u.systemd.cpu;
        case 16: return u.kthreadd.cpu;
        case 17: return u.rcu_gp.cpu;
        case 18: return u.rcu_par_gp.cpu;
        case 19: return u.kworker_0_events.cpu;
        case 20: return u.fpv_service.cpu;
        default: return 0.0;
        }
    }
    double getMemByIndex(const AllModuleUsage &u, int idx) const {
        switch(idx){
        case 0:  return u.camera_service.mem;
        case 1:  return u.captain.mem;
        case 2:  return u.fcs.mem;
        case 3:  return u.control_engine.mem;
        case 4:  return u.drvf_msg_monito.mem;
        case 5:  return u.top.mem;
        case 6:  return u.vio_hover.mem;
        case 7:  return u.logd.mem;
        case 8:  return u.exception_manag.mem;
        case 9:  return u.bt_service.mem;
        case 10: return u.battery_service.mem;
        case 11: return u.gimbal_service.mem;
        case 12: return u.kworker_u18_icp_message_q.mem;
        case 13: return u.logcat.mem;
        case 14: return u.kworker_u19_kgsl_events.mem;
        case 15: return u.systemd.mem;
        case 16: return u.kthreadd.mem;
        case 17: return u.rcu_gp.mem;
        case 18: return u.rcu_par_gp.mem;
        case 19: return u.kworker_0_events.mem;
        case 20: return u.fpv_service.mem;
        default: return 0.0;
        }
    }
};

#endif // MODULEUSAGECHART_H
