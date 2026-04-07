#ifndef DYNAMICCHARTWIDGET_H
#define DYNAMICCHARTWIDGET_H

#include <QWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QDateTime>
#include <QVector>
#include <QString>
#include <QColor>
#include <QList>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include "ChartBaseWidget.h"
#include "ChartStyleManager.h"
#include "ChartViewportMixin.h"

// ============================================================
// DynamicChartWidget
//
// 可动态添加任意数量折线系列的图表。
// 系列通过 addSeries() 添加，包含名称、数值数组和时间戳数组。
// 右侧面板有自动生成的图例（带勾选框），可控制系列可见性。
// 所有绘图通过 ChartWidgetBase 静态 helper 完成。
// ============================================================

// 单个数据系列
struct DynamicSeries {
    QString              name;
    QVector<double>      values;
    QVector<QDateTime>   timestamps;
    QColor               color;
    bool                 visible = true;
};

// ---- 纯绘图区（内嵌 widget）----
class DynamicChartCanvas : public ChartWidgetBase {
    Q_OBJECT
public:
    explicit DynamicChartCanvas(QWidget *parent = nullptr)
        : ChartWidgetBase(parent)
    {
        setMouseTracking(true);
        marginLeft   = 60;
        marginRight  = 20;
        marginTop    = 30;
        marginBottom = 40;
    }

    void setSeries(const QList<DynamicSeries> *series) { m_series = series; }

    // Rebuild viewport from union of all series timestamp ranges.
    // Called by DynamicChartWidget whenever series are added or cleared.
    void rebuildViewport()
    {
        QDateTime tMin, tMax;
        if (m_series) {
            for (const auto &s : *m_series) {
                for (const auto &ts : s.timestamps) {
                    if (!ts.isValid()) continue;
                    if (!tMin.isValid() || ts < tMin) tMin = ts;
                    if (!tMax.isValid() || ts > tMax) tMax = ts;
                }
            }
        }
        if (tMin.isValid() && tMax.isValid())
            m_vp.setRange(tMin, tMax);
        else
            m_vp.clear();
    }

signals:
    void hoverChanged(int seriesIdx, int ptIdx, QPoint widgetPos);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // 背景
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        const int W = width(), H = height();
        const int cW = contentWidth(), cH = contentHeight();

        if (!m_series || m_series->isEmpty() || cW <= 0 || cH <= 0) {
            p.setPen(ChartStyleManager::getColorTheme().text);
            p.setFont(ChartStyleManager::getFontTheme().axis);
            p.drawText(rect(), Qt::AlignCenter, "暂无数据\n使用搜索结果添加系列");
            return;
        }

        // ---- Build sliced series for the visible viewport window ----
        QList<DynamicSeries> sliced = buildSlicedSeries();

        // 计算全局 Y 范围（仅可见系列）
        double yMin = 0, yMax = 1;
        bool first = true;
        for (const auto &s : sliced) {
            if (!s.visible || s.values.isEmpty()) continue;
            for (double v : s.values) {
                if (first) { yMin = yMax = v; first = false; }
                else { yMin = qMin(yMin, v); yMax = qMax(yMax, v); }
            }
        }
        if (yMin == yMax) { yMin -= 1; yMax += 1; }

        // X 轴时间戳（取第一个有效系列的切片）
        const QVector<QDateTime> *xTs = nullptr;
        for (const auto &s : sliced) {
            if (s.visible && !s.timestamps.isEmpty()) { xTs = &s.timestamps; break; }
        }

        // 帧
        drawChartFrame(p, marginLeft, marginTop, cW, cH, "");

        // Y 轴
        drawYAxis(p, marginLeft, marginTop, cH,
                  marginLeft - 55, yMin, yMax, 5, "", 1);

        // X 轴时间刻度
        if (xTs)
            drawXAxisTime(p, marginLeft, marginTop, cW, cH, *xTs, 7);

        // 折线
        for (const auto &s : sliced) {
            if (!s.visible || s.values.size() < 2) continue;
            QPen pen(s.color, 1.5);
            drawPolyline(p, marginLeft, marginTop, cW, cH, s.values, yMin, yMax, pen);
        }

