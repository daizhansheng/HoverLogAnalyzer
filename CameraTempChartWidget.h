#ifndef CAMERATEMPCHARTWIDGET_H
#define CAMERATEMPCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QPainter>
#include <QMouseEvent>
#include "ChartStyleManager.h"

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
        update();
    }

    void clear() {
        samples.clear();
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

        // 边框
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(marginLeft, marginTop, w, h);

        // 标题
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(marginLeft + w - 80, marginTop + 20, "相机温度");

        int n = samples.size();

        // 固定温度范围 0-100
        const int tMin = 0;
        const int tMax = 100;

        // Y 轴刻度
        p.setFont(fontTheme.axis);
        for (int i = 0; i <= 10; ++i) {
            int y = marginTop + i * h / 10;
            double t = tMax - i * (tMax - tMin) / 10.0;
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(marginLeft - 5, y, marginLeft, y);
            p.drawText(marginLeft - 45, y + 5, QString::number((int)t) + "°C");
        }

        // X 轴刻度：根据可用宽度动态稀疏
        {
            QFontMetrics fm(p.font());
            int textWidth = fm.horizontalAdvance("00:00:00") + 10;
            int maxLabels = qMax(1, w / textWidth);
            int step = qMax(1, n / maxLabels);
            for (int i = 0; i < n; i += step) {
                int x = marginLeft + i * w / qMax(1, (n - 1));
                p.setPen(ChartStyleManager::getAxisPen());
                p.drawLine(x, marginTop + h, x, marginTop + h + 5);
                p.drawText(x - 10, marginTop + h + 20, samples[i].timestamp.toString("HH:mm:ss"));
            }
        }

        // 70°C 异常阈值红色虚线
        {
            int y70 = marginTop + (tMax - 70) * h / (tMax - tMin);
            QPen dashPen(ChartStyleManager::getColorTheme().danger, 1, Qt::DashLine);
            p.setPen(dashPen);
            p.drawLine(marginLeft, y70, marginLeft + w, y70);
        }

        // 曲线（统一使用主题蓝色 accent）
        p.setPen(QPen(ChartStyleManager::getColorTheme().accent, 2));
        for (int i = 0; i < n - 1; ++i) {
            double x1 = marginLeft + i * w / double(n - 1);
            double y1 = marginTop + (tMax - samples[i].temperature) * h / (tMax - tMin);
            double x2 = marginLeft + (i + 1) * w / double(n - 1);
            double y2 = marginTop + (tMax - samples[i + 1].temperature) * h / (tMax - tMin);
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }

        // 悬浮十字与提示
        if (hoverIndex >= 0 && hoverIndex < n) {
            int x = marginLeft + hoverIndex * w / double(n - 1);
            int y = marginTop + (tMax - samples[hoverIndex].temperature) * h / (tMax - tMin);

            // 时间文本（顶部）
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawText(x, marginTop - 2, samples[hoverIndex].timestamp.toString("HH:mm:ss"));

            // 十字线
            p.setPen(ChartStyleManager::getHoverPen());
            p.drawLine(x, marginTop, x, marginTop + h);
            p.drawLine(marginLeft, y, marginLeft + w, y);

            // 温度提示框
            QString text = QString("%1°C").arg(samples[hoverIndex].temperature);
            auto fontTheme = ChartStyleManager::getFontTheme();
            p.setFont(fontTheme.tooltip);
            QFontMetrics fm(p.font());
            QRect rect = fm.boundingRect(text).adjusted(-6, -4, 6, 4);
            QPoint centerPos(x + 30, y - 30);
            rect.moveCenter(centerPos);
            p.setBrush(ChartStyleManager::getTooltipBackground());
            p.setPen(ChartStyleManager::getTooltipBorder());
            p.drawRect(rect);
            p.setPen(ChartStyleManager::getColorTheme().text);
            p.drawText(rect, Qt::AlignCenter, text);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (samples.isEmpty()) return;
        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft   = layout.marginLeft;
        int marginRight  = layout.marginRight;
        int w            = width()  - marginLeft - marginRight;
        int x = event->pos().x();
        int n = samples.size();
        if (n < 2 || w <= 0) return;
        if (x < marginLeft) hoverIndex = 0;
        else if (x > marginLeft + w) hoverIndex = n - 1;
        else hoverIndex = qRound((x - marginLeft) * (n - 1) / double(w));
        update();
    }

    void leaveEvent(QEvent *) override {
        hoverIndex = -1;
        update();
    }

private:
    QVector<CameraTempSample> samples;
    int hoverIndex = -1;
};

#endif // CAMERATEMPCHARTWIDGET_H


