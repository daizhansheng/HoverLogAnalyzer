#pragma once
#include <QWidget>
#include <QPainter>

// 统一绘图基类：提供边距、内容区域及常用辅助方法
class ChartWidgetBase : public QWidget {
public:
    explicit ChartWidgetBase(QWidget *parent = nullptr)
        : QWidget(parent) {}

protected:
    // 边距（可在派生类中调整）
    int marginLeft   = 60;
    int marginRight  = 60;
    int marginTop    = 30;
    int marginBottom = 40;

    // 内容区域宽高（去除边距）
    int contentWidth()  const { return qMax(0, width()  - marginLeft - marginRight); }
    int contentHeight() const { return qMax(0, height() - marginTop  - marginBottom); }
    QRect contentRect() const { return QRect(marginLeft, marginTop, contentWidth(), contentHeight()); }

    // 根据目标最大标签数计算稀疏步长
    static int computeSparseStep(int count, int desiredMaxLabels) {
        if (count <= 0) return 1;
        if (desiredMaxLabels <= 0) return 1;
        int step = count / desiredMaxLabels;
        if (step <= 0) step = 1;
        return step;
    }

    // 绘制内容边框
    static void drawFrame(QPainter &p, const QRect &r) {
        p.setPen(Qt::black);
        p.drawRect(r);
    }
};


