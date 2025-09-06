#ifndef MODULEUSAGECHART_H
#define MODULEUSAGECHART_H
#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMap>
#include <QString>
#include <QPolygon>
#include <QMouseEvent>
#include "ChartStyleManager.h"

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
            moduleColors[moduleNames[i]] = colors[i];  // 直接对应
        }
    }

    void setData(const QVector<AllModuleUsage> &data) {
        allData = data;
        update();
    }

    QMap<QString,bool>& getModuleVisibility() { return moduleVisible; }

    QColor getModuleColor(QString name){return moduleColors[name];}
protected:
    void paintEvent(QPaintEvent *) override {
        if (allData.isEmpty()) return;

        QPainter p(this);
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        auto layout = ChartStyleManager::getLayoutTheme();
        const int marginLeft = layout.marginLeft;
        const int marginRight = layout.marginRight;
        const int marginTop = layout.marginTop;
        const int marginBottom = layout.marginBottom;
        const int chartWidth = width() - marginLeft - marginRight;  // 动态宽度
        const int chartHeight = height() - marginTop - marginBottom;

        // ---------------- 绘制坐标轴 ----------------
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(marginLeft, marginTop, chartWidth, chartHeight);

        // ---------------- 绘制CPU/MEM占用率标题 ----------------
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(marginLeft + chartWidth - 120, marginTop + 20, "CPU/MEM占用率");

        int n = allData.size();

        // ---------------- 计算 Y 轴最大值 ----------------
        double maxCpu = 10.0, maxMem = 10.0;
        for (int m = 0; m < moduleNames.size(); ++m) {
            if (!moduleVisible[moduleNames[m]]) continue;
            for (const auto &d : allData) {
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

        // ---------------- 绘制 Y 轴刻度（主+次） ----------------
        p.setFont(fontTheme.axis);
        const int stepMajor = 25;
        for (int val = 0; val <= yAxisMax; val += stepMajor) {
            int py = marginTop + chartHeight - int(val * chartHeight / yAxisMax);
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(marginLeft-5, py, marginLeft, py);                // 主刻度短线
            p.drawText(5, py+4, QString::number(val) + "%");           // 主刻度文字

            // 在主刻度与下一个主刻度之间画一个次刻度短线（不画网格线）
            int midVal = val + stepMajor/2; // 12.5%
            if (midVal < yAxisMax) {
                int pyMid = marginTop + chartHeight - int(midVal * chartHeight / yAxisMax);
                p.setPen(ChartStyleManager::getGridPen());
                p.drawLine(marginLeft-3, pyMid, marginLeft, pyMid);      // 次刻度短线（无文字）
            }
        }

        // ---------------- 绘制曲线 ----------------
        for (int m=0; m<moduleNames.size(); ++m) {
            if (!moduleVisible[moduleNames[m]]) continue;
            QPolygon cpuPoly, memPoly;
            for (int i=0; i<n; ++i) {
                double px = marginLeft + i*chartWidth / double(n-1);
                int pyCpu = marginTop + chartHeight - int(getCpuByIndex(allData[i], m)*chartHeight/yAxisMax);
                int pyMem = marginTop + chartHeight - int(getMemByIndex(allData[i], m)*chartHeight/yAxisMax);
                cpuPoly << QPoint(px, pyCpu);
                memPoly << QPoint(px, pyMem);
            }
            p.setPen(QPen(colors[m%colors.size()],2));
            p.drawPolyline(cpuPoly);
            // p.setPen(QPen(colors[m%colors.size()],1,Qt::DashLine));
            // p.drawPolyline(memPoly);
        }

        // ---------------- 绘制 X 轴刻度 (时间) ----------------
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int maxLabels = qMin(chartWidth / textWidth, 7);   // 最多显示 7 个刻度
        int stepLabel = n > maxLabels ? n / maxLabels : 1;

        for (int i = 0; i < n; i += stepLabel) {
            if (i == 0 || i == n-1 || i % stepLabel == 0) {
                int px = marginLeft + i * chartWidth / double(n-1);
                QString tStr = allData[i].timestamp.toString("HH:mm:ss");
                p.setPen(ChartStyleManager::getAxisPen());
                p.drawText(px - textWidth/2, marginTop + chartHeight + 20, tStr);
                p.drawLine(px, marginTop + chartHeight, px, marginTop + chartHeight + 5);
            }
        }
        // ---------------- 绘制十字线 ----------------
        if(mousePos.x()>=marginLeft && mousePos.x()<=marginLeft+chartWidth) {
            int idx = qBound(0,int((mousePos.x()-marginLeft)*(n-1)/double(chartWidth)),n-1);

            int moduleIdx=0;
            for(int m=0;m<moduleNames.size();++m)
                if(moduleVisible[moduleNames[m]]) { moduleIdx=m; break; }

            double cpuVal = getCpuByIndex(allData[idx], moduleIdx);
            double memVal = getMemByIndex(allData[idx], moduleIdx);
            int x = marginLeft + idx*chartWidth/double(n-1);
            int yCpu = marginTop + chartHeight - int(cpuVal*chartHeight/yAxisMax);
            int yMem = marginTop + chartHeight - int(memVal*chartHeight/yAxisMax);

            // 在十字线上方显示时间
            QDateTime ts = allData[idx].timestamp;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawText(x, marginTop - 2, ts.toString("HH:mm:ss"));

            // 使用统一的深灰色虚线样式
            p.setPen(ChartStyleManager::getHoverPen());
            p.drawLine(x,marginTop,x,marginTop+chartHeight);
            p.drawLine(marginLeft,yCpu,marginLeft+chartWidth,yCpu);
            // p.drawLine(marginLeft,yMem,marginLeft+chartWidth,yMem);

            // 绘制跟随鼠标的悬浮信息框
            drawHoverInfo(p, allData[idx], mousePos);
        }

    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if(allData.isEmpty()) return;
        auto layout = ChartStyleManager::getLayoutTheme();
        const int marginLeft = layout.marginLeft;
        const int chartWidth = width() - marginLeft - layout.marginRight;
        int n = allData.size();
        int x = event->pos().x();
        if(x<marginLeft) hoverIndex=0;
        else if(x>marginLeft+chartWidth) hoverIndex=n-1;
        else hoverIndex=qRound((x-marginLeft)*(n-1)/double(chartWidth));
        mousePos = event->pos();
        update();
    }

    void leaveEvent(QEvent *event) override {
        Q_UNUSED(event);
        hoverIndex = -1;          // 表示无效索引
        mousePos = QPoint(-1,-1); // 鼠标位置设为无效
        update();                 // 触发重绘，清除十字线
    }

private:
    QVector<AllModuleUsage> allData;
    QStringList moduleNames;
    QMap<QString,bool> moduleVisible;
    QMap<QString,QColor> moduleColors;
    QPoint mousePos;
    int hoverIndex = -1;

    void drawHoverInfo(QPainter &p, const AllModuleUsage &info, const QPointF &mousePos) {
        auto fontTheme = ChartStyleManager::getFontTheme();
        QFont smallerFont = fontTheme.tooltip;
        smallerFont.setPointSize(smallerFont.pointSize());
        smallerFont.setWeight(QFont::Light);  // 设置为细体
        p.setFont(smallerFont);

        // 只显示选中的模块信息
        QStringList visibleModules;
        for (int i = 0; i < moduleNames.size(); ++i) {
            if (moduleVisible[moduleNames[i]]) {
                double cpuVal = getCpuByIndex(info, i);
                double memVal = getMemByIndex(info, i);
                QString moduleName = moduleNames[i];
                // 将模块名缩写为更具体的缩写，并转为大写
                QString shortName;
                if (moduleName == "camera_service") {
                    shortName = "CS";
                } else if (moduleName == "control_engine") {
                    shortName = "CE";
                } else if (moduleName == "captain") {
                    shortName = "CP";
                } else if (moduleName == "fcs") {
                    shortName = "FC";
                } else if (moduleName == "drvf_msg_monito") {
                    shortName = "DM";
                } else if (moduleName == "top") {
                    shortName = "TP";
                } else if (moduleName == "vio_hover") {
                    shortName = "VH";
                } else if (moduleName == "logd") {
                    shortName = "LD";
                } else if (moduleName == "exception_manag") {
                    shortName = "EM";
                } else if (moduleName == "bt_service") {
                    shortName = "BS";
                } else if (moduleName == "battery_service") {
                    shortName = "BS";
                } else if (moduleName == "gimbal_service") {
                    shortName = "GS";
                } else if (moduleName == "kworker_u18_icp_message_q") {
                    shortName = "K1";
                } else if (moduleName == "logcat") {
                    shortName = "LC";
                } else if (moduleName == "kworker_u19_kgsl_events") {
                    shortName = "K2";
                } else if (moduleName == "systemd") {
                    shortName = "SD";
                } else if (moduleName == "kthreadd") {
                    shortName = "KT";
                } else if (moduleName == "rcu_gp") {
                    shortName = "RG";
                } else if (moduleName == "rcu_par_gp") {
                    shortName = "RP";
                } else if (moduleName == "kworker_0_events") {
                    shortName = "K0";
                } else if (moduleName == "fpv_service") {
                    shortName = "FS";
                } else {
                    shortName = moduleName.left(2).toUpper();  // 默认取前两个字符
                }
                visibleModules.append(QString("%1: CPU:%2%,MEM:%3%")
                    .arg(shortName)
                    .arg(cpuVal, 0, 'f', 1)
                    .arg(memVal, 0, 'f', 1));
            }
        }

        if (visibleModules.isEmpty()) {
            return; // 没有选中的模块，不显示悬浮框
        }

        QString text = visibleModules.join("\n");

        QFontMetrics fm(p.font());

        // 计算实际文本尺寸
        QStringList lines = text.split('\n');
        int maxWidth = 0;
        for (const QString &line : lines) {
            int lineWidth = fm.horizontalAdvance(line);
            if (lineWidth > maxWidth) {
                maxWidth = lineWidth;
            }
        }

        // 计算外框尺寸，添加内边距
        int wBox = maxWidth + 10;  // 左右各5px内边距
        int hBox = lines.size() * fm.height() + 8;  // 上下各4px内边距

        // 智能定位，避免遮挡
        QPointF pos;
        if (mousePos.x() + 40 + wBox/2 < width() - 10) {
            // 右侧有空间，显示在右侧
            pos = QPointF(mousePos.x() + 40, mousePos.y() - 20);
        } else {
            // 右侧空间不够，显示在左侧
            pos = QPointF(mousePos.x() - 40 - wBox/2, mousePos.y() - 20);
        }

        // 垂直位置调整
        if (pos.y() - hBox/2 < 10) {
            pos.setY(10 + hBox/2);
        }
        if (pos.y() + hBox/2 > height() - 10) {
            pos.setY(height() - 10 - hBox/2);
        }

        // 绘制背景和边框
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(pos.x(), pos.y(), wBox, hBox);

        // 设置字体和颜色，统一使用黑色
        int ty = pos.y() + fm.ascent() + 4;
        p.setPen(QColor(0, 0, 0));  // 统一使用黑色
        for (auto &s : lines) {
            p.drawText(pos.x() + 5, ty, s);
            ty += fm.height();
        }
    }
    QVector<QColor> colors = {
        QColor(180, 0, 0),      // 暗红
        QColor(0, 0, 180),      // 暗蓝
        QColor(0, 150, 0),      // 暗绿
        QColor(0, 120, 120),    // 暗青
        QColor(120, 0, 120),    // 暗紫
        QColor(180, 180, 0),    // 暗黄
        QColor(0, 150, 150),    // 深青
        QColor(120, 0, 0),      // 暗红2
        QColor(0, 0, 120),      // 暗蓝2
        QColor(0, 120, 0),      // 暗绿2
        QColor(100, 100, 100),  // 灰色
        QColor(90, 0, 90),      // 暗紫2
        QColor(100, 100, 0),    // 暗黄2
        QColor(50, 50, 50),     // 深灰
        QColor(160, 160, 160),  // 浅灰
        QColor(0, 120, 120),    // 暗青2
        QColor(0, 50, 180),     // 蓝2
        QColor(180, 0, 0),      // 红2
        QColor(0, 180, 0),      // 绿2
        QColor(120, 60, 0),     // 棕色
        QColor(180, 100, 100),  // 暖粉
        QColor(100, 180, 100),  // 浅绿
        QColor(100, 100, 180),  // 浅蓝
        QColor(180, 180, 100),  // 柠檬黄
        QColor(180, 100, 180)  // 紫粉
    };
    double getCpuByIndex(const AllModuleUsage &u, int idx) {
        switch(idx){
        case 0: return u.camera_service.cpu;
        case 1: return u.captain.cpu;
        case 2: return u.fcs.cpu;
        case 3: return u.control_engine.cpu;
        case 4: return u.drvf_msg_monito.cpu;
        case 5: return u.top.cpu;
        case 6: return u.vio_hover.cpu;
        case 7: return u.logd.cpu;
        case 8: return u.exception_manag.cpu;
        case 9: return u.bt_service.cpu;
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
    double getMemByIndex(const AllModuleUsage &u, int idx) {
        switch(idx){
        case 0: return u.camera_service.mem;
        case 1: return u.captain.mem;
        case 2: return u.fcs.mem;
        case 3: return u.control_engine.mem;
        case 4: return u.drvf_msg_monito.mem;
        case 5: return u.top.mem;
        case 6: return u.vio_hover.mem;
        case 7: return u.logd.mem;
        case 8: return u.exception_manag.mem;
        case 9: return u.bt_service.mem;
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
