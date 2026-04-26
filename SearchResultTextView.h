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
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QMap>
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

    using Pattern = QPair<QRegularExpression, QColor>;

    void setPatterns(const QVector<Pattern> &patterns) {
        patternList = patterns;
        applyHighlighting();
    }

    const QVector<Pattern> &patterns() const {
        return patternList;
    }

    // 颜色标记：keyword->color，独立于正则搜索高亮，深色背景+白色前景
    void setColorMarkPatterns(const QMap<QString, QColor> &marks) {
        colorMarkList = marks;
        applyHighlighting();
    }

    void setResultsText(const QStringList &lines) {
        QFont savedFont = this->font();  // 保存当前字体
        this->clear();
        this->setPlainText(lines.join("\n"));
        // clear()/setPlainText() 可能重置文档默认字体，需要重新应用
        this->setFont(savedFont);
        this->document()->setDefaultFont(savedFont);
        selectedRow = -1;
        applyHighlighting();
    }

    void clearResults() {
        QFont savedFont = this->font();  // 保存当前字体
        this->clear();
        // clear() 可能重置文档默认字体，需要重新应用
        this->setFont(savedFont);
        this->document()->setDefaultFont(savedFont);
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
            // 1. 正则搜索高亮（浅色背景+黑色前景）
            for (const auto &p : patternList) {
                if (!p.first.isValid() || p.first.pattern().isEmpty()) continue;
                auto it = p.first.globalMatch(lineText);
                while (it.hasNext()) {
                    const auto m = it.next();
                    const int len = m.capturedLength();
                    if (len <= 0) continue;
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + m.capturedStart());
                    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, len);
                    QTextCharFormat fmt;
                    fmt.setBackground(p.second);
                    fmt.setForeground(Qt::black);
                    sel.format = fmt;
                    selections.push_back(sel);
                }
            }
            // 2. 颜色标记高亮（深色背景+白色前景，覆盖在搜索高亮之上）
            for (auto it = colorMarkList.constBegin(); it != colorMarkList.constEnd(); ++it) {
                const QString &kw = it.key();
                const QColor &color = it.value();
                if (kw.isEmpty()) continue;
                int pos = 0;
                while ((pos = lineText.indexOf(kw, pos, Qt::CaseSensitive)) != -1) {
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + pos);
                    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, kw.length());
                    QTextCharFormat fmt;
                    fmt.setBackground(color);
                    fmt.setForeground(Qt::white);
                    sel.format = fmt;
                    selections.push_back(sel);
                    pos += kw.length();
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
    QVector<Pattern> patternList;
    QMap<QString, QColor> colorMarkList;
    int selectedRow;
};

#endif // SEARCHRESULTTEXTVIEW_H


