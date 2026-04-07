#ifndef SELECTABLELISTWIDGET_H
#define SELECTABLELISTWIDGET_H

#include <QListWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>

class SelectableListWidget : public QListWidget
{
    Q_OBJECT

public:
    explicit SelectableListWidget(QWidget *parent = nullptr);

    // 获取选中的文本
    QString getSelectedText() const;

    // 清除文本选择
    void clearTextSelection();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    // 文本选择相关
    int selectionStart;
    int selectionEnd;
    int currentItemIndex;
    bool isSelecting;
    QPoint mousePressPos;

    // 获取指定位置对应的字符索引
    int getCharIndexAtPosition(const QPoint &pos, int itemIndex) const;

    // 绘制文本选择
    void drawTextSelection(QPainter *painter, const QRect &itemRect, const QString &text, int itemIndex) const;

    // 更新选择范围
    void updateSelection(const QPoint &pos);

    // 获取选中文本
    QString getTextInRange(int start, int end, int itemIndex) const;
};

#endif // SELECTABLELISTWIDGET_H
