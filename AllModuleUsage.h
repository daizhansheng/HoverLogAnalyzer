#pragma once
#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMap>
#include <QString>
#include <QPolygon>
#include <QMouseEvent>

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
};

// ================= 曲线绘制控件 =================
class ModuleUsageChart : public QWidget {
    Q_OBJECT
public:
    explicit ModuleUsageChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumHeight(250);
        setMaximumHeight(250);

        moduleNames = QStringList() << "camera_service" << "captain" << "fcs"
                                    << "control_engine" << "drvf_msg_monito" << "top"
                                    << "vio_hover" << "logd" << "exception_manag"
                                    << "bt_service" << "battery_service" << "gimbal_service"
                                    << "kworker_u18_icp_message_q" << "logcat"
                                    << "kworker_u19_kgsl_events" << "systemd"
                                    << "kthreadd" << "rcu_gp" << "rcu_par_gp"
                                    << "kworker_0_events";

        for (const auto &name : moduleNames)
            moduleVisible[name] = false;

        colors = {Qt::red, Qt::blue, Qt::green, Qt::darkCyan, Qt::magenta, Qt::yellow,
                  Qt::cyan, Qt::darkRed, Qt::darkBlue, Qt::darkGreen, Qt::gray, Qt::darkMagenta,
                  Qt::darkYellow, Qt::black, Qt::darkGray, Qt::lightGray, Qt::darkCyan, Qt::blue,
                  Qt::red, Qt::green};

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
        p.fillRect(rect(), Qt::white);

        const int marginLeft = 60;
        const int marginRight = 150;
        const int marginTop = 20;
        const int marginBottom = 40;
        const int chartWidth = 550;  // 固定宽度
        const int chartHeight = height() - marginTop - marginBottom;

        // ---------------- 绘制坐标轴 ----------------
        p.setPen(Qt::black);
        p.drawRect(marginLeft, marginTop, chartWidth, chartHeight);

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

        // ---------------- 绘制 Y 轴刻度 ----------------
        QList<int> yTicks = {25,50,75,100,125,150,175,200,225,250};
        for(int val : yTicks) {
            int py = marginTop + chartHeight - int(val * chartHeight / yAxisMax);
            p.setPen(Qt::black);
            p.drawLine(marginLeft-5, py, marginLeft, py);
            p.drawText(5, py+4, QString::number(val) + "%");
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
            p.setPen(QPen(colors[m%colors.size()],1,Qt::DashLine));
            p.drawPolyline(memPoly);
        }

        // ---------------- 绘制 X 轴刻度 (时间) ----------------
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10; // 每个刻度占据的最小宽度
        int maxLabels = chartWidth / textWidth;                 // 最大刻度数量
        int stepLabel = n > maxLabels ? n / maxLabels : 1;

        for(int i=0; i<n; i+=stepLabel) {
            int px = marginLeft + i * chartWidth / double(n-1);
            QString tStr = allData[i].timestamp.toString("HH:mm:ss");
            p.setPen(Qt::black);
            p.drawText(px - textWidth/2, marginTop + chartHeight + 20, tStr);
            p.drawLine(px, marginTop + chartHeight, px, marginTop + chartHeight + 5);
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

            p.setPen(QPen(Qt::darkGray,1,Qt::DashLine));
            p.drawLine(x,marginTop,x,marginTop+chartHeight);
            p.drawLine(marginLeft,yCpu,marginLeft+chartWidth,yCpu);
            p.drawLine(marginLeft,yMem,marginLeft+chartWidth,yMem);

            p.setPen(Qt::black);
            QString timeStr = allData[idx].timestamp.toString("HH:mm:ss");
            p.drawText(x-30, marginTop-1, timeStr);
            p.drawText(marginLeft+chartWidth+1, yCpu-10, QString("CPU:%1%").arg(cpuVal,0,'f',1));
            p.drawText(marginLeft+chartWidth+1, yCpu+5, QString("MEM:%1%").arg(memVal,0,'f',1));
        }

        // ---------------- 绘制图例 ----------------
        // int legendX = marginLeft + chartWidth + 10;
        // int legendY = marginTop;
        // int colorIndex = 0;

        // for (int m = 0; m < moduleNames.size(); ++m) {
        //     const QString &name = moduleNames[m];
        //     if (!moduleVisible[name]) continue;

        //     // 限制文字长度
        //     QString displayName = name;
        //     if (displayName.length() > 12)
        //         displayName = displayName.left(9) + "...";

        //     // 使用模块对应颜色
        //     p.setPen(colors[m % colors.size()]);  // 这里用 m 而不是 colorIndex

        //     // 绘制文字（不画横线）
        //     p.drawText(legendX, legendY + colorIndex * 20 + 12, displayName);

        //     colorIndex++;
        // }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if(allData.isEmpty()) return;
        const int marginLeft = 60;
        const int chartWidth = 550;
        int n = allData.size();
        int x = event->pos().x();
        if(x<marginLeft) hoverIndex=0;
        else if(x>marginLeft+chartWidth) hoverIndex=n-1;
        else hoverIndex=qRound((x-marginLeft)*(n-1)/double(chartWidth));
        mousePos = event->pos();
        update();
    }


private:
    QVector<AllModuleUsage> allData;
    QStringList moduleNames;
    QMap<QString,bool> moduleVisible;
    QVector<QColor> colors;
    QMap<QString,QColor> moduleColors;
    QPoint mousePos;
    int hoverIndex = -1;

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
        default: return 0.0;
        }
    }
};
