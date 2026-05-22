// PressAnalyzerSearch.cpp - Search functionality
#include "PressAnalyzer.h"

#include <QTextCursor>
#include <QTextBlock>
#include <QCompleter>
#include <QStringListModel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QSettings>
#include <QMenu>
#include <QRegularExpression>
#include <QScrollBar>
#include <QMessageBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QApplication>
#include <QStatusBar>
#include <QScopeGuard>

// 按 '|' 分割，但忽略位于方括号 [] 内部的 '|'（例如 "[I|Captain]" 视为一个整体）
static QStringList splitByPipeOutsideBrackets(const QString &text)
{
    QStringList parts;
    QString current;
    int bracketDepth = 0;
    for (int i = 0; i < text.size(); ++i) {
        QChar ch = text[i];
        if (ch == '[') {
            bracketDepth++;
            current.append(ch);
            continue;
        }
        if (ch == ']') {
            if (bracketDepth > 0) bracketDepth--;
            current.append(ch);
            continue;
        }
        if (ch == '|' && bracketDepth == 0) {
            QString trimmed = current.trimmed();
            if (!trimmed.isEmpty()) parts.append(trimmed);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    QString trimmed = current.trimmed();
    if (!trimmed.isEmpty()) parts.append(trimmed);
    return parts;
}

// 共享的颜色池见 SearchColorPalette.h —— 此文件改为薄包装，与 highlight 模块同源
#include "SearchColorPalette.h"

static inline QColor searchColorForIndex(int idx) { return SearchPalette::colorForIndex(idx); }

// 多关键字归一化空格用的正则，全局共享避免重复编译
static const QRegularExpression &whitespaceRx()
{
    static const QRegularExpression rx(QStringLiteral("\\s+"));
    return rx;
}

void PressAnalyzer::addSearchHistory(const QString &text)
{
    if (text.isEmpty()) return;

    // 智能历史管理：如果已存在，先移除旧位置，再添加到开头
    historyHints.removeAll(text);
    // 如果是固定提示词（含内置与已锁定），则不进入历史，并清理历史中的同名项
    if (fixedHints.contains(text)) {
        updateCompleterWithSmartHints();
        updateSearchDropdownItems();
        return;
    }
    historyHints.prepend(text);

    // 限制历史数量，最多记录20个
    if (historyHints.size() > 20) {
        historyHints.removeLast();
    }

    // 智能更新 completer：优先显示历史记录
    updateCompleterWithSmartHints();
    updateSearchDropdownItems();
}

// 智能更新completer提示
void PressAnalyzer::updateCompleterWithSmartHints()
{
    if (!searchEdit) return;
    QCompleter *c = searchEdit->completer();
    if (!c) return;

    // 智能提示策略：
    // 1. 固定提示词总是放在前面（全部保留，不参与限额裁剪）
    // 2. 然后显示匹配的历史记录（仅对历史部分限额）
    QString currentText = searchEdit->text();

    QStringList hints;

    // 1. 首先添加固定提示词（总是显示且不被裁剪）
    hints.append(fixedHints);

    // 2. 然后添加匹配的历史记录（最多 20 条，避免历史无限膨胀）
    QStringList historyPart;
    if (currentText.isEmpty()) {
        historyPart = historyHints;
    } else {
        for (const QString &history : historyHints) {
            if (history.contains(currentText, Qt::CaseInsensitive)) {
                historyPart.append(history);
            }
        }
    }
    if (historyPart.size() > 20) historyPart = historyPart.mid(0, 20);
    hints.append(historyPart);

    // 更新completer
    QStringListModel *model = qobject_cast<QStringListModel*>(c->model());
    if (!model) {
        model = new QStringListModel(hints, c);
        c->setModel(model);
    } else {
        model->setStringList(hints);
    }
}

// 同步下拉菜单项目：固定提示词在上，历史提示词在下
void PressAnalyzer::updateSearchDropdownItems()
{
    if (!searchCombo) return;
    QString currentText = searchEdit ? searchEdit->text() : QString();
    QSignalBlocker blocker(searchCombo);

    // 复用同一个模型，防止视图在频繁重建时出现空白
    if (!searchDropdownModel) {
        searchDropdownModel = new QStandardItemModel(searchCombo);
        searchCombo->setModel(searchDropdownModel);
    }
    searchDropdownModel->clear();

    // 固定提示词（黑色文字 + 淡蓝背景）
    for (const QString &s : fixedHints) {
        QStandardItem *it = new QStandardItem(s);
        it->setForeground(QBrush(QColor(0, 0, 0))); // 黑色文字
        it->setBackground(QBrush(QColor(224, 238, 255))); // #E0EEFF 淡蓝底
        searchDropdownModel->appendRow(it);
    }

    // 历史提示词（灰色常规字体），最多20；排除所有固定提示词
    int count = 0;
    for (const QString &s : historyHints) {
        if (fixedHints.contains(s)) continue;
        if (count >= 20) break;
        QStandardItem *it = new QStandardItem(s);
        it->setForeground(QBrush(QColor(100, 100, 100)));
        searchDropdownModel->appendRow(it);
        ++count;
    }


    // 保持当前编辑文本
    if (searchEdit) searchEdit->setText(currentText);

    // 计算弹出视图宽度：以组合框宽度为基线，最宽不超过屏幕1/2
    if (searchCombo->view()) {
        QFontMetrics fm(searchCombo->view()->font());
        int maxw = 0;
        for (int i = 0; i < searchCombo->count(); ++i) {
            maxw = qMax(maxw, fm.horizontalAdvance(searchCombo->itemText(i)) + 30);
        }
        // 弹出宽度至少为组合框宽度，上限为屏幕宽度的一半
        int screenW = 1920;
        if (QScreen *scr = QGuiApplication::primaryScreen())
            screenW = scr->availableGeometry().width();
        int popupWidth = qBound(searchCombo->width(), maxw, screenW / 2);
        searchCombo->view()->setMinimumWidth(popupWidth);
        // 超过上限时用省略号截断，否则完整显示
        searchCombo->view()->setTextElideMode(maxw > popupWidth ? Qt::ElideMiddle : Qt::ElideNone);
    }
}

void PressAnalyzer::loadPinnedHints()
{
    QSettings st("ZZTools", "HoverLogAnalyzer");
    pinnedHints = st.value("search/pinnedHints").toStringList();
}

void PressAnalyzer::savePinnedHints()
{
    QSettings st("ZZTools", "HoverLogAnalyzer");
    st.setValue("search/pinnedHints", pinnedHints);
}

void PressAnalyzer::rebuildFixedHints()
{
    fixedHints.clear();
    // base 在前，pinned 在后（避免重复）
    for (const QString &s : baseFixedHints) fixedHints.append(s);
    for (const QString &s : pinnedHints) if (!fixedHints.contains(s)) fixedHints.append(s);
}

void PressAnalyzer::onDropdownContextMenu(const QPoint &pos)
{
    if (!searchCombo || !searchCombo->view()) return;
    QModelIndex idx = searchCombo->view()->indexAt(pos);
    if (!idx.isValid()) return;
    QString text = searchCombo->itemText(idx.row());

    QMenu menu;
    bool isPinned = pinnedHints.contains(text);
    QAction *actPin   = nullptr;
    QAction *actUnpin = nullptr;
    if (isPinned) actUnpin = menu.addAction("取消固定");
    else actPin = menu.addAction("固定为提示词");
    QAction *chosen = menu.exec(searchCombo->view()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == actPin) {
        if (!pinnedHints.contains(text)) pinnedHints.prepend(text);
        savePinnedHints();
        rebuildFixedHints();
        updateSearchDropdownItems();
        updateCompleterWithSmartHints();
        updatePinButtonState();
    } else if (chosen == actUnpin) {
        pinnedHints.removeAll(text);
        savePinnedHints();
        rebuildFixedHints();
        updateSearchDropdownItems();
        updateCompleterWithSmartHints();
        updatePinButtonState();
    }
}

void PressAnalyzer::onPinClicked()
{
    if (!searchEdit) return;
    QString text = searchEdit->text().trimmed();
    if (text.isEmpty()) return;
    // 只允许将"历史提示词"固定，若已是固定则直接返回
    if (pinnedHints.contains(text)) return;
    // 固定并保存
    pinnedHints.removeAll(text);
    pinnedHints.prepend(text);
    savePinnedHints();
    rebuildFixedHints();
    updateSearchDropdownItems();
    updateCompleterWithSmartHints();
    updatePinButtonState();
}

void PressAnalyzer::updatePinButtonState()
{
    if (!searchEdit) return;
    QString text = searchEdit->text().trimmed();
    bool canPin = !text.isEmpty() && !pinnedHints.contains(text);
    if (pinAction) {
        pinAction->setEnabled(canPin);
        pinAction->setToolTip(canPin ? "固定为提示词" : "已固定");
    }
}

// 显示搜索提示
void PressAnalyzer::showSearchHints()
{
    if (!searchEdit || !searchEdit->completer()) return;

    // 智能显示提示：固定（含 pinned）始终全部显示，再追加最近 10 条历史
    QStringList hints;

    // 1. 固定提示词全部显示（不受数量裁剪影响，避免 pinned 被滚动删除）
    hints.append(fixedHints);

    // 2. 追加最近使用的历史记录（最多 10 个）
    int historyCount = qMin(10, historyHints.size());
    for (int i = 0; i < historyCount; ++i) {
        hints.append(historyHints[i]);
    }

    // 更新completer
    QCompleter *c = searchEdit->completer();
    QStringListModel *model = qobject_cast<QStringListModel*>(c->model());
    if (!model) {
        model = new QStringListModel(hints, c);
        c->setModel(model);
    } else {
        model->setStringList(hints);
    }

    // 显示提示
    c->setCompletionPrefix("");
    c->complete();
}

// =================== 搜索相关 ===================
// 用于转义正则元字符，按字面匹配
QString escapeRegExp(const QString &text) {
    QString escaped = text;
    const QString specialChars = R"(\.^$|()[]*+?{})";
    for (int i = 0; i < specialChars.size(); ++i) {
        escaped.replace(specialChars[i], "\\" + QString(specialChars[i]));
    }
    return escaped;
}

void PressAnalyzer::searchAll()
{
    // 重入保护：长搜索中 processEvents 可能再次触发本函数
    if (searchInProgress) return;
    searchInProgress = true;
    auto guard = qScopeGuard([this]{ searchInProgress = false; });

    searchResults.clear();
    currentSearchIndex = -1;
    searchResultView->clearResults();
    searchHighlights.clear();

    QString text = searchEdit->text().trimmed();
    if (text.isEmpty()) return;

    // 分割多关键字：'|' 在方括号内时不作为分隔符
    QStringList parts = splitByPipeOutsideBrackets(text);
    QStringList rawKeys;
    QStringList normKeys;
    rawKeys.reserve(parts.size());
    normKeys.reserve(parts.size());
    const QRegularExpression &wsRx = whitespaceRx();
    for (const QString &part : parts) {
        QString k = part.trimmed();
        if (!k.isEmpty()) {
            rawKeys.append(k);
            QString nk = k;
            nk.replace(wsRx, " ");
            normKeys.append(nk);
        }
    }

    QVector<QPair<QRegularExpression, QColor>> patterns;
    patterns.reserve(rawKeys.size());
    for (int i = 0; i < rawKeys.size(); ++i) {
        // 构建正则用于结果列表着色（仍按字面匹配）
        QRegularExpression rx(QRegularExpression::escape(rawKeys[i]),
                              QRegularExpression::CaseInsensitiveOption);
        patterns.append(qMakePair(rx, searchColorForIndex(i)));
    }

    // 设置高亮（SearchResultTextView 内部使用 ExtraSelection 实现）
    searchResultView->setPatterns(patterns);

    // 遍历日志行，匹配关键字（使用 QString::indexOf 快路径，避免正则开销），并构建 logView 全局黄色高亮（重负载时跳过全量构建）
    QStringList resultLines;
    const int total = allLogLines.size();
    resultLines.reserve(qMin(total, 100000));
    // 大文件分块处理：每 chunkSize 行让 UI 喘息一次，避免界面冻结
    constexpr int kChunkSize = 20000;
    QStatusBar *sb = QMainWindow::statusBar();
    for (int i = 0; i < total; ++i) {
        bool matched = false;
        const QString &lineRef = allLogLines[i];
        // 先尝试原始关键字
        for (const QString &k : rawKeys) {
            if (lineRef.indexOf(k, 0, Qt::CaseInsensitive) != -1) { matched = true; break; }
        }
        // 若未匹配，再用"归一空格"的方式
        if (!matched) {
            QString ln = lineRef;
            ln.replace(wsRx, " ");
            for (const QString &k : normKeys) {
                if (ln.indexOf(k, 0, Qt::CaseInsensitive) != -1) { matched = true; break; }
            }
        }

        if (matched) {
            searchResults.push_back(i);
            QString itemText = QString("%1 | %2")
                                   .arg(i+1, 6, 10, QChar(' '))
                                   .arg(allLogLines[i]);
            // 不再截断结果列表，全部加入用于点击跳转
            resultLines.append(itemText);

            // 重负载：不构建全局黄色，交给可见区域增量高亮
        }

        // 分块刷新：报告进度并放行 UI 事件（>20K 行才做，避免小文件抖动）
        if (total > kChunkSize && (i & (kChunkSize - 1)) == kChunkSize - 1) {
            if (sb) {
                sb->showMessage(tr("正在搜索 %1 / %2 行（已命中 %3 条）...")
                                .arg(i + 1).arg(total).arg(searchResults.size()));
            }
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        }
    }
    if (sb) sb->clearMessage();

    if (searchResults.isEmpty()) {
        searchResultView->hide();
        searchDock->hide();
        QMessageBox::information(this, tr("搜索结果"), tr("匹配结果0,未搜索到内容。"));
        searchDock->setWindowTitle("查找结果");
        return;
    }

    // 一律重负载：不设置全局黄色，使用可见区域增量高亮
    searchResultView->setResultsText(resultLines);
    searchDock->setWindowTitle(QString("查找结果 - %1 命中").arg(searchResults.size()));
    searchResultView->show();
    currentSearchIndex = 0;
    jumpToSearchIndex(currentSearchIndex);
    // 立即更新一次可见区域高亮，避免初次无黄色
    updateVisibleHighlights();

    // 记录完整的原始搜索表达式，而不是分割后的关键字
    QString originalSearchText = searchEdit->text().trimmed();
    addSearchHistory(originalSearchText);

}

// 高亮搜索结果
void PressAnalyzer::highlightSearchResults(int currentIndex /* = -1 */)
{
    if (searchResults.isEmpty()) return;

    QTextDocument* doc = logView->document();

    // 仅在可见区域应用轻量高亮，避免整篇文档重绘
    QList<QTextEdit::ExtraSelection> selections = searchHighlights; // 先带上全局黄色高亮
    // ---------------- 2. 生成关键字颜色 ----------------
    // 与 searchAll() 保持一致，使用同样的分割（方括号内的 '|' 不分割）
    const QStringList keys = splitByPipeOutsideBrackets(searchEdit->text().trimmed());

    QVector<QColor> colors;
    colors.reserve(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        colors.append(searchColorForIndex(i));
    }

    // 只高亮当前索引所在行，其他行延迟到滚动时再做
    if (currentIndex >= 0 && currentIndex < searchResults.size()) {
        const int lineNumber = searchResults[currentIndex];
        QTextBlock block = doc->findBlockByNumber(lineNumber);
        if (block.isValid()) {
            // 整行浅灰底，帮助用户定位跳转行
            QTextEdit::ExtraSelection lineSel;
            lineSel.cursor = QTextCursor(block);
            lineSel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextCharFormat lineFmt;
            lineFmt.setBackground(QColor(180,180,180,140));
            lineSel.format = lineFmt;
            selections.prepend(lineSel); // 先画灰底，再画黄色关键字，避免覆盖

            QString lineText = block.text();
            for (int k = 0; k < keys.size(); ++k) {
                QString pattern = QRegularExpression::escape(keys[k]);
                QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatchIterator it = rx.globalMatch(lineText);
                while (it.hasNext()) {
                    QRegularExpressionMatch match = it.next();
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + match.capturedStart());
                    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, match.capturedLength());
                    QTextCharFormat fmt;
                    fmt.setBackground(colors[k]);
                    fmt.setForeground(Qt::black);
                    sel.format = fmt;
                    selections.push_back(sel);
                }
            }
        }
    }

    // updateVisibleHighlights() 会构建可见范围内的颜色标记+关键字高亮，
    // 不再需要全量 m_colorMarkHighlights 合并
    // 同步一次可见区域黄色关键字，确保不滚动也能看到
    updateVisibleHighlights();
}


