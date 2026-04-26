#ifndef SEARCHRESULTHIGHLIGHTER_H
#define SEARCHRESULTHIGHLIGHTER_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>
#include <QVector>
#include <QPair>
#include <QFontMetrics>
#include <algorithm>

class SearchResultHighlighter : public QStyledItemDelegate {
    Q_OBJECT
public:
    using Pattern = QPair<QRegularExpression, QColor>;

    explicit SearchResultHighlighter(const QVector<Pattern> &patterns,
                               QObject *parent = nullptr)
        : QStyledItemDelegate(parent), patternList(patterns) {}

    void setPatterns(const QVector<Pattern> &patterns) {
        patternList = patterns;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const QString text = index.data(Qt::DisplayRole).toString();

        painter->save();

        // 先绘制整行背景，避免重影
        painter->fillRect(option.rect,
                          (option.state & QStyle::State_Selected) ? option.palette.highlight() : option.palette.base());

        painter->setFont(option.font);
        const QFontMetrics fm(option.font);
        const QRect rect = option.rect;
        int x = rect.x() + 2; // 左边距
        const int y = rect.y() + fm.ascent() + (rect.height() - fm.height()) / 2;

        // 预计算该行所有匹配段（pos, len, color），按 pos 排序后线性绘制，
        // 避免原实现每步都遍历所有 pattern 导致 O(N*M*K) 复杂度。
        struct Seg { int pos; int len; QColor color; };
        QVector<Seg> segs;
        segs.reserve(16);
        for (const auto &p : patternList) {
            if (!p.first.isValid() || p.first.pattern().isEmpty()) continue;
            auto it = p.first.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                const int len = m.capturedLength();
                if (len <= 0) continue;
                segs.push_back({ m.capturedStart(), len, p.second });
            }
        }
        std::sort(segs.begin(), segs.end(), [](const Seg &a, const Seg &b){ return a.pos < b.pos; });

        int lastPos = 0;
        for (const Seg &s : segs) {
            if (s.pos < lastPos) continue; // 忽略与已绘制段重叠的匹配
            if (s.pos > lastPos) {
                const QString before = text.mid(lastPos, s.pos - lastPos);
                painter->drawText(x, y, before);
                x += fm.horizontalAdvance(before);
            }
            const QString match = text.mid(s.pos, s.len);
            const QRect matchRect(x, rect.y(), fm.horizontalAdvance(match), rect.height());
            painter->fillRect(matchRect, s.color);
            painter->drawText(x, y, match);
            x += fm.horizontalAdvance(match);
            lastPos = s.pos + s.len;
        }
        if (lastPos < text.length()) {
            painter->drawText(x, y, text.mid(lastPos));
        }

        painter->restore();
    }

private:
    QVector<Pattern> patternList;
};

#endif // SEARCHRESULTHIGHLIGHTER_H
