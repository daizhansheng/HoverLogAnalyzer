#ifndef NUMBERHIGHLIGHTER_H
#define NUMBERHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QTextDocument>

// 将每行跳过固定宽度前缀后的数字高亮显示
class NumberHighlighter : public QSyntaxHighlighter
{
public:
    explicit NumberHighlighter(QTextDocument *parent, int prefixSkipColumns = 7)
        : QSyntaxHighlighter(parent), skip(prefixSkipColumns), re(QStringLiteral("\\d+"))
    {
        // 亮蓝色 (90, 90, 255)
        fmt.setForeground(QColor(90, 90, 255));
    }

protected:
    void highlightBlock(const QString &text) override
    {
        if (text.length() <= skip) return;
        const QStringView sv(text);
        auto it = re.globalMatch(sv.mid(skip).toString());
        while (it.hasNext()) {
            auto m = it.next();
            setFormat(skip + m.capturedStart(), m.capturedLength(), fmt);
        }
    }

private:
    int skip;
    QRegularExpression re;
    QTextCharFormat fmt;
};

#endif // NUMBERHIGHLIGHTER_H


