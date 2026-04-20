// PressAnalyzerHighlight.cpp - Color marking and highlight functions
#include "PressAnalyzer.h"

#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QColor>
#include <QTextBlock>
#include <QScrollBar>
#include <algorithm>

void PressAnalyzer::applyLineColorMark(int colorIndex, const QString &keyword)
{
    if (colorIndex < 0 || colorIndex >= 5) return;

    QString kw = keyword;
    if (kw.isEmpty()) {
        // 未传入关键字时，从 logView 取选中文本
        QTextCursor cursor = logView->textCursor();
        if (cursor.hasSelection())
            kw = cursor.selectedText().trimmed();
    }
    if (kw.isEmpty()) return;

    m_colorMarks[kw] = colorIndex;
    m_colorMarkRebuildTimer->start(80);
}

void PressAnalyzer::clearColorMarkByText(const QString &text)
{
    m_colorMarks.remove(text);
    m_colorMarkRebuildTimer->start(80);
}

void PressAnalyzer::clearLineColorMark(int /*lineNumber*/)
{
    // 保留接口兼容，实际已无整行标记逻辑
}

void PressAnalyzer::clearAllColorMarks()
{
    m_colorMarks.clear();
    m_colorMarkHitData.clear();
    updateVisibleHighlights();
    searchResultView->setColorMarkPatterns({});
}

void PressAnalyzer::rebuildColorMarkSelections()
{
    m_colorMarkHitData.clear();
    m_searchViewColorHighlights.clear();
    if (m_colorMarks.isEmpty()) {
        updateVisibleHighlights();
        return;
    }

    // 逐 block 提取文本，避免 toPlainText() 对大文档的全量深拷贝
    QTextDocument *doc = logView->document();
    struct BlockText { int blockNumber; QString text; };
    QVector<BlockText> blocks;
    blocks.reserve(doc->blockCount());
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        blocks.append({b.blockNumber(), b.text()});
    }

    QMap<QString, int> marks = m_colorMarks;

    // 后台线程：在每个 block 的文本中搜索关键词，直接产生 (blockNumber, posInBlock, len, colorIdx)
    QFuture<QVector<ColorMarkHit>> future = QtConcurrent::run(
        [blocks, marks]() -> QVector<ColorMarkHit> {
            QVector<ColorMarkHit> hits;
            for (const auto &bt : blocks) {
                if (bt.text.isEmpty()) continue;
                for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
                    const QString &kw = it.key();
                    int colorIdx = it.value();
                    if (kw.isEmpty()) continue;
                    int pos = 0;
                    while ((pos = bt.text.indexOf(kw, pos, Qt::CaseSensitive)) != -1) {
                        ColorMarkHit h;
                        h.blockNumber = bt.blockNumber;
                        h.posInBlock  = pos;
                        h.length      = kw.length();
                        h.colorIndex  = colorIdx;
                        hits.append(h);
                        pos += kw.length();
                    }
                }
            }
            // 按 blockNumber 排序，便于后续二分查找可见范围
            std::sort(hits.begin(), hits.end(),
                [](const ColorMarkHit &a, const ColorMarkHit &b) {
                    return a.blockNumber < b.blockNumber
                        || (a.blockNumber == b.blockNumber && a.posInBlock < b.posInBlock);
                });
            return hits;
        }
    );

    auto *watcher = new QFutureWatcher<QVector<ColorMarkHit>>(this);
    connect(watcher, &QFutureWatcher<QVector<ColorMarkHit>>::finished, this,
        [this, watcher](){
            m_colorMarkHitData = watcher->result();
            watcher->deleteLater();

            updateVisibleHighlights();
            // 同步颜色标记到查找结果窗口
            QMap<QString, QColor> markColors;
            for (auto it = m_colorMarks.constBegin(); it != m_colorMarks.constEnd(); ++it)
                markColors[it.key()] = s_markColors[it.value()];
            searchResultView->setColorMarkPatterns(markColors);
        }
    );
    watcher->setFuture(future);
}

