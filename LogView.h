#ifndef LOGVIEW_H
#define LOGVIEW_H

#include <QPlainTextEdit>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QColor>

class LogView;

// 行号栏（绘制在 LogView 左侧视口边距内的独立 QWidget）
class LogLineNumberArea : public QWidget
{
public:
    explicit LogLineNumberArea(LogView *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    LogView *m_editor;
};

// 带独立行号栏的日志查看控件
class LogView : public QPlainTextEdit
{
public:
    explicit LogView(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
        m_lineNumberArea = new LogLineNumberArea(this);

        connect(this, &QPlainTextEdit::blockCountChanged,
                this, [this](int){ updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest,
                this, [this](const QRect &r, int dy){ onUpdateRequest(r, dy); });

        updateLineNumberAreaWidth();
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(245, 245, 245));
        // 右侧分隔线
        painter.setPen(QColor(220, 220, 220));
        painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                         m_lineNumberArea->width() - 1, event->rect().bottom());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        painter.setPen(QColor(130, 130, 130));
        painter.setFont(font());
        const int rightPadding = 6;
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);
                painter.drawText(0, top,
                                 m_lineNumberArea->width() - rightPadding,
                                 fontMetrics().height(),
                                 Qt::AlignRight | Qt::AlignVCenter, number);
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) { max /= 10; ++digits; }
        digits = qMax(digits, 4); // 至少预留 4 位，避免频繁抖动
        const int leftPadding = 6;
        const int rightPadding = 8;
        return leftPadding + rightPadding + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void setFont(const QFont &f)
    {
        QPlainTextEdit::setFont(f);
        updateLineNumberAreaWidth();
        if (m_lineNumberArea)
            m_lineNumberArea->update();
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QPlainTextEdit::resizeEvent(e);
        const QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(),
                                            lineNumberAreaWidth(), cr.height()));
    }

private:
    void updateLineNumberAreaWidth()
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
        // 同步调整行号栏几何，避免尚未触发 resize 时宽度不一致
        const QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(),
                                            lineNumberAreaWidth(), cr.height()));
    }

    void onUpdateRequest(const QRect &rect, int dy)
    {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(),
                                     m_lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth();
    }

    QWidget *m_lineNumberArea = nullptr;
};

inline LogLineNumberArea::LogLineNumberArea(LogView *editor)
    : QWidget(editor), m_editor(editor) {}

inline QSize LogLineNumberArea::sizeHint() const
{
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

inline void LogLineNumberArea::paintEvent(QPaintEvent *event)
{
    m_editor->lineNumberAreaPaintEvent(event);
}

#endif // LOGVIEW_H
