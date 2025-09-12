#ifndef SEARCHRESULTTEXTVIEW_H
#define SEARCHRESULTTEXTVIEW_H

#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextOption>
#include <QVector>
#include <QPair>
#include <QRegExp>
#include <QMouseEvent>

class SearchResultTextView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SearchResultTextView(QWidget *parent = nullptr)
        : QPlainTextEdit(parent) {
        setReadOnly(true);
        setWordWrapMode(QTextOption::NoWrap);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // 柔和的选中配色，尽量还原 QListWidget 的体验
        setStyleSheet(
            // 将选中背景设置为更深的蓝色，避免与当前行淡蓝色(#CCE8FF)混淆
            "QPlainTextEdit{selection-background-color:#80BFFF; selection-color:white;}\n"
            "QPlainTextEdit:hover{background:#FFFFFF;}"
        );
        // 字体与外部控制同步，默认不强制设定固定字号
        selectedRow = -1;
    }

    void setPatterns(const QVector<QPair<QRegExp, QColor>> &patterns) {
        patternList = patterns;
        applyHighlighting();
    }

    void setResultsText(const QStringList &lines) {
        this->clear();
        this->setPlainText(lines.join("\n"));
        selectedRow = -1;
        applyHighlighting();
    }

    void clearResults() {
        this->clear();
        this->setExtraSelections({});
        selectedRow = -1;
    }

signals:
    void rowClicked(int row);
    void rowDoubleClicked(int row);

protected:
    void mousePressEvent(QMouseEvent *event) override {
        QPlainTextEdit::mousePressEvent(event);
        QTextCursor c = cursorForPosition(event->pos());
        selectedRow = c.blockNumber();
        applyHighlighting();
        emit rowClicked(c.blockNumber());
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        // 不触发默认的“选中单词”行为，仅发出双击行号并清除任何选中态
        event->accept();
        QTextCursor c = cursorForPosition(event->pos());
        selectedRow = c.blockNumber();
        // 清除选择，避免蓝色选中单词高亮
        QTextCursor tc = textCursor();
        tc.clearSelection();
        setTextCursor(tc);
        applyHighlighting();
        emit rowDoubleClicked(selectedRow);
    }

private:
    void applyHighlighting() {
        QList<QTextEdit::ExtraSelection> selections;

        // 遍历每一行，按关键字上色
        for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
            const QString lineText = block.text();
            for (const auto &p : patternList) {
                int pos = 0;
                while ((pos = p.first.indexIn(lineText, pos)) != -1) {
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + pos);
                    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, p.first.cap(0).length());
                    QTextCharFormat fmt;
                    fmt.setBackground(p.second);
                    fmt.setForeground(Qt::black);
                    sel.format = fmt;
                    selections.push_back(sel);
                    pos += qMax(1, p.first.cap(0).length());
                }
            }
        }

        // 高亮当前选中的整行（单击时）
        // 注意：将灰底行插到列表最前面，先绘制灰底，再绘制关键词颜色，避免覆盖
        if (selectedRow >= 0) {
            QTextBlock block = document()->findBlockByNumber(selectedRow);
            if (block.isValid()) {
                QTextEdit::ExtraSelection lineSel;
                lineSel.cursor = QTextCursor(block);
                lineSel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
                QTextCharFormat lineFmt;
                lineFmt.setBackground(QColor(200, 200, 200)); // 更深一点的浅灰行高亮
                lineSel.format = lineFmt;
                selections.prepend(lineSel);
            }
        }

        setExtraSelections(selections);
    }

private:
    QVector<QPair<QRegExp, QColor>> patternList;
    int selectedRow;
};

#endif // SEARCHRESULTTEXTVIEW_H