void PressAnalyzer::updateVisibleHighlights()
{
    // 仅重绘可见区域的黄色关键字与当前行灰底
    QList<QTextEdit::ExtraSelection> selections;
    QTextDocument* doc = logView->document();
    // 使用坐标换算可见块范围，避免调用受保护的 firstVisibleBlock()
    int firstVisibleBlock = logView->cursorForPosition(QPoint(0, 0)).block().blockNumber();
    int lastVisibleBlock  = logView->cursorForPosition(QPoint(0, logView->viewport()->height() - 1)).block().blockNumber();
    if (lastVisibleBlock < firstVisibleBlock) lastVisibleBlock = firstVisibleBlock;

    // 颜色标记高亮（仅可见范围 — 虚拟化，替代旧的全量列表）
    if (!m_colorMarkHitData.isEmpty()) {
        // 二分查找可见范围内的命中数据
        auto lower = std::lower_bound(m_colorMarkHitData.constBegin(), m_colorMarkHitData.constEnd(),
            firstVisibleBlock, [](const ColorMarkHit &h, int bn) { return h.blockNumber < bn; });
        auto upper = std::upper_bound(lower, m_colorMarkHitData.constEnd(),
            lastVisibleBlock, [](int bn, const ColorMarkHit &h) { return bn < h.blockNumber; });

        for (auto it = lower; it != upper; ++it) {
            QTextBlock block = doc->findBlockByNumber(it->blockNumber);
            if (!block.isValid()) continue;
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(block);
            sel.cursor.setPosition(block.position() + it->posInBlock);
            sel.cursor.setPosition(block.position() + it->posInBlock + it->length, QTextCursor::KeepAnchor);
            QTextCharFormat fmt;
            fmt.setBackground(s_markColors[it->colorIndex]);
            fmt.setForeground(Qt::white);
            sel.format = fmt;
            selections.push_back(sel);
        }
    }

    // 当前行灰底
    if (currentSearchIndex >= 0 && currentSearchIndex < searchResults.size()) {
        QTextBlock cur = doc->findBlockByNumber(searchResults[currentSearchIndex]);
        if (cur.isValid()) {
            QTextEdit::ExtraSelection lineSel;
            lineSel.cursor = QTextCursor(cur);
            lineSel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat lineFmt; lineFmt.setBackground(QColor(180,180,180,140));
            lineSel.format = lineFmt;
            selections.push_back(lineSel);
        }
    }
    // 关键字高亮（仅可见范围），颜色与搜索结果区域一致
    QStringList keys = searchEdit->text().trimmed().split('|', Qt::SkipEmptyParts);
    for (int i = 0; i < keys.size(); ++i) keys[i] = keys[i].trimmed();

    // 颜色池与搜索结果区域保持一致：第1个黄色、第2个绿色、其余按池循环
    QVector<QColor> colorPool = {
        QColor(255, 182, 193), // light pink
        QColor(173, 216, 230), // light blue
        QColor(144, 238, 144), // light green
        QColor(255, 255, 150), // light yellow
        QColor(255, 160, 122), // light salmon
        QColor(255, 228, 181), // moccasin
        QColor(221, 160, 221), // plum
        QColor(176, 224, 230), // powder blue
        QColor(152, 251, 152), // pale green
        QColor(240, 230, 140)  // khaki
    };
    QVector<QColor> colors;
    colors.reserve(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        if (i == 0) colors.append(Qt::yellow);
        else if (i == 1) colors.append(Qt::green);
        else colors.append(colorPool[(i - 2) % colorPool.size()]);
    }

    for (int ln = firstVisibleBlock; ln <= lastVisibleBlock; ++ln) {
        QTextBlock block = doc->findBlockByNumber(ln);
        if (!block.isValid()) break;
        QString text = block.text();
        QString hay = text.toLower();
        for (int kIdx = 0; kIdx < keys.size(); ++kIdx) {
            const QString &k = keys[kIdx];
            if (k.isEmpty()) continue;
            QString ndl = k.toLower();
            int pos = 0;
            while ((pos = hay.indexOf(ndl, pos)) != -1) {
                QTextEdit::ExtraSelection sel;
                sel.cursor = QTextCursor(block);
                sel.cursor.setPosition(block.position() + pos);
                sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, ndl.length());
                QTextCharFormat fmt; fmt.setBackground(colors.value(kIdx, Qt::yellow)); fmt.setForeground(Qt::black);
                sel.format = fmt;
                selections.push_back(sel);
                pos += ndl.length();
            }
        }
    }
    // 所有高亮均限于可见区域，直接设置
    logView->setExtraSelections(selections);
}