void PressAnalyzer::onSearchResultRowClicked(int row)
{
    if (row < 0 || row >= searchResults.size()) return;
    currentSearchIndex = row;
    highlightSearchResults(currentSearchIndex);
}

void PressAnalyzer::onSearchResultRowDoubleClicked(int row)
{
    if (row < 0 || row >= searchResults.size()) return;
    currentSearchIndex = row;
    jumpToSearchIndex(currentSearchIndex);
}

void PressAnalyzer::jumpToSearchIndex(int index)
{
    if (index<0 || index>=searchResults.size()) return;
    currentSearchIndex = index;
    highlightSearchResults(currentSearchIndex);

    // 执行跳转到对应行并居中显示
    QTextDocument* doc = logView->document();
    QTextBlock currentBlock = doc->findBlockByNumber(searchResults[currentSearchIndex]);
    if (currentBlock.isValid()) {
        QTextCursor cursor(currentBlock);
        cursor.movePosition(QTextCursor::StartOfBlock);
        logView->setTextCursor(cursor);
        logView->centerCursor();

        // updateVisibleHighlights() 会构建可见范围的颜色标记+关键字+当前行灰底
        // 不再需要手动合并 m_colorMarkHighlights
        updateVisibleHighlights();
    }
}

void PressAnalyzer::goToPrevSearch()
{
    if (searchResults.isEmpty()) return;
    currentSearchIndex--;
    if (currentSearchIndex < 0) currentSearchIndex = searchResults.size() - 1;
    jumpToSearchIndex(currentSearchIndex);
}

