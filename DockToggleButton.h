#ifndef DOCKTOGGLEBUTTON_H
#define DOCKTOGGLEBUTTON_H
#include <QPushButton>
#include <QPainter>
#include <QDockWidget>
#include <QWidget>
class DockToggleButton : public QPushButton {
    Q_OBJECT
public:
    explicit DockToggleButton(QDockWidget *dockWidget, QWidget *parent = nullptr)
        : QPushButton(parent), dock(dockWidget)
    {
        connect(this, &QPushButton::clicked, this, &DockToggleButton::toggleDock);

        // 跟踪 dock 的显示状态变化
        connect(dock, &QDockWidget::visibilityChanged, this, &DockToggleButton::updateText);
        updateText(dock->isVisible());

        // 设置按钮样式
        setFlat(true);
        setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(240, 240, 240, 200);"
            "  border: 1px solid #CCCCCC;"
            "  border-radius: 4px;"
            "  padding: 2px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(220, 220, 220, 200);"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(200, 200, 200, 200);"
            "}"
        );
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::black);
        QFont f = font();
        f.setBold(true);
        f.setPointSize(14); // 调整字体大小
        p.setFont(f);

        // 居中绘制
        QRect r = rect();
        p.drawText(r, Qt::AlignCenter, displayText);
    }

    // 确保按钮在窗口大小变化时保持在左上角
    void moveToTopLeft() {
        if (!parent() || !dock) return;
        // 移动到父窗口的左上角
        move(5, 5);
    }

private:
    QDockWidget *dock;
    QString displayText;

    void toggleDock() {
        if (!dock) return;
        dock->setVisible(!dock->isVisible());
        // 切换后重新定位
        moveToTopLeft();
    }

    void updateText(bool visible) {
        if (visible) {
            displayText = "<<";
        } else {
            displayText = ">>";
        }
        update(); // 触发重绘
    }
};

#endif // DOCKTOGGLEBUTTON_H
