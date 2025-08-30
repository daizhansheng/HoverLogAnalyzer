#ifndef BATTERYCHARTWIDGET_H
#define BATTERYCHARTWIDGET_H
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>
#include <QString>

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
        setMinimumSize(400, 400);
    }

    void setData(const QVector<BatteryTimeInfo> &data) {
        batteryData = data;
        update();
    }

    void clear() {
        batteryData.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);

        if (batteryData.isEmpty()) return;

        int marginLeft   = 60;
        int marginRight  = 60;
        int marginTop    = 30;
        int marginBottom = 40;
        int chartWidth   = 380;
        int chartHeight  = 150;
        int chartSpacing = 40;

        int n = batteryData.size();
        int chart1Top = marginTop;
        int chart2Top = chart1Top + chartHeight + chartSpacing;

        drawSocChart(painter, marginLeft, chart1Top, chartWidth, chartHeight, n);
        drawTempChart(painter, marginLeft, chart2Top, chartWidth, chartHeight, n);

        // SOC 和 Temp 十字线
        if (hoverIndex >= 0 && hoverIndex < n) {
            int x = marginLeft + hoverIndex * chartWidth / double(n - 1);
            QDateTime ts = batteryData[hoverIndex].timestamp;
            painter.setPen(Qt::black);

            // 上面 SOC 图上方显示时间
            painter.drawText(x, chart1Top - 2, ts.toString("HH:mm:ss"));

            // 下面 Temp 图上方显示时间
            painter.drawText(x, chart2Top - 2, ts.toString("HH:mm:ss"));

            // 竖线覆盖两图
            painter.setPen(QPen(Qt::DashLine));
            painter.drawLine(x, chart1Top, x, chart2Top + chartHeight);

            // SOC水平虚线
            int ySoc = chart1Top + chartHeight - batteryData[hoverIndex].info.soc * chartHeight / 100.0;
            painter.drawLine(marginLeft, ySoc, marginLeft + chartWidth, ySoc);

            // Temp水平虚线
            double temp = batteryData[hoverIndex].info.temp;
            double tempMin = -20, tempMax = 80;
            int yTemp = chart2Top + (tempMax - temp) * chartHeight / (tempMax - tempMin);
            painter.drawLine(marginLeft, yTemp, marginLeft + chartWidth, yTemp);

            // 悬浮数值
            drawHoverText(painter, QString("%1%").arg(batteryData[hoverIndex].info.soc), QPointF(x, ySoc));
            drawHoverText(painter, QString("%1°C").arg(temp, 0, 'f', 1), QPointF(x, yTemp));

            // 右侧完整信息
            drawHoverInfo(painter, batteryData[hoverIndex].info, QPointF(width()+10 , chart1Top));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (batteryData.isEmpty()) {
            hoverIndex = -1;
            return;
        }

        int marginLeft = 60;
        int chartWidth = 380;  // 固定宽度，与 paintEvent 保持一致

        int n = batteryData.size();
        int x = event->pos().x();

        // 限制 x 在绘图区域内
        if (x < marginLeft) hoverIndex = 0;
        else if (x > marginLeft + chartWidth) hoverIndex = n - 1;
        else hoverIndex = qRound((x - marginLeft) * (n - 1) / double(chartWidth));

        update();
    }

    void leaveEvent(QEvent *) override {
        hoverIndex = -1;
        update();
    }

