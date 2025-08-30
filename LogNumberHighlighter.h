#ifndef LOGNUMBERHIGHLIGHTER_H
#define LOGNUMBERHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QTextDocument>

// 将每行跳过固定宽度前缀后的数字高亮显示
class LogNumberHighlighter : public QSyntaxHighlighter
{
public:
    explicit LogNumberHighlighter(QTextDocument *parent, int prefixSkipColumns = 7)
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

#endif // LOGNUMBERHIGHLIGHTER_H