        // 十字准线 + tooltip
        if (m_hoverSeriesIdx >= 0 && m_hoverSeriesIdx < sliced.size()) {
            const auto &hs = sliced[m_hoverSeriesIdx];
            if (m_hoverPtIdx >= 0 && m_hoverPtIdx < hs.values.size()) {
                // 使用时间比例映射 x 坐标，与 drawPolyline(vpStart/vpEnd 版本) 一致
                int hx;
                QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : QDateTime();
                QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : QDateTime();
                const QDateTime &ptTs = hs.timestamps[m_hoverPtIdx];
                if (vpStart.isValid() && vpEnd.isValid() && ptTs.isValid()) {
                    qint64 span = vpStart.msecsTo(vpEnd);
                    qint64 ms   = vpStart.msecsTo(ptTs);
                    hx = marginLeft + (span > 0 ? int(ms * cW / double(span)) : cW / 2);
                } else {
                    int n = hs.values.size();
                    hx = marginLeft + (n > 1 ? int(m_hoverPtIdx * cW / double(n - 1)) : 0);
                }
                hx = qBound(marginLeft, hx, marginLeft + cW);
                double val = hs.values[m_hoverPtIdx];
                int hy = marginTop + cH - int((val - yMin) * cH / (yMax - yMin));
                QString timeStr = (ptTs.isValid()) ? ptTs.toString("HH:mm:ss") : "";
                drawCrosshair(p, marginLeft, marginTop, cW, cH, hx, hy, timeStr);
                QString tip = QString("%1\n%2").arg(hs.name).arg(val, 0, 'f', 2);
                drawTooltipBubble(p, tip, QPointF(hx, hy), W, H);
            }
        }

        // ---- Zoom hint ----
        if (m_vp.isZoomed()) {
            auto ft = ChartStyleManager::getFontTheme();
            QFont smallF = ft.axis;
            smallF.setPointSize(smallF.pointSize() - 1);
            p.setFont(smallF);
            p.setPen(QColor(150, 150, 150));
            p.drawText(marginLeft + cW - 80, marginTop + cH - 2, "滚轮缩放/拖动");
        }
    }

    // ---- Zoom ----
    void wheelEvent(QWheelEvent *e) override
    {
        m_vp.handleWheel(e->angleDelta().y(), e->position().x(), marginLeft, contentWidth());
        m_hoverSeriesIdx = -1;
        m_hoverPtIdx     = -1;
        update();
        e->accept();
    }

    // ---- Pan ----
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_vp.beginDrag(e->pos().x());
            setCursor(Qt::SizeHorCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_vp.dragging && (e->buttons() & Qt::LeftButton)) {
            m_vp.doDrag(e->pos().x(), contentWidth());
            m_hoverSeriesIdx = -1;
            m_hoverPtIdx     = -1;
            update();
            return;
        }
        updateHover(e->pos());
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_vp.endDrag();
            setCursor(Qt::ArrowCursor);
        }
    }

    // ---- Reset ----
    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        m_vp.reset();
        m_hoverSeriesIdx = -1;
        m_hoverPtIdx     = -1;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hoverSeriesIdx = -1;
        m_hoverPtIdx     = -1;
        update();
    }

private:
    // ---- 切片：将每个系列裁剪到可见视口 [vStart, vEnd]，两端各保留一个 guard 点 ----
    // Guard 点确保缩放时折线能延伸到图表边框，而不会在边缘断裂。
    QList<DynamicSeries> buildSlicedSeries() const
    {
        QList<DynamicSeries> sliced;
        if (!m_series) return sliced;
        bool vpValid = m_vp.hasData();
        for (const auto &s : *m_series) {
            if (!s.visible || !vpValid || s.timestamps.isEmpty()) {
                sliced.append(s);
                continue;
            }
            qint64 vS = m_vp.vStart, vE = m_vp.vEnd;
            const int total = qMin(s.values.size(), s.timestamps.size());

            // 找窗口内第一个/最后一个索引
            int first = total, last = -1;
            for (int i = 0; i < total; ++i) {
                qint64 ms = m_vp.tMin.msecsTo(s.timestamps[i]);
                if (ms >= vS && first == total) first = i;
                if (ms <= vE) last = i;
            }
            // 两端各扩展一个 guard 点
            int lo = qMax(0, first - 1);
            int hi = qMin(total - 1, last + 1);

            DynamicSeries sv;
            sv.name    = s.name;
            sv.color   = s.color;
            sv.visible = s.visible;
            if (lo <= hi) {
                sv.values.reserve(hi - lo + 1);
                sv.timestamps.reserve(hi - lo + 1);
                for (int i = lo; i <= hi; ++i) {
                    sv.values.append(s.values[i]);
                    sv.timestamps.append(s.timestamps[i]);
                }
            }
            sliced.append(sv);
        }
        return sliced;
    }

    void updateHover(const QPoint &pos)
    {
        if (!m_series || m_series->isEmpty()) return;
        const int cW = contentWidth();
        if (cW <= 0) return;

        // 复用 buildSlicedSeries() 避免重复切片逻辑
        QList<DynamicSeries> sliced = buildSlicedSeries();

        // 使用时间比例映射计算每个点的像素 x，与 paintEvent 中 drawPolyline 一致
        QDateTime vpStart = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vStart) : QDateTime();
        QDateTime vpEnd   = m_vp.hasData() ? m_vp.tMin.addMSecs(m_vp.vEnd)   : QDateTime();
        qint64 vpSpan = (vpStart.isValid() && vpEnd.isValid()) ? vpStart.msecsTo(vpEnd) : 0;

        int best = -1, bestPt = -1;
        int bestDist = 20;
        for (int si = 0; si < sliced.size(); ++si) {
            const auto &s = sliced[si];
            if (!s.visible || s.values.isEmpty()) continue;
            int n = s.values.size();
            for (int i = 0; i < n; ++i) {
                int px;
                if (vpSpan > 0 && i < s.timestamps.size() && s.timestamps[i].isValid()) {
                    qint64 ms = vpStart.msecsTo(s.timestamps[i]);
                    px = marginLeft + int(ms * cW / double(vpSpan));
                } else {
                    px = marginLeft + (n > 1 ? int(i * cW / double(n - 1)) : 0);
                }
                int d = qAbs(pos.x() - px);
                if (d < bestDist) { bestDist = d; best = si; bestPt = i; }
            }
        }
        m_hoverSeriesIdx = best;
        m_hoverPtIdx     = bestPt;
        update();
    }

    const QList<DynamicSeries> *m_series = nullptr;
    ChartViewport m_vp;
    int m_hoverSeriesIdx = -1;
    int m_hoverPtIdx     = -1;
};

