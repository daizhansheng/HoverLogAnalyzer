#ifndef FLIGHTANIMATIONWIDGET_H
#define FLIGHTANIMATIONWIDGET_H

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QPainter>
#include <QDateTime>
#include <QRegularExpression>
#include "ChartStyleManager.h"

struct FlightEvent {
    int lineNumber = 0;
    QString display;
};

class FlightAnimationWidget : public QWidget {
    Q_OBJECT
public:
    explicit FlightAnimationWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(420, 260);
        timer.setInterval(30);
        connect(&timer, &QTimer::timeout, this, [this]() {
            if (steps.isEmpty()) return;
            if (!isPlaying) return;
            if (currentStepIndex < 0 || currentStepIndex >= steps.size()) return;
            const Step &s = steps[currentStepIndex];
            stepElapsedMs += timer.interval();
            if (stepElapsedMs >= s.durationMs) {
                currentStepIndex++;
                stepElapsedMs = 0;
                if (currentStepIndex >= steps.size()) {
                    isPlaying = false;
                    currentStepIndex = steps.size() - 1;
                }
            }
            update();
        });
    }

    void clear() {
        steps.clear();
        currentStepIndex = -1;
        stepElapsedMs = 0;
        isPlaying = false;
        notifyLabels.clear();
        update();
    }

    void setEventsForFlight(const QVector<FlightEvent> &events, int flightNumber) {
        clear();

        // Build state sequence from events
        bool hasArm = contains(events, "FC STATE -> ARM");
        bool hasTakeoffState = contains(events, "FC STATE -> TAKINGOFF");
        bool hasFlying = contains(events, "FC STATE -> FLYING");
        bool hasLanding = contains(events, "FC STATE -> LANDING");
        bool hasDisarm = contains(events, "FC STATE -> DISARM");

        int flightDurationSec = parseFlightDuration(events); // may be -1

        // Steps with reasonable default durations
        if (hasArm) addStep("ARM", 800);
        if (hasTakeoffState || contains(events, "starting takeoff")) addStep("TAKINGOFF", 1800);
        if (hasFlying || contains(events, "takeoff success")) {
            int flyMs = 6000;
            if (flightDurationSec > 0) {
                // compress long durations while keeping relative feel
                flyMs = qBound(3000, flightDurationSec * 60, 20000);
            }
            addStep("FLYING", flyMs);
        }
        if (hasLanding) addStep("LANDING", 1800);
        if (hasDisarm) addStep("DISARM", 800);
        if (steps.isEmpty()) {
            // fallback minimal storyboard
            addStep("TAKINGOFF", 1500);
            addStep("FLYING", 5000);
            addStep("LANDING", 1500);
        }

        // Collect notify labels to overlay during FLYING
        for (const auto &e : events) {
            if (e.display.contains("event:", Qt::CaseInsensitive)) {
                notifyLabels.push_back(e.display.mid(e.display.indexOf("event:")));
            }
        }

        currentStepIndex = 0;
        stepElapsedMs = 0;
        isPlaying = true;
        update();
    }

    void play() { if (!steps.isEmpty()) { isPlaying = true; timer.start(); } }
    void pause() { isPlaying = false; }
    void restart() { if (!steps.isEmpty()) { currentStepIndex = 0; stepElapsedMs = 0; isPlaying = true; timer.start(); update(); } }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // background
        p.fillRect(rect(), ChartStyleManager::getChartBackground());

        // title
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.title);
        p.setPen(ChartStyleManager::getColorTheme().primary);
        p.drawText(12, 22, QString::fromUtf8("飞行动画"));

        if (steps.isEmpty()) {
            p.setFont(fontTheme.axis);
            p.setPen(ChartStyleManager::getColorTheme().text);
            p.drawText(rect().adjusted(0, 0, 0, -20), Qt::AlignCenter, QString::fromUtf8("无可播放的飞行步骤"));
            return;
        }

        // ground line
        int margin = 24;
        int groundY = height() - margin;
        p.setPen(ChartStyleManager::getAxisPen());
        p.drawLine(margin, groundY, width() - margin, groundY);

        // compute quadcopter y by current state
        const Step &s = steps[qBound(0, currentStepIndex, steps.size()-1)];
        double t01 = s.durationMs > 0 ? qBound(0.0, stepElapsedMs / double(s.durationMs), 1.0) : 1.0;
        double y;
        if (s.label == "TAKINGOFF") {
            // ease-out takeoff from ground to air
            double h = 120.0;
            double ease = 1.0 - pow(1.0 - t01, 3);
            y = groundY - ease * h;
        } else if (s.label == "FLYING") {
            // cruise at mid air with gentle bobbing
            double base = groundY - 120;
            y = base + 6.0 * sin(totalPhase());
        } else if (s.label == "LANDING") {
            // ease-in landing from air to ground
            double h = 120.0;
            double ease = pow(t01, 3);
            y = groundY - (1.0 - ease) * h;
        } else {
            // ARM/DISARM
            y = (s.label == "ARM") ? groundY - 10 : groundY - 0;
        }

        // draw quadcopter
        drawQuadcopter(p, QPointF(width()/2.0, y), s.label);

        // draw current label box
        drawLabelBox(p, s.label);

        // draw timeline steps at top-right
        drawTimeline(p);
    }

