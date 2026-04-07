#ifndef CHARTBASEWIDGET_H
#define CHARTBASEWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QFontMetrics>
#include <QDateTime>
#include "ChartStyleManager.h"

// ============================================================
// 统一绘图基类：提供边距、内容区域及所有图表共用的绘制模板函数
// ============================================================
class ChartWidgetBase : public QWidget {
public:
    explicit ChartWidgetBase(QWidget *parent = nullptr)
        : QWidget(parent) {}

public:
    // ---- 所有绘制 helper 均为 public static，方便非子类（如 FlightAnimationWidget）调用 ----

    // ============================================================
    // 1. 绘制图表边框 + 右上角标题
    // ============================================================
    static void drawChartFrame(QPainter &p, int left, int top, int w, int h,
                               const QString &title)
    {
        p.setPen(ChartStyleManager::getBorderPen());
        p.drawRect(left, top, w, h);

        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(left + w - 80, top + 20, title);
    }

    // ============================================================
    // 2. 绘制Y轴刻度（主刻度 + 可选次刻度，底部=minVal）
    //    labelX    : 刻度文字X坐标（通常 < left）
    //    steps     : 主刻度数量
    //    unit      : 单位，如 "%" 或 "°C"
    //    minorTicks: 每段主刻度间的次刻度数（0=不绘）
    // ============================================================
    static void drawYAxis(QPainter &p, int left, int top, int h,
                          int labelX,
                          double minVal, double maxVal, int steps,
                          const QString &unit, int minorTicks = 1)
    {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.axis);
        for (int i = 0; i <= steps; ++i) {
            double val = minVal + (maxVal - minVal) * i / steps;
            int y = top + h - int(i * h / double(steps));
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(left - 5, y, left, y);
            p.drawText(labelX, y + 5, QString::number((int)val) + unit);
            if (minorTicks > 0 && i < steps) {
                for (int j = 1; j <= minorTicks; ++j) {
                    int yMinor = y - int(j * h / double(steps) / (minorTicks + 1));
                    p.setPen(ChartStyleManager::getGridPen());
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }
    }

    // ============================================================
    // 3. 绘制Y轴刻度（顶部=maxVal，从顶到底）
    // ============================================================
    static void drawYAxisTopDown(QPainter &p, int left, int top, int h,
                                 int labelX,
                                 double minVal, double maxVal, int steps,
                                 const QString &unit, int minorTicks = 1)
    {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.axis);
        for (int i = 0; i <= steps; ++i) {
            double val = maxVal - (maxVal - minVal) * i / steps;
            int y = top + int(i * h / double(steps));
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(left - 5, y, left, y);
            p.drawText(labelX, y + 5, QString::number((int)val) + unit);
            if (minorTicks > 0 && i < steps) {
                for (int j = 1; j <= minorTicks; ++j) {
                    int yMinor = y + int(j * h / double(steps) / (minorTicks + 1));
                    p.setPen(ChartStyleManager::getGridPen());
                    p.drawLine(left - 3, yMinor, left, yMinor);
                }
            }
        }
    }