void PressAnalyzer::highlightLine(int lineNumber, const QString &eventType)
{
    int blockNumber = lineNumber - 1;
    QTextBlock block = logView->document()->findBlockByNumber(blockNumber);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    QColor color = Qt::yellow; // 默认黄色

    if (eventType.contains("press power key", Qt::CaseInsensitive)) {
        color = QColor(186, 85, 211); // 紫色 - 电源键事件
    }  else if (eventType.contains("starting takeoff", Qt::CaseInsensitive)) {
        color = QColor(60, 179, 113); // 中海绿色，表示成功"pre-flight check success"
    }else if (eventType.contains("Flying", Qt::CaseInsensitive)) {
        color = QColor(100, 149, 237); // 蓝色 - 飞行中
    } else if (eventType.contains("FC STATE", Qt::CaseInsensitive)) {
        color = QColor(173, 216, 230); // 粉蓝色 - 飞控状态改变
    }else if (eventType.contains("Landing", Qt::CaseInsensitive)) {
        color = QColor(50, 205, 50); // 绿色 - 降落中
    } else if (eventType.contains("Media UUID", Qt::CaseInsensitive)) {
        color = QColor(255, 165, 0); // 橙色 - 媒体相关事件
    } else if (eventType.contains("event:", Qt::CaseInsensitive) ||
               eventType.contains("errors:", Qt::CaseInsensitive)) {
        color = QColor(255, 69, 0); // 红色 - 错误或特殊事件
    } else if (eventType.contains("close", Qt::CaseInsensitive)) {
        color = QColor(169, 169, 169); // 深灰色 - Camera关闭
    } else if (eventType.contains("[init]", Qt::CaseInsensitive)) {
        color = QColor(211, 211, 211); // 浅灰色 - Camera初始化状态
    } else if (eventType.contains("[recording]", Qt::CaseInsensitive)) {
        color = Qt::red; // 红色 - 正在录像
    } else if (eventType.contains("[stream+preview+recording]", Qt::CaseInsensitive)) {
        color = QColor(255, 99, 71); // 浅红色 - 流+预览+录像同时开启
    } else if (eventType.contains("[stream+preview]", Qt::CaseInsensitive)) {
        color = QColor(255, 165, 0); // 橙色 - 流+预览同时开启
    } else if (eventType.contains("[stream]", Qt::CaseInsensitive)) {
        color = Qt::green; // 绿色 - 流开启
    } else if (eventType.contains("[preview]", Qt::CaseInsensitive)) {
        color = Qt::yellow; // 黄色 - 预览开启
    } else if (eventType.contains("[snapshot]", Qt::CaseInsensitive)) {
        color = Qt::cyan; // 青色 - 快照状态
    } else {
        color = QColor(255, 255, 0); // 默认黄色 - 未分类事件
    }

    QTextCharFormat fmt;
    fmt.setBackground(color);

    // 选择整个文本块，包括换行部分
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.setCharFormat(fmt);
}
// 遍历所有事件，给 viewLog 对应行上色
void PressAnalyzer::highlightAllEvents()
{
    // 合并 allEvents 和 cameraEvents 并按行号排序
    QList<EventItem> mergedEvents = allEvents;
    mergedEvents += cameraEvents;   // 合并列表
    mergedEvents += statusEvents; // 合并列表
    // 按 lineNumber 升序排序
    std::sort(mergedEvents.begin(), mergedEvents.end(),
              [](const EventItem &a, const EventItem &b) {
                  return a.lineNumber < b.lineNumber;
              });

    // 使用 beginEditBlock/endEditBlock 将所有格式修改合并为一次文档更新，
    // 避免每次 setCharFormat 都触发重排（N 次 → 1 次）
    QTextCursor batchCursor(logView->document());
    batchCursor.beginEditBlock();

    // 遍历统一高亮
    for (const auto &event : mergedEvents) {
        highlightLine(event.lineNumber, event.display);
    }

    batchCursor.endEditBlock();
}
