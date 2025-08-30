#ifndef SEARCHRESULTHIGHLIGHTER_H
#define SEARCHRESULTHIGHLIGHTER_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QRegExp>
#include <QVector>
#include <QPair>

class SearchResultHighlighter : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit SearchResultHighlighter(const QVector<QPair<QRegExp, QColor>> &patterns,
                               QObject *parent = nullptr)
        : QStyledItemDelegate(parent), patternList(patterns) {}

    void setPatterns(const QVector<QPair<QRegExp, QColor>> &patterns) {
        patternList = patterns;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QString text = index.data(Qt::DisplayRole).toString();

        painter->save();

        // 先绘制整行背景，避免重影
        painter->fillRect(option.rect,
                          (option.state & QStyle::State_Selected) ? option.palette.highlight() : option.palette.base());

        painter->setFont(option.font);
        QFontMetrics fm(option.font);
        QRect rect = option.rect;
        int x = rect.x() + 2; // 左边距
        int y = rect.y() + fm.ascent() + (rect.height() - fm.height()) / 2;

        int lastPos = 0;

        while (lastPos < text.length()) {
            int nearestPos = text.length();
            QColor color;
            int matchLen = 0;

            // 找出当前剩余文本中最先匹配的关键字
            for (auto &p : patternList) {
                int pPos = p.first.indexIn(text, lastPos);
                if (pPos != -1 && pPos < nearestPos) {
                    nearestPos = pPos;
                    color = p.second;
                    matchLen = p.first.cap(0).length();
                }
            }

            // 绘制普通文本
            if (nearestPos > lastPos) {
                QString before = text.mid(lastPos, nearestPos - lastPos);
                painter->drawText(x, y, before);
                x += fm.horizontalAdvance(before);
            }

            // 绘制匹配关键字
            if (nearestPos < text.length()) {
                QString match = text.mid(nearestPos, matchLen);
                QRect matchRect(x, rect.y(), fm.horizontalAdvance(match), rect.height());
                painter->fillRect(matchRect, color);   // 高亮颜色
                painter->drawText(x, y, match);
                x += fm.horizontalAdvance(match);
                lastPos = nearestPos + matchLen;
            } else {
                break;
            }
        }

        painter->restore();
    }

private:
    QVector<QPair<QRegExp, QColor>> patternList;
};

#endif // SEARCHRESULTHIGHLIGHTER_H
