#pragma once
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QMouseEvent>
#include <QString>

struct BatteryInfo {
    int soc = 0;                 // 电量 0-100
    int current = 0;             // 电流 0-1000
    int voltage = 0;             // 电压 0-1000
    double temp = 0.0;           // 温度 0-100
    int is_abnormal = 0;         // 异常状态 0-1
    int is_charging = 0;         // 充电状态 0-1
    int heating = 0;             // 加热状态 0-1
    int can_heat = 0;            // 可加热 0-1
    QString battery_sn;          // 电池序列号
    int battery_cycles_count = 0;// 循环次数
    int battery_health = 0;      // 电池健康
};

class BatteryChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BatteryChartWidget(QWidget *parent = nullptr)
        : QWidget(parent) {
        setMouseTracking(true); // 开启鼠标追踪
        setMinimumSize(400, 200);
    }

    void setData(const QVector<BatteryInfo> &data) {
        batteryData = data;
        update(); // 触发重绘
    }
    void clear() {            // ← 新增清空方法
        batteryData.clear();
        update();             // 触发重绘
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);

        if (batteryData.isEmpty())
            return;

        // 边距
        int marginLeft = 50;
        int marginBottom = 30;
        int marginTop = 20;
        int marginRight = 40;

        int w = width() - marginLeft - marginRight;
        int h = height() - marginTop - marginBottom;

        int n = batteryData.size();

        painter.setPen(Qt::black);
        QFont titleFont = painter.font();
        titleFont.setPointSize(11);
        painter.setFont(titleFont);
        painter.drawText(marginLeft + w - 80, marginTop + 15, "电池电量");

        // 坐标轴
        painter.setPen(Qt::black);
        painter.drawLine(marginLeft, marginTop, marginLeft, marginTop + h); // Y轴
        painter.drawLine(marginLeft, marginTop + h, marginLeft + w, marginTop + h); // X轴

        // 绘制SOC曲线
        painter.setPen(QPen(Qt::green, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = marginLeft + i * w / double(n - 1);
            double y1 = marginTop + h - batteryData[i].soc * h / 100.0;
            double x2 = marginLeft + (i + 1) * w / double(n - 1);
            double y2 = marginTop + h - batteryData[i + 1].soc * h / 100.0;
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }

        // Y轴刻度（大刻度+小刻度）
        painter.setPen(Qt::black);
        int majorTickCount = 10;
        int minorTickCount = 1;
        for (int i = 0; i <= majorTickCount; ++i) {
            int y = marginTop + h - i * h / majorTickCount;
            painter.drawLine(marginLeft - 5, y, marginLeft, y);
            painter.drawText(5, y + 5, QString::number(i * 10)+"%");

            if (i < majorTickCount) {
                for (int j = 1; j <= minorTickCount; ++j) {
                    int yMinor = y - j * h / majorTickCount / (minorTickCount + 1);
                    painter.drawLine(marginLeft - 3, yMinor, marginLeft, yMinor);
                }
            }
        }

        // X轴刻度（动态显示大刻度）
        int pointIntervalSec = 2;       // 每个数据点间隔2秒
        int maxTicks = 10;
        int step = qMax(1, n / maxTicks);

        for (int i = 0; i < n; i += step) {
            int x = marginLeft + i * w / double(n - 1);
            painter.drawLine(x, marginTop + h, x, marginTop + h + 10);
            int seconds = i * pointIntervalSec;
            painter.drawText(x - 15, marginTop + h + 25, QString::number(seconds) + "s");
        }

        // 绘制鼠标悬停十字线
        if (hoverIndex >= 0 && hoverIndex < n) {
            int x = marginLeft + hoverIndex * w / double(n - 1);
            int y = marginTop + h - batteryData[hoverIndex].soc * h / 100.0;

            painter.setPen(QPen(Qt::red, 1, Qt::DashLine));
            painter.drawLine(x, marginTop, x, marginTop + h);       // 垂直线
            painter.drawLine(marginLeft, y, marginLeft + w, y);     // 水平线

            painter.setPen(Qt::black);
            int totalSeconds = hoverIndex * pointIntervalSec;
            int minutes = totalSeconds / 60;
            int seconds = totalSeconds % 60;
            QString timeStr = QString("%1:%2")
                                  .arg(minutes, 2, 10, QChar('0'))
                                  .arg(seconds, 2, 10, QChar('0'));
            painter.drawText(x - 15, marginTop - 5, timeStr);
            painter.drawText(marginLeft + w + 5, y + 5, QString::number(batteryData[hoverIndex].soc) + "%");
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (batteryData.isEmpty()) {
            hoverIndex = -1;
            return;
        }

        int marginLeft = 50;
        int marginRight = 40;
        int w = width() - marginLeft - marginRight;

        int n = batteryData.size();
        int x = event->pos().x();
        hoverIndex = qRound((x - marginLeft) * (n - 1) / double(w));
        if (hoverIndex < 0) hoverIndex = 0;
        if (hoverIndex >= n) hoverIndex = n - 1;

        update();
    }

    void leaveEvent(QEvent *) override {
        hoverIndex = -1;
        update();
    }

private:
    QVector<BatteryInfo> batteryData;
    int hoverIndex = -1;  // 当前鼠标悬停点索引
};
