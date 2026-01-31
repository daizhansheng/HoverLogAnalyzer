#ifndef CHARTSTYLEMANAGER_H
#define CHARTSTYLEMANAGER_H

#include <QColor>
#include <QFont>
#include <QPen>
#include <QBrush>
#include <QString>

class ChartStyleManager {
public:
    // 统一的颜色主题
    struct ColorTheme {
        QColor primary = QColor(52, 73, 94);      // 深蓝灰
        QColor secondary = QColor(149, 165, 166);  // 浅灰
        QColor accent = QColor(41, 128, 185);       // 蓝色
        QColor success = QColor(39, 174, 96);      // 绿色
        QColor warning = QColor(243, 156, 18);     // 橙色
        QColor danger = QColor(231, 76, 60);       // 红色
        QColor background = QColor(248, 249, 250);  // 浅灰背景
        QColor grid = QColor(236, 240, 241);       // 网格线
        QColor text = QColor(44, 62, 80);          // 文字颜色
        QColor border = QColor(189, 195, 199);     // 边框颜色
    };

    // 统一的字体设置
    struct FontTheme {
        QFont title = QFont("Arial", 12, QFont::Bold);
        QFont axis = QFont("Arial", 9);
        QFont label = QFont("Arial", 8);
        QFont tooltip = QFont("Arial", 9);
    };

    // 统一的布局参数
    struct LayoutTheme {
        int marginLeft = 55;     // 减少左边距15像素，使图表整体向左移动
        int marginRight = 115;   // 增加右边距15像素，减小图表绘制宽度
        int marginTop = 40;
        int marginBottom = 50;
        int chartSpacing = 50;   // 增加图表间距，使三个图表间距更均匀
        int titleHeight = 25;
        int axisWidth = 2;
        int gridWidth = 1;
    };

    // 获取统一的颜色主题
    static ColorTheme getColorTheme() {
        return colorTheme;
    }

    // 获取统一的字体主题
    static FontTheme getFontTheme() {
        return fontTheme;
    }

    // 设置统一的字体主题
    static void setFontTheme(const FontTheme &theme) {
        fontTheme = theme;
    }

    // 获取统一的布局主题
    static LayoutTheme getLayoutTheme() {
        return layoutTheme;
    }

    // 获取图表背景样式
    static QBrush getChartBackground() {
        return QBrush(colorTheme.background);
    }

    // 获取坐标轴画笔
    static QPen getAxisPen() {
        return QPen(colorTheme.primary, layoutTheme.axisWidth);
    }

    // 获取网格线画笔
    static QPen getGridPen() {
        return QPen(colorTheme.grid, layoutTheme.gridWidth, Qt::DashLine);
    }

    // 获取高可见性虚线画笔
    static QPen getHoverPen() {
        return QPen(QColor(64, 64, 64), 1, Qt::DashLine);  // 深灰色，1像素宽度，高对比度
    }

    // 获取边框画笔
    static QPen getBorderPen() {
        return QPen(colorTheme.border, 1);
    }

    // 获取数据线颜色（按索引）
    static QColor getDataColor(int index) {
        static QVector<QColor> colors = {
            colorTheme.accent,      // 蓝色
            colorTheme.success,     // 绿色
            colorTheme.warning,     // 橙色
            colorTheme.danger,      // 红色
            QColor(155, 89, 182),   // 紫色
            QColor(26, 188, 156),   // 青色
            QColor(52, 152, 219),   // 浅蓝
            QColor(241, 196, 15),   // 黄色
            QColor(230, 126, 34),   // 深橙
            QColor(142, 68, 173),   // 深紫
            QColor(46, 204, 113),   // 浅绿
            QColor(52, 73, 94),     // 深灰
            QColor(149, 165, 166),  // 中灰
            QColor(189, 195, 199),  // 浅灰
            QColor(236, 240, 241)   // 最浅灰
        };
        return colors[index % colors.size()];
    }

    // 获取工具提示样式
    static QBrush getTooltipBackground() {
        return QBrush(QColor(255, 255, 255, 240));
    }

    static QPen getTooltipBorder() {
        return QPen(colorTheme.border, 1);
    }

    // 获取状态面板容器样式
    static QString getStatusContainerStyle() {
        return QString(
            "QWidget {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 8px;"
            "  padding: 10px;"
            "}"
        ).arg(colorTheme.background.name(), colorTheme.border.name());
    }

    // 获取标题样式
    static QString getTitleStyle() {
        return QString(
            "QLabel {"
            "  color: %1;"
            "  font-weight: bold;"
            "  font-size: 14px;"
            "  padding: 5px;"
            "  background-color: %2;"
            "  border-radius: 4px;"
            "}"
        ).arg(colorTheme.primary.name(), colorTheme.background.name());
    }

    // 获取事件列表样式
    static QString getEventListStyle() {
        return QString(
            "QListWidget {"
            "  background-color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 4px;"
            "  padding: 5px;"
            "  font-family: 'Arial';"
            "  font-size: 10px;"
            "}"
            "QListWidget::item {"
            "  padding: 2px;"
            "  border-bottom: 1px solid %3;"
            "}"
            "QListWidget::item:selected {"
            "  background-color: %4;"
            "  color: white;"
            "}"
        ).arg(colorTheme.background.name(),
              colorTheme.border.name(),
              QColor(236, 240, 241).name(),
              colorTheme.accent.name());
    }

private:
    static ColorTheme colorTheme;
    static FontTheme fontTheme;
    static LayoutTheme layoutTheme;
};

#endif // CHARTSTYLEMANAGER_H