void PressAnalyzer::goToNextSearch()
{
    if (searchResults.isEmpty()) return;
    currentSearchIndex++;
    if (currentSearchIndex >= searchResults.size()) currentSearchIndex = 0;
    jumpToSearchIndex(currentSearchIndex);
}

void PressAnalyzer::offerChartFromSearchResults()
{
    if (searchResults.isEmpty()) return;

    // 扫描所有匹配行，收集出现过的键名
    QRegularExpression kvRx(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*[:=]\s*-?\d[\d.]*\b)");
    QMap<QString, int> keyCount;

    for (int idx : searchResults) {
        if (idx < 0 || idx >= allLogLines.size()) continue;
        const QString &line = allLogLines[idx];
        QSet<QString> seenInLine;
        auto it = kvRx.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            QString k = m.captured(1);
            if (!seenInLine.contains(k)) { keyCount[k]++; seenInLine.insert(k); }
        }
    }

    if (keyCount.isEmpty()) return;

    QStringList keys = keyCount.keys();
    std::sort(keys.begin(), keys.end(), [&](const QString &a, const QString &b){
        return keyCount[a] > keyCount[b];
    });

    // 多选对话框
    QDialog dlg(this);
    dlg.setWindowTitle("选择要绘制的字段");
    dlg.setMinimumWidth(300);
    QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);

    QLabel *hint = new QLabel(
        QString("在 %1 条搜索结果中发现以下数值字段，\n选择后将添加到动态图表：")
            .arg(searchResults.size()), &dlg);
    hint->setWordWrap(true);
    dlgLayout->addWidget(hint);

    QListWidget *listW = new QListWidget(&dlg);
    listW->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const QString &k : keys) {
        auto *item = new QListWidgetItem(
            QString("%1  （%2 行）").arg(k).arg(keyCount[k]), listW);
        item->setData(Qt::UserRole, k);
    }
    dlgLayout->addWidget(listW);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    QPushButton *btnOk     = new QPushButton("添加到图表", &dlg);
    QPushButton *btnCancel = new QPushButton("取消",       &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    btnLayout->addWidget(btnCancel);
    dlgLayout->addLayout(btnLayout);
    connect(btnOk,     &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    QStringList selectedKeys;
    for (auto *item : listW->selectedItems())
        selectedKeys << item->data(Qt::UserRole).toString();
    if (selectedKeys.isEmpty()) return;

    // 确保管理器已创建（正常情况下初始化时已创建）
    if (!m_chartManager)
        m_chartManager = new DynamicChartManager(allLogLines, this);

    QRegularExpression tsRx(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    const QString searchLabel = searchCombo->currentText();

    QStringList seriesLabels;
    QVector<QVector<double>>    valuesPerKey;
    QVector<QVector<QDateTime>> timestampsPerKey;

    for (const QString &fieldKey : selectedKeys) {
        QRegularExpression valRx(
            QString(R"(\b%1\s*[:=]\s*([-\d.]+))").arg(QRegularExpression::escape(fieldKey)));
        QVector<double>    values;
        QVector<QDateTime> timestamps;
        for (int idx : searchResults) {
            if (idx < 0 || idx >= allLogLines.size()) continue;
            const QString &line = allLogLines[idx];
            auto tsM  = tsRx.match(line);
            auto valM = valRx.match(line);
            if (!tsM.hasMatch() || !valM.hasMatch()) continue;
            QDateTime dt = QDateTime::fromString(tsM.captured(1), "yyyy-MM-dd HH:mm:ss");
            bool ok = false;
            double v = valM.captured(1).toDouble(&ok);
            if (!dt.isValid() || !ok) continue;
            timestamps.append(dt);
            values.append(v);
        }
        if (values.isEmpty()) continue;
        seriesLabels << QString("%1 (%2)").arg(fieldKey, searchLabel);
        valuesPerKey << values;
        timestampsPerKey << timestamps;
    }

    if (seriesLabels.isEmpty()) return;

    QString cardTitle = selectedKeys.size() == 1
        ? QString("%1 (%2)").arg(selectedKeys.first(), searchLabel)
        : QString("%1 等 %2 项 (%3)")
              .arg(selectedKeys.first()).arg(selectedKeys.size()).arg(searchLabel);

    m_chartManager->addChartDirect(cardTitle, selectedKeys,
                                   valuesPerKey, timestampsPerKey, seriesLabels);
    // 显示动态图表浮动窗口（定位到主窗口所在屏幕）
    if (m_chartFloatWin) {
        if (!m_chartFloatWin->isVisible()) {
            QRect ref = geometry();
            QScreen *scr = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
            QRect avail = scr ? scr->availableGeometry() : ref;
            int wx = qBound(avail.left(), ref.left() + 60, avail.right()  - m_chartFloatWin->width());
            int wy = qBound(avail.top(),  ref.top()  + 40, avail.bottom() - m_chartFloatWin->height());
            m_chartFloatWin->move(wx, wy);
        }
        m_chartFloatWin->show();
        m_chartFloatWin->raise();
        m_chartFloatWin->activateWindow();
    }
}