// ---- 主 widget：图表 + 图例面板 ----
class DynamicChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit DynamicChartWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *rootLayout = new QHBoxLayout(this);
        rootLayout->setContentsMargins(0, 0, 0, 0);
        rootLayout->setSpacing(0);

        // 左侧：画布
        m_canvas = new DynamicChartCanvas(this);
        m_canvas->setSeries(&m_series);
        rootLayout->addWidget(m_canvas, 1);

        // 右侧：图例面板（固定宽度，可滚动）
        m_legendArea = new QScrollArea(this);
        m_legendArea->setWidgetResizable(true);
        m_legendArea->setFixedWidth(160);
        m_legendArea->setFrameShape(QFrame::NoFrame);
        m_legendArea->setStyleSheet("background: transparent;");

        m_legendContainer = new QWidget;
        m_legendLayout = new QVBoxLayout(m_legendContainer);
        m_legendLayout->setContentsMargins(6, 6, 6, 6);
        m_legendLayout->setSpacing(4);
        m_legendLayout->addStretch();

        m_legendArea->setWidget(m_legendContainer);
        rootLayout->addWidget(m_legendArea);
    }

    // 添加一个新系列（自动分配颜色）
    void addSeries(const QString &name,
                   const QVector<double> &values,
                   const QVector<QDateTime> &timestamps = {})
    {
        DynamicSeries s;
        s.name       = name;
        s.values     = values;
        s.timestamps = timestamps;
        s.visible    = true;
        s.color      = pickColor(m_series.size());
        m_series.append(s);

        // Rebuild viewport to include the new series' time range
        m_canvas->rebuildViewport();

        // 为这个系列创建图例勾选框
        int idx = m_series.size() - 1;
        auto *row = new QWidget(m_legendContainer);
        auto *rowL = new QHBoxLayout(row);
        rowL->setContentsMargins(0, 0, 0, 0);
        rowL->setSpacing(4);

        // 色块
        auto *colorLabel = new QLabel(row);
        colorLabel->setFixedSize(12, 12);
        colorLabel->setStyleSheet(QString("background:%1; border:1px solid #888;")
                                      .arg(s.color.name()));
        rowL->addWidget(colorLabel);

        // 勾选框
        auto *cb = new QCheckBox(name, row);
        cb->setChecked(true);
        cb->setStyleSheet("font-size:11px;");
        connect(cb, &QCheckBox::toggled, this, [this, idx](bool checked) {
            if (idx < m_series.size()) {
                m_series[idx].visible = checked;
                m_canvas->update();
            }
        });
        rowL->addWidget(cb);
        rowL->addStretch();

        // 插入到 stretch 之前
        m_legendLayout->insertWidget(m_legendLayout->count() - 1, row);

        m_canvas->update();
    }

    // 清空所有系列
    void clearSeries()
    {
        m_series.clear();
        // 清空图例（保留最后一个 stretch）
        while (m_legendLayout->count() > 1) {
            QLayoutItem *item = m_legendLayout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        m_canvas->rebuildViewport();
        m_canvas->update();
    }

    int seriesCount() const { return m_series.size(); }

private:
    static QColor pickColor(int idx)
    {
        static const QColor palette[] = {
            QColor( 54, 162, 235),  // blue
            QColor(255,  99, 132),  // red
            QColor( 75, 192, 192),  // teal
            QColor(255, 159,  64),  // orange
            QColor(153, 102, 255),  // purple
            QColor(255, 205,  86),  // yellow
            QColor(201, 203, 207),  // grey
            QColor( 46, 204, 113),  // green
        };
        constexpr int N = sizeof(palette) / sizeof(palette[0]);
        return palette[idx % N];
    }

    QList<DynamicSeries>  m_series;
    DynamicChartCanvas   *m_canvas         = nullptr;
    QScrollArea          *m_legendArea     = nullptr;
    QWidget              *m_legendContainer = nullptr;
    QVBoxLayout          *m_legendLayout   = nullptr;
};

#endif // DYNAMICCHARTWIDGET_H