    // ============================================================
    // 4a. 绘制X轴时间刻度（均匀索引映射，无视口参数时使用）
    // ============================================================
    static void drawXAxisTime(QPainter &p, int left, int top, int w, int h,
                              const QVector<QDateTime> &timestamps, int maxLabels = 7)
    {
        int n = timestamps.size();
        if (n == 0) return;
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.axis);
        QFontMetrics fm(p.font());
        int textWidth = fm.horizontalAdvance("00:00:00") + 10;
        int autoMax = qMax(1, w / textWidth);
        int step = computeSparseStep(n, qMin(maxLabels, autoMax));
        int labelW = fm.horizontalAdvance("00:00:00") + 8;
        for (int i = 0; i < n; i += step) {
            int x = left + (n > 1 ? int(i * w / double(n - 1)) : 0);
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(x, top + h, x, top + h + 5);
            p.drawText(x - labelW / 2, top + h + 20, timestamps[i].toString("HH:mm:ss"));
        }
    }

    // ============================================================
    // 4b. 绘制X轴时间刻度（时间比例映射，带视口范围）
    //     vpStart/vpEnd : 视口起止时间（对应像素 left 和 left+w）
    //     刻度按均匀时间间隔生成，位置按真实时间比例计算，与数据点无关
    // ============================================================
    static void drawXAxisTime(QPainter &p, int left, int top, int w, int h,
                              const QVector<QDateTime> &timestamps,
                              const QDateTime &vpStart, const QDateTime &vpEnd,
                              int maxLabels = 7)
    {
        Q_UNUSED(timestamps); Q_UNUSED(maxLabels);
        if (!vpStart.isValid() || !vpEnd.isValid()) return;
        qint64 spanMs = vpStart.msecsTo(vpEnd);
        if (spanMs <= 0) return;

        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.axis);
        QFontMetrics fm(p.font());

        int labelW   = fm.horizontalAdvance("00:00:00") + 8;
        int labelGap = 6;

        // 像素宽度能并排放多少标签（至少 4，保证最小窗口也有足够刻度）
        int maxTicks = qMax(4, w / (labelW + labelGap));

        // 候选 nice 间隔（ms），从小到大
        static const qint64 niceMsSteps[] = {
            100, 200, 500,
            1000, 2000, 5000, 10000, 15000, 30000,
            60000, 120000, 300000, 600000, 900000, 1800000,
            3600000, 7200000, 10800000, 21600000, 43200000, 86400000
        };
        static const int nSteps = (int)(sizeof(niceMsSteps) / sizeof(niceMsSteps[0]));

        // 目标：选最小的 nice step 满足：
        //   step >= spanMs / maxTicks   （标签总数不超出像素容量）
        // 同时保底：step <= spanMs / 2  （视口内至少 2 个刻度）
        qint64 minStep = spanMs / qMax(1, maxTicks); // 不超出容量的最小间隔
        qint64 tickIntervalMs = spanMs / 2;           // 保底：至少 2 个刻度
        for (int si = 0; si < nSteps; ++si) {
            if (niceMsSteps[si] >= minStep) {
                tickIntervalMs = niceMsSteps[si];
                // 若选出的 step > spanMs/2，强制回退到 spanMs/2，保证至少 2 个刻度
                if (tickIntervalMs > spanMs / 2) tickIntervalMs = spanMs / 2;
                break;
            }
        }
        if (tickIntervalMs <= 0) tickIntervalMs = 1;

        // 第一个 tick：向下对齐到 tickIntervalMs 的整数倍（floor），
        // 确保视口左侧附近就有刻度线，不会跳过大半视口
        qint64 epochStart = vpStart.toMSecsSinceEpoch();
        qint64 firstTick  = (epochStart / tickIntervalMs) * tickIntervalMs;
        // 若 firstTick 在视口之前也没关系，循环会从它开始向右推进

        // 绘制：碰撞检测保证标签不重叠
        int lastLabelRight = left - labelW;
        for (qint64 tickEpoch = firstTick; ; tickEpoch += tickIntervalMs) {
            qint64 ms = tickEpoch - epochStart;
            if (ms > spanMs) break;
            if (ms < 0) continue; // 跳过视口左侧的 tick
            int x = left + int(ms * w / double(spanMs));
            int labelLeft = x - labelW / 2;

            if (labelLeft < lastLabelRight + labelGap) {
                p.setPen(ChartStyleManager::getAxisPen());
                p.drawLine(x, top + h, x, top + h + 3);
                continue;
            }

            QDateTime dt = QDateTime::fromMSecsSinceEpoch(tickEpoch);
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawLine(x, top + h, x, top + h + 5);
            // 检查是否需要显示日期（如果数据跨越了不同日期）
            QString timeFormat = "HH:mm:ss";
            if (spanMs > 24 * 60 * 60 * 1000) { // 如果时间跨度超过24小时
                timeFormat = "MM-dd HH:mm";
            }
            p.drawText(labelLeft, top + h + 20, dt.toString(timeFormat));
            lastLabelRight = labelLeft + labelW;
        }
    }

    // ============================================================
    // 5. 绘制悬停十字准线（垂直 + 水平，顶部显示时间）
    // ============================================================
    static void drawCrosshair(QPainter &p, int left, int top, int w, int h,
                              int px, int py, const QString &timeStr)
    {
        p.setPen(ChartStyleManager::getAxisPen());
        p.drawText(px, top - 2, timeStr);
        p.setPen(ChartStyleManager::getHoverPen());
        p.drawLine(px, top, px, top + h);
        p.drawLine(left, py, left + w, py);
    }

    // ============================================================
    // 6. 绘制小型 tooltip 气泡
    //    anchor   : 十字交叉点像素坐标
    //    widgetW/H: 所属 widget 宽高（边界检测）
    // ============================================================
    static void drawTooltipBubble(QPainter &p, const QString &text,
                                  const QPointF &anchor,
                                  int widgetW, int widgetH)
    {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.tooltip);
        QFontMetrics fm(p.font());
        QRect rect = fm.boundingRect(text).adjusted(-6, -4, 6, 4);
        QPoint center(int(anchor.x()) + 30, int(anchor.y()) - 30);
        if (center.x() + rect.width() / 2 > widgetW - 10)
            center.setX(int(anchor.x()) - 30 - rect.width() / 2);
        center.setY(qBound(10 + rect.height() / 2, center.y(),
                           widgetH - 10 - rect.height() / 2));
        rect.moveCenter(center);
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(rect);
        p.setPen(ChartStyleManager::getColorTheme().text);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    // ============================================================
    // 7. 绘制多行信息面板（固定在图表右侧区域）
    //    rightX   : 面板右边界的 widget X 坐标
    //    topY     : 期望顶部 Y（自动做边界裁剪）
    //    textColor: 文字颜色
    //    widgetH  : 所属 widget 高度
    // ============================================================
    static void drawInfoPanel(QPainter &p, const QStringList &lines,
                              int rightX, int topY,
                              const QColor &textColor, int widgetH)
    {
        if (lines.isEmpty()) return;
        auto fontTheme = ChartStyleManager::getFontTheme();
        QFont f = fontTheme.tooltip;
        f.setWeight(QFont::Light);
        p.setFont(f);
        QFontMetrics fm(p.font());
        int maxWidth = 0;
        for (const QString &s : lines)
            maxWidth = qMax(maxWidth, fm.horizontalAdvance(s));
        int wBox = maxWidth + 10;
        int hBox = lines.size() * fm.height() + 8;
        int x = rightX - wBox - 10;
        int y = qBound(10, topY, widgetH - hBox - 10);
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(x, y, wBox + 5, hBox);
        int ty = y + fm.ascent() + 4;
        p.setPen(textColor);
        for (const QString &s : lines) {
            p.drawText(x + 5, ty, s);
            ty += fm.height();
        }
    }

    // ============================================================
    // 8a. 绘制折线（均匀索引映射，无视口参数时使用）
    //    values   : 与X轴均匀对应的数据值
    //    minVal/maxVal : Y轴范围
    // ============================================================
    static void drawPolyline(QPainter &p, int left, int top, int w, int h,
                             const QVector<double> &values,
                             double minVal, double maxVal, const QPen &pen)
    {
        int n = values.size();
        if (n < 1) return;
        p.setPen(pen);
        if (n == 1) {
            // 单点：画一个小圆点
            double x = left + w / 2.0;
            double y = top + h - (values[0] - minVal) * h / (maxVal - minVal);
            p.setBrush(pen.color());
            p.drawEllipse(QPointF(x, y), 3, 3);
            p.setBrush(Qt::NoBrush);
            return;
        }
        QPolygonF poly;
        poly.reserve(n);
        for (int i = 0; i < n; ++i) {
            double x = left + i * w / double(n - 1);
            double y = top + h - (values[i] - minVal) * h / (maxVal - minVal);
            poly << QPointF(x, y);
        }
        p.drawPolyline(poly);
    }

    // ============================================================
    // 8b. 绘制折线（时间比例映射，带视口范围）
    //     timestamps : 与 values 一一对应的时间点
    //     vpStart/vpEnd : 视口起止时间（对应像素 left 和 left+w）
    //     允许 guard 点（视口外）向外延伸，确保折线连续；
    //     裁剪到图表区域防止画出边框外
    // ============================================================
    static void drawPolyline(QPainter &p, int left, int top, int w, int h,
                             const QVector<double> &values,
                             const QVector<QDateTime> &timestamps,
                             double minVal, double maxVal,
                             const QDateTime &vpStart, const QDateTime &vpEnd,
                             const QPen &pen)
    {
        int n = values.size();
        if (n < 1 || n != timestamps.size()) return;
        qint64 span = vpStart.msecsTo(vpEnd);
        if (span <= 0) return;
        p.setPen(pen);
        if (n == 1) {
            // 单点：画一个小圆点
            qint64 ms = vpStart.msecsTo(timestamps[0]);
            double x = left + ms * w / double(span);
            double y = top + h - (values[0] - minVal) * h / (maxVal - minVal);
            p.setBrush(pen.color());
            p.drawEllipse(QPointF(x, y), 3, 3);
            p.setBrush(Qt::NoBrush);
            return;
        }
        QPolygonF poly;
        poly.reserve(n);
        for (int i = 0; i < n; ++i) {
            qint64 ms = vpStart.msecsTo(timestamps[i]);
            double x = left + ms * w / double(span);
            double y = top + h - (values[i] - minVal) * h / (maxVal - minVal);
            poly << QPointF(x, y);
        }
        p.save();
        p.setClipRect(left, top, w, h);
        p.setPen(pen);
        p.drawPolyline(poly);
        p.restore();
    }

    // ============================================================
    // 9. 绘制通用标签框（tooltip/labelBox 统一样式）
    //    rect     : 已计算好的框坐标
    // ============================================================
    static void drawLabelBox(QPainter &p, const QRect &rect,
                             const QString &text, const QColor &textColor,
                             const QBrush &bgBrush = QBrush())
    {
        p.setBrush(bgBrush.style() != Qt::NoBrush
                   ? bgBrush
                   : QBrush(ChartStyleManager::getTooltipBackground()));
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(rect);
        p.setPen(textColor);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    // ---- 稀疏步长工具函数 ----
    static int computeSparseStep(int count, int desiredMaxLabels) {
        if (count <= 0 || desiredMaxLabels <= 0) return 1;
        return qMax(1, count / desiredMaxLabels);
    }

protected:
    // -------- 边距（派生类可调整） --------
    int marginLeft   = 60;
    int marginRight  = 60;
    int marginTop    = 30;
    int marginBottom = 40;

    // -------- 内容区域便捷方法 --------
    int   contentWidth()  const { return qMax(0, width()  - marginLeft - marginRight); }
    int   contentHeight() const { return qMax(0, height() - marginTop  - marginBottom); }
    QRect contentRect()   const { return QRect(marginLeft, marginTop, contentWidth(), contentHeight()); }
};

#endif // CHARTBASEWIDGET_H
