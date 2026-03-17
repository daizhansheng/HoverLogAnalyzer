#ifndef DOCKTOGGLEBUTTON_H
#define DOCKTOGGLEBUTTON_H
#include <QPushButton>
#include <QPainter>
#include <QDockWidget>
#include <QWidget>
#include <QList>
class DockToggleButton : public QPushButton {
    Q_OBJECT
public:
    explicit DockToggleButton(QDockWidget *dockWidget, QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        connect(this, &QPushButton::clicked, this, &DockToggleButton::toggleDock);
        addDockWidget(dockWidget);

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

    void addDockWidget(QDockWidget *dockWidget) {
        if (!dockWidget || docks.contains(dockWidget)) return;
        docks.append(dockWidget);
        connect(dockWidget, &QDockWidget::visibilityChanged, this, [this](bool){ updateText(); });
        updateText();
    }

    QSize sizeHint() const override {
        return QSize(24, 64);
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
        if (!parent() || docks.isEmpty()) return;
        QWidget *parentWidget = qobject_cast<QWidget*>(parent());
        if (!parentWidget) return;
        move(5, qMax(5, (parentWidget->height() - height()) / 2));
    }

private:
    QList<QDockWidget *> docks;
    QString displayText;

    bool anyDockVisible() const {
        for (QDockWidget *dock : docks) {
            if (dock && dock->isVisible()) return true;
        }
        return false;
    }

    void toggleDock() {
        const bool showDocks = !anyDockVisible();
        for (QDockWidget *dock : docks) {
            if (dock) dock->setVisible(showDocks);
        }
        // 切换后重新定位
        moveToTopLeft();
    }

    void updateText() {
        if (anyDockVisible()) {
            displayText = "<<";
        } else {
            displayText = ">>";
        }
        update(); // 触发重绘
    }
};

#endif // DOCKTOGGLEBUTTON_H
