#ifndef BATTERYCHARTWIDGET_H
#define BATTERYCHARTWIDGET_H
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>
#include <QString>
#include "ChartStyleManager.h"

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
        update();
    }

    void clear() {
        batteryData.clear();
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
        int chartSpacing = layout.chartSpacing + 30;  // 增加图表间距30像素
        int chartHeight  = (height() - marginTop - marginBottom - chartSpacing) / 2;  // 均匀分配高度

        int n = batteryData.size();
        int chart1Top = marginTop;
        int chart2Top = chart1Top + chartHeight + chartSpacing;

        drawSocChart(painter, marginLeft, chart1Top, totalWidth, chartHeight, n);
        drawTempChart(painter, marginLeft, chart2Top, totalWidth, chartHeight, n);

        // SOC 和 Temp 十字线
        if (hoverIndex >= 0 && hoverIndex < n) {
            int x = marginLeft + hoverIndex * totalWidth / double(n - 1);
            QDateTime ts = batteryData[hoverIndex].timestamp;
            painter.setPen(ChartStyleManager::getAxisPen());

            // 上面 SOC 图上方显示时间
            painter.drawText(x, chart1Top - 2, ts.toString("HH:mm:ss"));

            // 下面 Temp 图上方显示时间
            painter.drawText(x, chart2Top - 2, ts.toString("HH:mm:ss"));

            // 竖线覆盖两图
            painter.setPen(ChartStyleManager::getHoverPen());
            painter.drawLine(x, chart1Top, x, chart2Top + chartHeight);

            // SOC水平虚线
            int ySoc = chart1Top + chartHeight - batteryData[hoverIndex].info.soc * chartHeight / 100.0;
            painter.drawLine(marginLeft, ySoc, marginLeft + totalWidth, ySoc);

            // Temp水平虚线
            double temp = batteryData[hoverIndex].info.temp;
            double tempMin = -20, tempMax = 80;
            int yTemp = chart2Top + (tempMax - temp) * chartHeight / (tempMax - tempMin);
            painter.drawLine(marginLeft, yTemp, marginLeft + totalWidth, yTemp);

            // 悬浮数值
            drawHoverText(painter, QString("%1%").arg(batteryData[hoverIndex].info.soc), QPointF(x, ySoc));
            drawHoverText(painter, QString("%1°C").arg(temp, 0, 'f', 1), QPointF(x, yTemp));

            // 右侧完整信息
            drawHoverInfo(painter, batteryData[hoverIndex].info, QPointF(width() - 20, chart1Top));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (batteryData.isEmpty()) {
            hoverIndex = -1;
            return;
        }

        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft = layout.marginLeft;
        int chartWidth = width() - marginLeft - layout.marginRight;  // 动态宽度

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
        // 绘制边框
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(left, top, w, h);

        // 绘制标题
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(left + w - 80, top + 20, "电池电量");

        // Y轴刻度
        p.setFont(fontTheme.axis);
        for (int i = 0; i <= 10; ++i) {
            int y = top + h - i * h / 10;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(left - 5, y, left, y);
            p.drawText(10, y + 5, QString::number(i * 10) + "%");

            if (i < 10) {
                int minorCount = 1;
                for (int j = 1; j <= minorCount; ++j) {
                    int yMinor = y - j * h / 10 / (minorCount + 1);
                    p.setPen(ChartStyleManager::getGridPen());
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }

        // X轴刻度，自动稀疏
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int maxLabels = w / textWidth;
        int step = qMax(1, n / maxLabels);

        for (int i = 0; i < n; i += step) {
            int x = left + i * w / double(n - 1);
            QDateTime ts = batteryData[i].timestamp;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(x, top + h, x, top + h + 5);
            p.drawText(x-10 , top + h + 20, ts.toString("HH:mm:ss"));
        }

        // SOC曲线 - 使用统一的绿色
        p.setPen(QPen(ChartStyleManager::getColorTheme().success, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = left + i * w / double(n - 1);
            double y1 = top + h - batteryData[i].info.soc * h / 100.0;
            double x2 = left + (i + 1) * w / double(n - 1);
            double y2 = top + h - batteryData[i + 1].info.soc * h / 100.0;
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }

    void drawTempChart(QPainter &p, int left, int top, int w, int h, int n) {
        // 绘制边框
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(left, top, w, h);

        // 绘制标题
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(left + w - 80, top + 20, "电池温度");

        double tempMin = -20, tempMax = 80;

        // Y轴刻度
        p.setFont(fontTheme.axis);
        for (int i = 0; i <= 10; ++i) {
            int y = top + i * h / 10;
            double t = tempMax - i * (tempMax - tempMin) / 10;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(left - 5, y, left, y);
            p.drawText(left - 45, y + 5, QString::number((int)t) + "°C");

            if (i < 10) {
                int minorCount = 1;
                for (int j = 1; j <= minorCount; ++j) {
                    int yMinor = y + j * h / 10 / (minorCount + 1);
                    p.setPen(ChartStyleManager::getGridPen());
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }

        // X轴刻度，自动稀疏
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int maxLabels = w / textWidth;
        int step = qMax(1, n / maxLabels);

        for (int i = 0; i < n; i += step) {
            int x = left + i * w / double(n - 1);
            QDateTime ts = batteryData[i].timestamp;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(x, top + h, x, top + h + 5);
            p.drawText(x-10, top + h + 20, ts.toString("HH:mm:ss"));
        }

        // Temp曲线 - 使用统一的红色
        p.setPen(QPen(ChartStyleManager::getColorTheme().accent, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = left + i * w / double(n - 1);
            double y1 = top + (tempMax - batteryData[i].info.temp) * h / (tempMax - tempMin);
            double x2 = left + (i + 1) * w / double(n - 1);
            double y2 = top + (tempMax - batteryData[i + 1].info.temp) * h / (tempMax - tempMin);
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    }
    void drawHoverText(QPainter &p, const QString &text, const QPointF &pos) {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.tooltip);
        QFontMetrics fm(p.font());
        QRect rect = fm.boundingRect(text).adjusted(-6, -3, 6, 3);

        // 智能定位，避免遮挡
        QPoint centerPos;
        if (pos.x() + 40 + rect.width()/2 < width() - 10) {
            // 右侧有空间，显示在右侧
            centerPos = QPoint(pos.x() + 40, pos.y() - 20);
        } else {
            // 右侧空间不够，显示在左侧
            centerPos = QPoint(pos.x() - 40 - rect.width()/2, pos.y() - 20);
        }

        // 垂直位置调整
        if (centerPos.y() - rect.height()/2 < 10) {
            centerPos.setY(10 + rect.height()/2);
        }
        if (centerPos.y() + rect.height()/2 > height() - 10) {
            centerPos.setY(height() - 10 - rect.height()/2);
        }

        rect.moveCenter(centerPos);
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(rect);
        p.setPen(ChartStyleManager::getColorTheme().text);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    void drawHoverInfo(QPainter &p, const BatteryInfo &info, const QPointF &mousePos) {
        auto fontTheme = ChartStyleManager::getFontTheme();
        QFont smallerFont = fontTheme.tooltip;
        smallerFont.setPointSize(smallerFont.pointSize());  // 减小字号
        smallerFont.setWeight(QFont::Light);  // 设置为细体
        p.setFont(smallerFont);

        // 将电池序列号分成两行显示
        QString sn = info.battery_sn;
        QString snFirst = sn.left(8);   // 前8字节
        QString snSecond = sn.mid(8);   // 后8字节

        // 格式化文本，使标签左对齐，冒号对齐
        QString text = QString(
                           "%1: %2%\n%3: %4°C\n%5: %6\n%7: %8\n%9: %10\n%11: %12\n%13: %14\n%15: %16\n%17: %18\n%19: %20\n%21: %22\n%23: %24")
                           .arg("SOC", -4)          // 左对齐，占8个字符宽度
                           .arg(info.soc)
                           .arg("Temp", -4)
                           .arg(info.temp, 0, 'f', 1)
                           .arg("Cur", -4)
                           .arg(info.current)
                           .arg("Vol", -4)
                           .arg(info.voltage)
                           .arg("Abn", -4)
                           .arg(info.is_abnormal)
                           .arg("Chrg", -4)
                           .arg(info.is_charging)
                           .arg("Htg", -4)
                           .arg(info.heating)
                           .arg("CanH", -4)
                           .arg(info.can_heat)
                           .arg("SN_1", -4)
                           .arg(snFirst)
                           .arg("SN_2", -4)
                           .arg(snSecond)
                           .arg("Cyc", -4)
                           .arg(info.battery_cycles_count)
                           .arg("Heal", -4)
                           .arg(info.battery_health);

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

        // 固定在右侧预留空间内显示
        QPointF pos;
        pos.setX(width() - wBox - 10);  // 右侧预留空间内，留10px边距
        pos.setY(mousePos.y());

        // 垂直位置调整，避免超出边界
        if (pos.y() + hBox > height() - 10) {
            pos.setY(height() - hBox - 10);
        }
        if (pos.y() < 10) {
            pos.setY(10);
        }

        // 绘制背景和边框
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(pos.x(), pos.y(), wBox+5, hBox);

        // 设置字体和颜色
        int ty = pos.y() + fm.ascent() + 4;
        p.setPen(ChartStyleManager::getColorTheme().danger);
        for (auto &s : lines) {
            p.drawText(pos.x() + 5, ty, s);
            ty += fm.height();
        }
    }
};

#endif // BATTERYCHARTWIDGET_H
