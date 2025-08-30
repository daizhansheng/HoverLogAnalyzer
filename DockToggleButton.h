#ifndef DOCKTOGGLEBUTTON_H
#define DOCKTOGGLEBUTTON_H
#include <QPushButton>
#include <QPainter>
#include <QDockWidget>
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
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::black);
        QFont f = font();
        f.setBold(true);
        f.setPointSize(20);
        f.setBold(true);
        p.setFont(f);

        // 水平居中绘制
        QRect r = rect();
        p.drawText(r, Qt::AlignLeft, displayText);
    }

private:
    QDockWidget *dock;
    QString displayText;

    void toggleDock() {
        if (!dock) return;
        dock->setVisible(!dock->isVisible());
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
