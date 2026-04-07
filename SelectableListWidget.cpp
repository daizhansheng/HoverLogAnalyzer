#include "SelectableListWidget.h"
#include <QFontMetrics>

SelectableListWidget::SelectableListWidget(QWidget *parent)
    : QListWidget(parent)
    , selectionStart(-1)
    , selectionEnd(-1)
    , currentItemIndex(-1)
    , isSelecting(false)
{
    // 设置选择模式
    setSelectionMode(QAbstractItemView::SingleSelection);

    // 启用鼠标跟踪
    setMouseTracking(true);
}

QString SelectableListWidget::getSelectedText() const
{
    if (selectionStart == -1 || selectionEnd == -1 || currentItemIndex == -1) {
        return QString();
    }

    int start = qMin(selectionStart, selectionEnd);
    int end = qMax(selectionStart, selectionEnd);

    if (currentItemIndex >= 0 && currentItemIndex < count()) {
        QString itemText = item(currentItemIndex)->text();
        return itemText.mid(start, end - start);
    }

    return QString();
}

void SelectableListWidget::clearTextSelection()
{
    selectionStart = -1;
    selectionEnd = -1;
    currentItemIndex = -1;
    isSelecting = false;
    viewport()->update();
}

void SelectableListWidget::paintEvent(QPaintEvent *event)
{
    QListWidget::paintEvent(event);

    // 如果有文本选择，绘制选择背景
    if (selectionStart != -1 && selectionEnd != -1 && currentItemIndex != -1) {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);

        QListWidgetItem *item = this->item(currentItemIndex);
        if (item) {
            QRect itemRect = visualItemRect(item);
            QString text = item->text();

            drawTextSelection(&painter, itemRect, text, currentItemIndex);
        }
    }
}

void SelectableListWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QListWidgetItem *item = itemAt(event->pos());
        if (item) {
            currentItemIndex = row(item);
            int charIndex = getCharIndexAtPosition(event->pos(), currentItemIndex);

            selectionStart = charIndex;
            selectionEnd = charIndex;
            isSelecting = true;
            mousePressPos = event->pos();

            // 不调用父类的 mousePressEvent，避免改变 item 选择
            viewport()->update();
            return;
        } else {
            // 点击空白区域，清除选择
            clearTextSelection();
        }
    }

    QListWidget::mousePressEvent(event);
}

void SelectableListWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (isSelecting && (event->buttons() & Qt::LeftButton)) {
        updateSelection(event->pos());
        viewport()->update();
        return;
    }

    QListWidget::mouseMoveEvent(event);
}

void SelectableListWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isSelecting) {
        isSelecting = false;
        viewport()->update();
        return;
    }

    QListWidget::mouseReleaseEvent(event);
}

void SelectableListWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QListWidgetItem *item = itemAt(event->pos());
        if (item) {
            currentItemIndex = row(item);
            QString text = item->text();

            // 双击选择整行
            selectionStart = 0;
            selectionEnd = text.length();
            isSelecting = false;

            viewport()->update();
            return;
        }
    }

    QListWidget::mouseDoubleClickEvent(event);
}

void SelectableListWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) {
        QString selectedText = getSelectedText();
        if (!selectedText.isEmpty()) {
            QApplication::clipboard()->setText(selectedText);
            return;
        }
    }

    QListWidget::keyPressEvent(event);
}

void SelectableListWidget::focusOutEvent(QFocusEvent *event)
{
    // 失去焦点时清除文本选择
    clearTextSelection();
    QListWidget::focusOutEvent(event);
}

int SelectableListWidget::getCharIndexAtPosition(const QPoint &pos, int itemIndex) const
{
    if (itemIndex < 0 || itemIndex >= count()) {
        return -1;
    }

    QListWidgetItem *item = this->item(itemIndex);
    if (!item) {
        return -1;
    }

    QRect itemRect = visualItemRect(item);
    QString text = item->text();
    QFontMetrics fm(font());

    // 计算相对于文本起始位置的偏移
    int x = pos.x() - itemRect.x() - 2; // 减去左边距

    // 找到最接近的字符位置
    int charIndex = 0;
    int currentX = 0;

    for (int i = 0; i < text.length(); ++i) {
        int charWidth = fm.horizontalAdvance(text.at(i));
        if (x < currentX + charWidth / 2) {
            charIndex = i;
            break;
        }
        currentX += charWidth;
        charIndex = i + 1;
    }

    return qBound(0, charIndex, text.length());
}

void SelectableListWidget::drawTextSelection(QPainter *painter, const QRect &itemRect, const QString &text, int itemIndex) const
{
    if (selectionStart == -1 || selectionEnd == -1) {
        return;
    }

    int start = qMin(selectionStart, selectionEnd);
    int end = qMax(selectionStart, selectionEnd);

    if (start == end) {
        return; // 没有选择
    }

    QFontMetrics fm(font());

    // 计算选择区域的起始和结束位置
    QString beforeSelection = text.left(start);
    QString selectedText = text.mid(start, end - start);

    int startX = itemRect.x() + 2 + fm.horizontalAdvance(beforeSelection);
    int endX = startX + fm.horizontalAdvance(selectedText);

    QRect selectionRect(startX, itemRect.y(), endX - startX, itemRect.height());

    // 绘制选择背景（使用半透明蓝色）
    QColor selectionColor = palette().highlight().color();
    selectionColor.setAlpha(128); // 半透明
    painter->fillRect(selectionRect, selectionColor);

    // 绘制选中文本（使用深色文字以确保可读性）
    painter->setPen(Qt::black);
    painter->drawText(selectionRect, Qt::AlignLeft | Qt::AlignVCenter, selectedText);
}

void SelectableListWidget::updateSelection(const QPoint &pos)
{
    if (currentItemIndex < 0 || currentItemIndex >= count()) {
        return;
    }

    int charIndex = getCharIndexAtPosition(pos, currentItemIndex);
    if (charIndex != -1) {
        selectionEnd = charIndex;
    }
}

QString SelectableListWidget::getTextInRange(int start, int end, int itemIndex) const
{
    if (itemIndex < 0 || itemIndex >= count()) {
        return QString();
    }

    QListWidgetItem *item = this->item(itemIndex);
    if (!item) {
        return QString();
    }

    QString text = item->text();
    int actualStart = qBound(0, start, text.length());
    int actualEnd = qBound(0, end, text.length());

    if (actualStart >= actualEnd) {
        return QString();
    }

    return text.mid(actualStart, actualEnd - actualStart);
}