private:
    struct Step { QString label; int durationMs; };

    QTimer timer;
    QVector<Step> steps;
    int currentStepIndex = -1;
    int stepElapsedMs = 0;
    bool isPlaying = false;
    QVector<QString> notifyLabels;

    void addStep(const QString &label, int durationMs) { steps.push_back({label, durationMs}); }

    static bool contains(const QVector<FlightEvent> &events, const char *needle) {
        for (const auto &e : events) {
            if (e.display.contains(needle, Qt::CaseInsensitive)) return true;
        }
        return false;
    }

    static int parseFlightDuration(const QVector<FlightEvent> &events) {
        QRegularExpression re("Flight Duration:\\s*(\\d+)\\s*seconds", QRegularExpression::CaseInsensitiveOption);
        for (const auto &e : events) {
            auto m = re.match(e.display);
            if (m.hasMatch()) return m.captured(1).toInt();
        }
        return -1;
    }

    double totalPhase() const {
        int totalMs = 0;
        for (int i = 0; i < currentStepIndex; ++i) totalMs += steps[i].durationMs;
        totalMs += stepElapsedMs;
        return (totalMs % 2000) * (2 * 3.1415926 / 2000.0);
    }

    void drawQuadcopter(QPainter &p, const QPointF &center, const QString &state) {
        auto colors = ChartStyleManager::getColorTheme();
        QColor body = colors.accent;
        if (state == "ARM" || state == "DISARM") body = QColor(120,120,120);
        if (state == "LANDING") body = colors.warning;
        if (state == "TAKINGOFF") body = colors.primary;

        p.setPen(Qt::NoPen);
        p.setBrush(body);
        double r = 14.0;
        p.drawEllipse(center, r, r);

        // arms
        p.setPen(QPen(body.lighter(110), 3));
        p.drawLine(center + QPointF(-r, 0), center + QPointF(-r-24, 0));
        p.drawLine(center + QPointF( r, 0), center + QPointF( r+24, 0));
        p.drawLine(center + QPointF(0, -r), center + QPointF(0, -r-24));
        p.drawLine(center + QPointF(0,  r), center + QPointF(0,  r+24));

        // props with spinning
        double phase = totalPhase();
        drawProp(p, center + QPointF(-r-28, 0), phase);
        drawProp(p, center + QPointF( r+28, 0), -phase);
        drawProp(p, center + QPointF(0, -r-28), phase*1.2);
        drawProp(p, center + QPointF(0,  r+28), -phase*1.1);
    }

    static void drawProp(QPainter &p, const QPointF &c, double phase) {
        p.save();
        p.translate(c);
        p.rotate(phase * 180.0 / 3.1415926);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(90,90,90), 2));
        p.drawEllipse(QPointF(0,0), 10, 10);
        p.drawLine(QPointF(-12, 0), QPointF(12, 0));
        p.drawLine(QPointF(0, -12), QPointF(0, 12));
        p.restore();
    }

    void drawLabelBox(QPainter &p, const QString &label) {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.tooltip);
        QString text = label;
        if (!notifyLabels.isEmpty() && label == "FLYING") {
            text += "  ·  " + notifyLabels.first();
        }
        QFontMetrics fm(p.font());
        QRect rect = fm.boundingRect(text).adjusted(-8, -6, 8, 6);
        rect.moveTo(16, 36);
        p.setBrush(ChartStyleManager::getTooltipBackground());
        p.setPen(ChartStyleManager::getTooltipBorder());
        p.drawRect(rect);
        p.setPen(ChartStyleManager::getColorTheme().text);
        p.drawText(rect, Qt::AlignCenter, text);
    }

    void drawTimeline(QPainter &p) {
        auto fontTheme = ChartStyleManager::getFontTheme();
        p.setFont(fontTheme.axis);
        int x = width() - 12;
        int y = 36;
        for (int i = steps.size() - 1; i >= 0; --i) {
            QString s = steps[i].label;
            QFontMetrics fm(p.font());
            int w = fm.horizontalAdvance(s) + 12;
            QRect r(x - w, y, w, 20);
            p.setBrush(i == currentStepIndex ? ChartStyleManager::getColorTheme().primary.lighter(180)
                                             : ChartStyleManager::getChartBackground());
            p.setPen(ChartStyleManager::getAxisPen());
            p.drawRect(r);
            p.setPen(ChartStyleManager::getColorTheme().text);
            p.drawText(r, Qt::AlignCenter, s);
            y += 24;
        }
    }
};

#endif // FLIGHTANIMATIONWIDGET_H


