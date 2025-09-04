#ifndef SOCTEMPCHARTWIDGET_H
#define SOCTEMPCHARTWIDGET_H

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QColor>
#include <QPainter>
#include <QMouseEvent>
#include "ChartStyleManager.h"

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
        update();
    }

    void addData(QVector<SocTempInfo> &infos) {
        for (auto &info : infos) addData(info);
    }

    void clear() {
        socData.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        if (socData.isEmpty()) return;

        // ================= 动态布局参数 =================
        auto layout = ChartStyleManager::getLayoutTheme();
        int marginLeft   = layout.marginLeft;
        int marginRight  = layout.marginRight;
        int marginTop    = layout.marginTop;
        int marginBottom = layout.marginBottom;
        int chartWidth   = width() - marginLeft - marginRight;
        int chartHeight  = height() - marginTop - marginBottom;
        int chartSpacing = layout.chartSpacing;

        int n = socData.size();

        // 绘制主温度曲线图
        int chartTop = marginTop;
        drawTempChart(p, marginLeft, chartTop, chartWidth, chartHeight, n);

        // 最大温度显示
        drawMaxTempDisplay(p, marginLeft, chartTop, chartWidth, chartHeight);

        // 悬浮显示
        if (hoverActive)
            drawHover(p, marginLeft, chartTop, chartWidth, chartHeight, n);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        hoverPos = event->pos();
        hoverActive = true;
        update();
    }

    void leaveEvent(QEvent *event) override {
        Q_UNUSED(event);
        hoverActive = false;   // 鼠标离开 → 关闭悬浮框
        update();
    }

private:
    QVector<SocTempInfo> socData;

    QPointF hoverPos;
    bool hoverActive = false;

    void drawTempChart(QPainter &p, int left, int top, int w, int h, int n) {
        // 找到最大温度
        int maxY = 0;
        for (auto &d : socData) if (d.maxTemp > maxY) maxY = d.maxTemp;
        maxY = qMax(maxY, 130); // 至少显示到 130

        // 绘制边框
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(left, top, w, h);

        // 绘制标题
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(left + w - 80, top + 20, "CPU温度");

        // Y 轴刻度
        p.setFont(fontTheme.axis);
        for (int y=0; y<=maxY; y+=10) {
            int py = top + h - (y * h / maxY);
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(left-5, py, left, py);
            p.drawText(5, py, QString::number(y)+"°C");
        }

        // X 轴刻度 (时间)
        int step = qMax(1, n/6);
        for (int i=0; i<n; i+=step) {
            int px = left + i * w / (n-1);
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(px, top+h, px, top+h+5);
            p.drawText(px-10, top+h+20, socData[i].timestamp.toString("HH:mm:ss"));
        }

        // 最大温度曲线 (红色虚线) - 使用统一的红色
        QPen pen(ChartStyleManager::getColorTheme().danger, 2, Qt::DashLine);
        p.setPen(pen);
        QPolygon poly;
        for (int i=0; i<n; i++) {
            int val = socData[i].maxTemp;
            int px = left + i * w / (n-1);
            int py = top + h - (val * h / maxY);
            poly << QPoint(px, py);
        }
        p.drawPolyline(poly);
    }

    void drawMaxTempDisplay(QPainter &p, int left, int top, int w, int h) {
        if (socData.isEmpty()) return;
        int maxTemp = socData.last().maxTemp;

    }

    void drawHover(QPainter &p, int left, int top, int w, int h, int n) {
        if (n < 2) return;

        // 计算鼠标对应索引
        int idx = (hoverPos.x() - left) * (n - 1) / w;
        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;

        const SocTempInfo &d = socData[idx];
        int px = left + idx * w / (n - 1);

        // ---------- 十字线 ----------
        p.setPen(ChartStyleManager::getHoverPen());
        p.drawLine(px, top-20, px, top + h);

        int maxY = 0;
        for (auto &item : socData) if (item.maxTemp > maxY) maxY = item.maxTemp;
        maxY = qMax(maxY, 130);
        int py = top + h - (d.maxTemp * h / maxY);
        p.drawLine(left, py, left + w, py);

        // ---------- 顶部显示时间 ----------
        QString timeStr = d.timestamp.toString("HH:mm:ss");
        p.setPen(Qt::black);
        p.drawText(px - 30, top - 2, timeStr);

        // ---------- 悬浮显示最大温度 ----------
        QString text = QString("%1°C").arg(d.maxTemp);
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.tooltip);
        QFontMetrics fm1(p.font());
        QRect rect = fm1.boundingRect(text).adjusted(-6, -4, 6, 4);
        QPoint centerPos(px + 30, py - 40);        // 十字线右上方
        rect.moveCenter(centerPos);

        // 背景框
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(rect);

        // 文字
        p.setPen(ChartStyleManager::getColorTheme().text);
        p.drawText(rect, Qt::AlignCenter, text);

        // ---------- 右侧信息框显示各核心温度 ----------
        QStringList lines;
        for (int i = 0; i < d.coreTemps.size(); ++i)
            lines << QString("CPU%1: %2°C").arg(i).arg(d.coreTemps[i]);

        QFontMetrics fm(font());
        int wBox = 0;
        for (auto &s : lines) wBox = qMax(wBox, fm.horizontalAdvance(s));
        int hBox = lines.size() * fm.height() + 8;

        int xBox = width() - 100;
        int yBox = top + 20;        // 图表顶部 + 20px
        if (yBox + hBox > height() - 10) yBox = height() - hBox - 10;

        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(xBox, yBox, wBox + 5, hBox);

        // 设置字体和颜色
        int ty = yBox + fm.ascent() + 4;
        p.setPen(ChartStyleManager::getColorTheme().danger);
        for (auto &s : lines) {
            p.drawText(xBox + 5, ty, s);
            ty += fm.height();
        }
    }
};

#endif // SOCTEMPCHARTWIDGET_H