private:
    QVector<BatteryTimeInfo> batteryData;
    int hoverIndex = -1;

    void drawSocChart(QPainter &p, int left, int top, int w, int h, int n) {
        p.setPen(Qt::black);
        p.drawRect(left, top, w, h);
        p.drawText(left + w - 60, top + 15, "电池电量");

        // Y轴刻度
        for (int i = 0; i <= 10; ++i) {
            int y = top + h - i * h / 10;
            p.setPen(Qt::black);
            p.drawLine(left - 5, y, left, y);
            p.drawText(10, y + 5, QString::number(i * 10) + "%");

            if (i < 10) {
                int minorCount = 1;
                for (int j = 1; j <= minorCount; ++j) {
                    int yMinor = y - j * h / 10 / (minorCount + 1);
                    p.setPen(QPen(Qt::black, 1, Qt::DashLine));
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }

        // X轴刻度，自动稀疏
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int maxLabels = w / textWidth;
        int step = qMax(1, n / maxLabels);
        // step *= 2; // 再稀疏一倍

        for (int i = 0; i < n; i += step) {
            int x = left + i * w / double(n - 1);
            QDateTime ts = batteryData[i].timestamp;
            p.setPen(Qt::black);
            p.drawLine(x, top + h, x, top + h + 5);
            p.drawText(x-10 , top + h + 20, ts.toString("HH:mm:ss"));
        }

        // SOC曲线
        p.setPen(QPen(Qt::green, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = left + i * w / double(n - 1);
            double y1 = top + h - batteryData[i].info.soc * h / 100.0;
            double x2 = left + (i + 1) * w / double(n - 1);
            double y2 = top + h - batteryData[i + 1].info.soc * h / 100.0;
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }

    void drawTempChart(QPainter &p, int left, int top, int w, int h, int n) {
        p.setPen(Qt::black);
        p.drawRect(left, top, w, h);
        p.drawText(left + w - 60, top + 15, "电池温度");

        double tempMin = -20, tempMax = 80;

        // Y轴刻度
        for (int i = 0; i <= 10; ++i) {
            int y = top + i * h / 10;
            double t = tempMax - i * (tempMax - tempMin) / 10;
            p.setPen(Qt::black);
            p.drawLine(left - 5, y, left, y);
            p.drawText(left - 45, y + 5, QString::number((int)t) + "°C");

            if (i < 10) {
                int minorCount = 1;
                for (int j = 1; j <= minorCount; ++j) {
                    int yMinor = y + j * h / 10 / (minorCount + 1);
                    p.setPen(QPen(Qt::black, 1, Qt::DashLine));
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }

        // X轴刻度，自动稀疏
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int maxLabels = w / textWidth;
        int step = qMax(1, n / maxLabels);
        // step *= 2; // 再稀疏一倍

        for (int i = 0; i < n; i += step) {
            int x = left + i * w / double(n - 1);
            QDateTime ts = batteryData[i].timestamp;
            p.setPen(Qt::black);
            p.drawLine(x, top + h, x, top + h + 5);
            p.drawText(x-10, top + h + 20, ts.toString("HH:mm:ss"));
        }

        // Temp曲线
        p.setPen(QPen(Qt::red, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = left + i * w / double(n - 1);
            double y1 = top + (tempMax - batteryData[i].info.temp) * h / (tempMax - tempMin);
            double x2 = left + (i + 1) * w / double(n - 1);
            double y2 = top + (tempMax - batteryData[i + 1].info.temp) * h / (tempMax - tempMin);
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }
    void drawHoverText(QPainter &p, const QString &text, const QPointF &pos) {
        QFontMetrics fm(p.font());
        QRect rect = fm.boundingRect(text).adjusted(-4, -2, 4, 2);
        rect.moveCenter(QPoint(pos.x() + 40, pos.y() - 20));
        p.setBrush(QColor(255, 255, 225));
        p.setPen(Qt::black);
        p.drawRect(rect);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    void drawHoverInfo(QPainter &p, const BatteryInfo &info, const QPointF &mousePos) {
        QString text = QString(
                           "SOC: %1%\nTemp: %2°C\nCurrent: %3\nVoltage: %4\nAbnormal: %5\nCharging: %6\nHeating: %7\nCan Heat: %8\nSN: %9\nCycles: %10\nHealth: %11")
                           .arg(info.soc)
                           .arg(info.temp, 0, 'f', 1)
                           .arg(info.current)
                           .arg(info.voltage)
                           .arg(info.is_abnormal)
                           .arg(info.is_charging)
                           .arg(info.heating)
                           .arg(info.can_heat)
                           .arg(info.battery_sn)
                           .arg(info.battery_cycles_count)
                           .arg(info.battery_health);

        QFontMetrics fm(p.font());
        QRect rect = fm.boundingRect(0, 0, 200, 200, Qt::TextWordWrap, text);
        // rect.adjusted(4, 2, -4, -2);

        QPointF pos(mousePos);
        int hoverOffset = 20;

        pos.setX(mousePos.x() + hoverOffset);
        if (pos.x() + rect.width() > width() - 10)
            pos.setX(mousePos.x() - rect.width() - hoverOffset);
        if (pos.y() + rect.height() > height() - 10)
            pos.setY(height() - rect.height() - 10);
        if (pos.y() < 10) pos.setY(10);

        rect.moveTopLeft(pos.toPoint());

        p.setBrush(QColor(255, 255, 225, 220));
        p.setPen(Qt::darkRed);
        p.drawRect(rect);
        p.drawText(rect, Qt::TextWordWrap | Qt::AlignLeft, text);
    }
};

#endif // BATTERYCHARTWIDGET_H
