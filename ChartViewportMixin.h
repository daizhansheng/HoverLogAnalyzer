#ifndef CHARTVIEWPORTMIXIN_H
#define CHARTVIEWPORTMIXIN_H

#include <QDateTime>
#include <QtGlobal>

// ============================================================
// ChartViewport
//
// Reusable zoom / pan / reset state for time-based chart widgets.
//
// Usage inside a QWidget subclass:
//
//   1. Embed:   ChartViewport m_vp;
//
//   2. On data load (setData / addData), call:
//        m_vp.setRange(tMin, tMax);
//
//   3. In paintEvent, clamp your data to [m_vp.vStart, m_vp.vEnd]
//      (ms offsets from tMin):
//        qint64 vS = m_vp.vStart, vE = m_vp.vEnd;
//
//   4. Forward wheel / mouse events:
//        void wheelEvent(QWheelEvent *e) override {
//            m_vp.handleWheel(e->angleDelta().y(), e->position().x(), chartLeft, chartWidth);
//            update(); e->accept();
//        }
//        void mousePressEvent(QMouseEvent *e) override {
//            if (e->button() == Qt::LeftButton) {
//                m_vp.beginDrag(e->pos().x()); setCursor(Qt::SizeHorCursor);
//            }
//        }
//        void mouseMoveEvent(QMouseEvent *e) override {
//            if (m_vp.dragging && (e->buttons() & Qt::LeftButton)) {
//                m_vp.doDrag(e->pos().x(), chartWidth); update();
//            }
//        }
//        void mouseReleaseEvent(QMouseEvent *e) override {
//            if (e->button() == Qt::LeftButton) {
//                m_vp.endDrag(); setCursor(Qt::ArrowCursor);
//            }
//        }
//        void mouseDoubleClickEvent(QMouseEvent *) override {
//            m_vp.reset(); update();
//        }
//
//   5. In paintEvent, convert a data timestamp to pixel x:
//        int x = chartLeft + m_vp.timeToX(ts, chartWidth);
//
//   6. To slice data to visible range, use m_vp.tMin.msecsTo(ts)
//      and compare to [vStart, vEnd].
//
// All margins are handled by the calling widget — this struct
// only deals with time offsets and drag state.
// ============================================================

struct ChartViewport {
    // ---- state ----
    qint64    vStart      = 0;     // ms offset from tMin (visible window start)
    qint64    vEnd        = 0;     // ms offset from tMin (visible window end)
    qint64    minWindowMs = 3000;  // minimum zoom window; caller should set this to ~3x data interval
    QDateTime tMin;
    QDateTime tMax;

    bool dragging  = false;
    int  dragLastX = 0;

    // ---- initialise / reset ----
    void setRange(const QDateTime &mn, const QDateTime &mx)
    {
        tMin   = mn;
        tMax   = mx;
        vStart = 0;
        vEnd   = totalSpanMs();
        if (vEnd == 0) vEnd = 1000;
        dragging = false;
    }

    void reset()
    {
        vStart   = 0;
        vEnd     = totalSpanMs();
        if (vEnd == 0) vEnd = 1000;
        dragging = false;
    }

    void clear()
    {
        tMin = QDateTime(); tMax = QDateTime();
        vStart = 0; vEnd = 0; dragging = false;
    }

    // ---- total data span ----
    qint64 totalSpanMs() const
    {
        if (!tMin.isValid() || !tMax.isValid()) return 0;
        qint64 s = tMin.msecsTo(tMax);
        return (s > 0) ? s : 0;
    }

    bool hasData() const { return tMin.isValid() && tMax.isValid() && vEnd > vStart; }

    // ---- wheel zoom (centred on cursor) ----
    // posX   : mouse x position in widget coords
    // mL     : chart area left margin
    // cW     : chart area pixel width
    void handleWheel(int angleDeltaY, double posX, int mL, int cW)
    {
        qint64 span = totalSpanMs();
        if (span <= 0 || cW <= 0) return;

        double frac = (posX - mL) / double(cW);
        frac = qBound(0.0, frac, 1.0);

        qint64 window = vEnd - vStart;
        if (window <= 0) window = span;

        double factor = (angleDeltaY > 0) ? 0.75 : 1.33;
        qint64 newWindow = qBound(minWindowMs, (qint64)(window * factor), span);

        qint64 pivot    = vStart + (qint64)(frac * window);
        qint64 newStart = pivot  - (qint64)(frac * newWindow);
        qint64 newEnd   = newStart + newWindow;

        // clamp
        if (newStart < 0)    { newEnd   -= newStart; newStart = 0; }
        if (newEnd   > span) { newStart -= (newEnd - span); newEnd = span; }
        newStart = qMax((qint64)0, newStart);
        newEnd   = qMin(span, newEnd);

        vStart = newStart;
        vEnd   = newEnd;
    }

    // ---- drag pan ----
    void beginDrag(int x) { dragging = true; dragLastX = x; }

    // cW : chart area pixel width
    void doDrag(int x, int cW)
    {
        if (!dragging || cW <= 0) return;
        int dx = x - dragLastX;
        dragLastX = x;
        panBy(dx, cW);
    }

    void endDrag() { dragging = false; }

    // ---- coordinate helpers ----
    // Convert a QDateTime → pixel x within chart area (0 = left edge)
    int timeToX(const QDateTime &ts, int cW) const
    {
        if (!tMin.isValid() || !ts.isValid() || vEnd <= vStart) return cW / 2;
        qint64 ms = tMin.msecsTo(ts);
        return int((ms - vStart) * cW / double(vEnd - vStart));
    }

    // Is a given ms offset (from tMin) within the visible window?
    bool isVisible(qint64 ms) const
    {
        return ms >= vStart && ms <= vEnd;
    }

    // ---- zoom hint (show when zoomed in) ----
    bool isZoomed() const
    {
        qint64 span = totalSpanMs();
        return span > 0 && (vEnd - vStart) < span;
    }

private:
    void panBy(int dxPx, int cW)
    {
        qint64 span   = totalSpanMs();
        if (span <= 0) return;
        qint64 window = vEnd - vStart;
        if (window <= 0) return;
        // dxPx > 0 means mouse moved right → user wants to see earlier data → vStart decreases.
        // So dMs must be negative when dxPx is positive: dMs = -dxPx * window / cW.
        qint64 dMs = (qint64)(-dxPx * window / double(cW));
        qint64 ns  = qBound((qint64)0, vStart + dMs, span - window);
        vStart = ns;
        vEnd   = ns + window;
    }
};

#endif // CHARTVIEWPORTMIXIN_H
