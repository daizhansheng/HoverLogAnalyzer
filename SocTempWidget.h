#pragma once

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include <QColor>
#include <QPainter>
#include <QMouseEvent>

// ================= 数据结构 =================
struct SocTempInfo {
    QDateTime timestamp;      // 时间戳 (含日期+时间+毫秒)
    int maxTemp = 0;          // SoC 最大温度
    QVector<int> coreTemps;   // 8 核心温度
};

// ================= 绘图控件 =================
class SocTempChart : public QWidget {
    Q_OBJECT
public:
    explicit SocTempChart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setFixedSize(500, 400); // 固定大小
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
        p.fillRect(rect(), Qt::white);

        if (socData.isEmpty()) return;

        // ================= 统一布局参数 =================
        int marginLeft   = 60;
        int marginRight  = 60;
        int marginTop    = 30;
        int marginBottom = 40;
        int chartWidth   = 450;
        int chartHeight  = 150;
        int chartSpacing = 40;

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

        // 坐标轴
        p.setPen(Qt::black);
        p.drawRect(left, top, w, h);

        // Y 轴刻度
        for (int y=0; y<=maxY; y+=10) {
            int py = top + h - (y * h / maxY);
            p.drawLine(left-5, py, left, py);
            p.drawText(5, py, QString::number(y)+"°C");
        }

        // X 轴刻度 (时间)
        int step = qMax(1, n/6);
        for (int i=0; i<n; i+=step) {
            int px = left + i * w / (n-1);
            p.drawLine(px, top+h, px, top+h+5);
            p.drawText(px-10, top+h+20, socData[i].timestamp.toString("HH:mm:ss"));
        }

        // 最大温度曲线 (红色虚线)
        QPen pen(Qt::red, 2, Qt::DashLine);
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

        QFont font = this->font();
        font.setPointSize(14);
        p.setFont(font);
        p.setPen(Qt::black);

        QString text = QString("CPU温度");
        int textWidth = p.fontMetrics().horizontalAdvance(text);
        p.drawText(width() - textWidth - 205, top + 20, text);
    }

    void drawHover(QPainter &p, int left, int top, int w, int h, int n) {
        if (n < 2) return;

        // 计算鼠标对应索引
        int idx = (hoverPos.x() - left) * (n - 1) / w;
        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;

        const SocTempInfo &d = socData[idx];
        int px = left + idx * w / (n - 1);

        // ========== 加粗十字线 ==========
        QPen linePen(Qt::gray, 1, Qt::DashLine); // 宽度改为1
        p.setPen(linePen);
        p.drawLine(px, top-20, px, top + h);

        int maxY = 0;
        for (auto &item : socData)
            if (item.maxTemp > maxY) maxY = item.maxTemp;
        maxY = qMax(maxY, 130);

        int py = top + h - (d.maxTemp * h / maxY);
        p.drawLine(left, py, left + w, py);

        // ========== 顶部显示 HH:mm:ss ==========
        QString timeStr = d.timestamp.toString("HH:mm:ss");
        p.setPen(Qt::black);
        p.drawText(px , top -2, timeStr); // 十字架上方显示

        // ========== 固定右侧信息框 ==========
        QStringList lines;
        for (int i = 0; i < d.coreTemps.size(); i++)
            lines << QString("CPU%1: %2°C").arg(i).arg(d.coreTemps[i]);
        lines << QString("MaxTmp: %1°C").arg(d.maxTemp);

        QFontMetrics fm(font());
        int wBox = 0;
        for (auto &s : lines) wBox = qMax(wBox, fm.horizontalAdvance(s));
        int hBox = lines.size() * fm.height() + 8;

        int xBox = left + w + 20;  // 图表右侧 + 10px
        int yBox = top + 20;       // 图表顶部 + 20px

        if (xBox + wBox + 10 > width()) xBox = width() - wBox - 10;
        if (yBox + hBox > height() - 10) yBox = height() - hBox - 10;

        p.setBrush(QColor(255, 255, 220));
        p.setPen(Qt::darkRed);
        p.drawRect(xBox, yBox, wBox + 25, hBox);

        int ty = yBox + fm.ascent() + 4;
        for (auto &s : lines) {
            p.drawText(xBox + 5, ty, s);
            ty += fm.height();
        }
    }
};
