// PressAnalyzerTabs.cpp - Multi-tab management and background parsing
#include "PressAnalyzer.h"

#include <QTextCursor>
#include <QTextBlock>
#include <QScrollBar>
#include <QThread>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextDocumentLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QStyle>
#include <QApplication>
#include <QProgressBar>
#include <QStatusBar>
#include "LogNumberHighlighter.h"
#include "MultiLanguageSyntaxHighlighter.h"
#include "ParseProgressDialog.h"
#include "HLogBinaryParser.h"
#include "LogParserWorker.h"

QAbstractButton *PressAnalyzer::makeTabCloseButton(int /*tabIndex*/)
{
    QPushButton *btn = new QPushButton(m_tabBar);
    btn->setFixedSize(16, 16);
    btn->setFlat(true);
    btn->setCursor(Qt::ArrowCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setStyleSheet(
        QString(
        "QPushButton {"
        "  border-radius: 7px;"
        "  background-color: rgba(0,0,0,0);"
        "  border: none;"
        "  color: #999999;"
        "  font-size: %1px;"
        "  font-weight: bold;"
        "  padding: 0px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #FF5F57;"
        "  color: white;"
        "}"
        ).arg(stylePx(11))
    );
    btn->setText("\xC3\x97");  // UTF-8 ×
    connect(btn, &QPushButton::clicked, this, [this, btn]() {
        for (int i = 0; i < m_tabBar->count(); ++i) {
            if (m_tabBar->tabButton(i, QTabBar::RightSide) == btn) {
                closeTab(i);
                return;
            }
        }
    });
    return btn;
}

// ============================================================
// 后台解析成员方法：连接信号并启动线程（含 generation 守卫）
// ============================================================
void PressAnalyzer::startBackgroundParse(LogParserWorker *worker,
                                          QThread *thread,
                                          int generation)
{
    // 启动前打开独立的进度窗口（懒创建，重复使用同一实例）
    if (!m_parseProgressDialog) {
        m_parseProgressDialog = new ParseProgressDialog(this);
    }
    m_parseProgressDialog->setProgress(0, tr("准备开始解析..."));
    m_parseProgressDialog->show();
    m_parseProgressDialog->raise();
    m_parseProgressDialog->activateWindow();

    worker->moveToThread(thread);
    QObject::connect(thread,  &QThread::started,
                     worker,  &LogParserWorker::run);
    QObject::connect(worker,  &LogParserWorker::progressChanged,
                     this,    &PressAnalyzer::onParseProgress,
                     Qt::QueuedConnection);
    // parseFinished 通过 lambda 捕获 generation，旧线程结果自动丢弃
    QObject::connect(worker,  &LogParserWorker::parseFinished,
                     this,    [this, generation](ParseResult result){
                         if (m_parseGeneration == generation)
                             onParseFinished(result);
                     },
                     Qt::QueuedConnection);
    QObject::connect(worker,  &LogParserWorker::parseError,
                     this,    [this](const QString &msg){
                         QMessageBox::warning(this, "解析错误", msg);
                     },
                     Qt::QueuedConnection);
    // 线程结束后自动清理
    QObject::connect(thread,  &QThread::finished,
                     worker,  &QObject::deleteLater);
    QObject::connect(thread,  &QThread::finished,
                     thread,  &QObject::deleteLater);
    thread->start();
}

// ============================================================
// 解析进度槽：更新状态栏
// ============================================================
// 多标签页管理：保存/恢复/打开/关闭
// ============================================================

// 将当前激活标签的状态保存到 m_tabStates[m_currentTabIndex]，
// 并将 logView 换为空文档（为下一个标签腾出空间）。
void PressAnalyzer::saveCurrentTabState()
{
    if (m_currentTabIndex < 0 || m_currentTabIndex >= m_tabStates.size()) return;
    TabState &st = m_tabStates[m_currentTabIndex];

    // 保存当前标签的字体快照（缩放字号等）
    st.logFont     = currentLogFont;
    st.logFontPtSize = logFontPointSize;

    // O(1) 文档交换：从 logView 取走文档，换入新空文档
    st.document = logView->document();
    st.document->setParent(this);   // 转交给 PressAnalyzer 管理，避免 Qt 内部 control 提前删除
    QTextDocument *fresh = new QTextDocument(this);  // parent=this，窗口关闭时统一清理
    fresh->setDocumentLayout(new QPlainTextDocumentLayout(fresh));  // QPlainTextEdit 要求此 layout
    // 显式设置新文档默认字体为 currentLogFont，避免使用 QApplication 默认字体
    fresh->setDefaultFont(currentLogFont);
    m_loadingFile = true;
    logView->setDocument(fresh);
    m_loadingFile = false;
    // 新 tab 文档挂多语言高亮器（默认 Log 模式：数字 + 级别关键字），
    // 更新成员指针以便 loadFileToLogView 按扩展名切换 mode
    m_codeHighlighter = new MultiLanguageSyntaxHighlighter(fresh);
    logView->setFont(currentLogFont);
    logView->document()->setDefaultFont(currentLogFont);  // 双重保障

    // 数据快照
    st.allLogLines   = allLogLines;
    st.allEvents     = allEvents;
    st.cameraEvents  = cameraEvents;
    st.statusEvents  = statusEvents;
    st.batteryinfo   = batteryinfo;
    st.soctmp        = soctmp;
    st.cameraTemps   = cameraTemps;
    st.allusage      = allusage;
    st.colorMarks    = m_colorMarks;
    st.colorMarkHitData            = m_colorMarkHitData;
    st.searchViewColorHighlights   = m_searchViewColorHighlights;
    st.searchResults               = searchResults;
    st.currentSearchIndex          = currentSearchIndex;
    st.searchHighlights            = searchHighlights;
    // 保存搜索结果面板内容，以便切回该标签时恢复
    st.searchResultLines = searchResultView
        ? searchResultView->toPlainText().split('\n', Qt::KeepEmptyParts)
        : QStringList();
    // 移除末尾因 split 产生的空行（join("\n") 末尾不带 \n，split 不会多出空项；但防御性清理）
    while (!st.searchResultLines.isEmpty() && st.searchResultLines.last().isEmpty())
        st.searchResultLines.removeLast();
    st.searchPatterns     = searchResultView ? searchResultView->patterns() : QVector<QPair<QRegularExpression,QColor>>();
    st.searchKeyword      = searchEdit ? searchEdit->text().trimmed() : QString();
    st.searchDockVisible  = searchDock && !searchDock->isHidden() && !searchResults.isEmpty();
    st.triggerCount  = triggerCount;
    st.flightCount   = flightCount;
    st.sn            = sn;
    st.pendingTopLogs = m_pendingTopLogs;

    // UI 状态
    st.scrollValue    = logView->verticalScrollBar()->value();
    st.cursorPosition = logView->textCursor().position();
    st.windowTitle    = windowTitle();
    st.statusPath     = statusPathLabel ? statusPathLabel->text() : QString();
    st.statusInfo     = statusInfoLabel ? statusInfoLabel->text() : QString();
    st.statusDockVisible = statusDock && !statusDock->isHidden();
}

// 从 m_tabStates[index] 恢复状态到 UI（logView、事件列表、图表等）。
void PressAnalyzer::restoreTabState(int index)
{
    if (index < 0 || index >= m_tabStates.size()) return;
    TabState &st = m_tabStates[index];

    // 文档还原：如果该标签有存储的文档，换入 logView
    if (st.document) {
        QTextDocument *old = logView->document();
        m_loadingFile = true;
        logView->setDocument(st.document);
        m_loadingFile = false;
        st.document = nullptr;
        // 不 delete old：old 的 parent=this，由 PressAnalyzer 在关闭时统一销毁。
        // 直接 delete 可能触发 LogNumberHighlighter 的 pending QTimer 事件，
        // 导致 use-after-free crash。
        Q_UNUSED(old);
    }
    // 否则 logView 已持有该标签的文档（理论上不应发生，但安全起见保持不变）

    // 恢复该标签的字体状态
    if (st.logFontPtSize > 0) {
        currentLogFont    = st.logFont;
        logFontPointSize  = st.logFontPtSize;
    }

    // setDocument 后 Qt 可能重置字体，确保始终应用当前字体到 logView 及其文档
    logView->setFont(currentLogFont);
    logView->document()->setDefaultFont(currentLogFont);  // 显式同步文档默认字体

    // 同步其他关联控件的字体
    if (searchResultView) searchResultView->setFont(currentLogFont);
    if (searchCombo) searchCombo->setFont(currentLogFont);
    if (eventList) eventList->setFont(currentEventFont);
    if (cameraEventList) cameraEventList->setFont(currentEventFont);
    if (heartbeatLostEventList) heartbeatLostEventList->setFont(currentEventFont);

    // 恢复数据
    allLogLines      = st.allLogLines;
    allEvents        = st.allEvents;
    cameraEvents     = st.cameraEvents;
    statusEvents     = st.statusEvents;
    batteryinfo      = st.batteryinfo;
    soctmp           = st.soctmp;
    cameraTemps      = st.cameraTemps;
    allusage         = st.allusage;
    m_colorMarks     = st.colorMarks;
    m_colorMarkHitData          = st.colorMarkHitData;
    m_searchViewColorHighlights = st.searchViewColorHighlights;
    searchResults      = st.searchResults;
    currentSearchIndex = st.currentSearchIndex;
    searchHighlights   = st.searchHighlights;
    triggerCount = st.triggerCount;
    flightCount  = st.flightCount;
    sn           = st.sn;
    m_pendingTopLogs = st.pendingTopLogs;

    // 恢复搜索结果面板内容
    if (searchResultView) {
        if (!st.searchResultLines.isEmpty()) {
            // 先恢复 patterns，再设置文本（setResultsText 内部会调用 applyHighlighting）
            searchResultView->setPatterns(st.searchPatterns);
            searchResultView->setResultsText(st.searchResultLines);
            if (searchDock) {
                searchDock->setWindowTitle(QString("查找结果 - %1 命中").arg(searchResults.size()));
                searchDock->show();
            }
            searchResultView->show();
        } else {
            searchResultView->setPatterns({});
            searchResultView->clearResults();
            if (searchDock && !st.searchDockVisible) searchDock->hide();
        }
    }
    // 恢复搜索框关键字（不触发新搜索）
    if (searchEdit && !st.searchKeyword.isEmpty())
        searchEdit->setText(st.searchKeyword);

    // 重建事件列表控件
    repopulateEventLists();

    // 恢复滚动/光标
    {
        int maxPos = qMax(0, logView->document()->characterCount() - 1);
        QTextCursor cur = logView->textCursor();
        cur.setPosition(qBound(0, st.cursorPosition, maxPos));
        logView->setTextCursor(cur);
        logView->verticalScrollBar()->setValue(st.scrollValue);
    }

    // 窗口标题和状态栏
    setWindowTitle(st.windowTitle.isEmpty() ? "日志分析工具" : st.windowTitle);
    if (statusPathLabel) statusPathLabel->setText(st.statusPath);
    if (statusInfoLabel) statusInfoLabel->setText(st.statusInfo);

    // 状态面板：仅 CE 标签才更新图表数据（不改变 statusDock 可见性）
    if (st.isControlEngine) {
        batteryChart->setData(batteryinfo);
        cameraTempChart->setData(cameraTemps);
        socChart->clear();
        socChart->addData(soctmp);
        usageChart->setData(allusage);
        if (titleLabel)
            titleLabel->setText(QString("心跳丢失次数:%1").arg(statusEvents.size()));
    }

    // 刷新高亮
    updateVisibleHighlights();
}

// 重新填充 eventList / cameraEventList / heartbeatLostEventList 及时间轴，
// 使用当前成员变量 allEvents / cameraEvents / statusEvents。
void PressAnalyzer::repopulateEventLists()
{
    // 主事件列表
    eventList->clear();
    static const QList<QColor> bgColors = {
        QColor("#FFCCCC"), QColor("#CCE5FF"), QColor("#CCFFCC"),
        QColor("#FFF2CC"), QColor("#E5CCFF"), QColor("#FFCCE5"),
        QColor("#CCE5FF"), QColor("#CCFFE5"), QColor("#FFE5CC"), QColor("#CCFFFF")
    };
    for (int i = 0; i < allEvents.size(); ++i) {
        QListWidgetItem *item = new QListWidgetItem(allEvents[i].display);
        // 优先使用存储的颜色（保持和原始渲染一致），回退到索引推导
        QColor bg = allEvents[i].bgColor.isValid()
                        ? allEvents[i].bgColor
                        : bgColors[i % bgColors.size()];
        item->setBackground(bg);
        eventList->addItem(item);
    }
    if (!allEvents.isEmpty()) { m_sideBar->showPanel(1); }

    // 相机事件列表
    cameraEventList->clear();
    for (const EventItem &ev : cameraEvents) {
        QListWidgetItem *item = new QListWidgetItem(ev.display);
        QColor bg;
        if (ev.bgColor.isValid()) {
            bg = ev.bgColor;
        } else {
            int s = ev.display.indexOf('[');
            int e = ev.display.indexOf(']', s);
            QString note = (s >= 0 && e > s) ? ev.display.mid(s + 1, e - s - 1) : "";
            if (note == "close")                                       bg = QColor(169,169,169);
            else if (note == "init")                                   bg = QColor(211,211,211);
            else if (note.contains("recording"))                       bg = Qt::red;
            else if (note.contains("stream") && note.contains("preview")) bg = QColor(255,165,0);
            else if (note.contains("stream"))                          bg = Qt::green;
            else if (note.contains("preview"))                         bg = Qt::yellow;
            else if (note.contains("snapshot"))                        bg = Qt::cyan;
            else                                                       bg = Qt::white;
        }
        item->setBackground(bg);
        cameraEventList->addItem(item);
    }

    // 心跳丢失列表
    heartbeatLostEventList->clear();
    for (const EventItem &ev : statusEvents) {
        QListWidgetItem *item = new QListWidgetItem(ev.display);
        item->setBackground(QColor(255, 182, 193));
        heartbeatLostEventList->addItem(item);
    }
    if (titleLabel)
        titleLabel->setText(QString("心跳丢失次数:%1").arg(heartbeatLostEventList->count()));

    // 时间轴
    if (eventTimeline) {
        QList<TimelineEvent> tlEvents;
        for (const auto &ev : cameraEvents) {
            TimelineEvent te;
            te.lineNumber = ev.lineNumber;
            te.timestamp  = ev.timestamp;
            te.display    = ev.display;
            te.category   = "camera";
            int s = ev.display.indexOf('[');
            int e = ev.display.indexOf(']', s);
            te.note = (s >= 0 && e > s) ? ev.display.mid(s + 1, e - s - 1) : "";
            tlEvents.append(te);
        }
        for (const auto &ev : statusEvents) {
            TimelineEvent te;
            te.lineNumber = ev.lineNumber;
            te.timestamp  = ev.timestamp;
            te.display    = ev.display;
            te.category   = "heartbeat";
            te.note       = "";
            tlEvents.append(te);
        }
        std::sort(tlEvents.begin(), tlEvents.end(),
                  [](const TimelineEvent &a, const TimelineEvent &b) {
                      if (a.timestamp.isValid() && b.timestamp.isValid())
                          return a.timestamp < b.timestamp;
                      return a.lineNumber < b.lineNumber;
                  });
        eventTimeline->setEvents(tlEvents);
    }
}

// 推断标签名：control_engine_log 路径→ "control_engine"；否则使用目录/文件名。
QString PressAnalyzer::tabLabelForPath(const QString &path) const
{
    if (path.isEmpty()) return "新标签";
    QFileInfo fi(path);
    // 如果路径本身或父目录包含 control_engine_log，命名为 control_engine
    if (path.contains("control_engine_log", Qt::CaseInsensitive) ||
        fi.dir().dirName().contains("control_engine_log", Qt::CaseInsensitive))
        return "control_engine";
    return fi.fileName().isEmpty() ? "新标签" : fi.fileName();
}

// 在新标签页中智能解析指定路径（文件 or 目录）。
void PressAnalyzer::openInNewTab(const QString &path)
{
    // 1. 保存当前标签
    saveCurrentTabState();

    // 2. 创建新的 TabState，继承当前字体设置
    TabState newState;
    newState.sourcePath    = path;
    newState.logFont       = currentLogFont;
    newState.logFontPtSize = logFontPointSize;
    m_tabStates.append(newState);
    int newIdx = m_tabStates.size() - 1;

    // 3. 将标签栏切换到新标签（阻断 currentChanged 信号避免递归）
    m_tabBar->blockSignals(true);
    m_tabBar->addTab(tabLabelForPath(path));
    m_tabBar->setCurrentIndex(newIdx);
    m_tabBar->setTabButton(newIdx, QTabBar::RightSide, makeTabCloseButton(newIdx));
    m_tabBar->setTabColor(newIdx, tabColorForIndex(newIdx));
    m_tabBar->blockSignals(false);

    // 4. 更新当前标签索引（logView 此时持有为新标签创建的空文档）
    m_currentTabIndex = newIdx;

    // 5. 智能解析：CE 路径 → loadAndAnalyzeLogsFromPath，目录 → loadMergeLogsFromPath，文件 → loadFileToLogView
    QString analysisRoot = findControlEngineAnalysisRoot(path);
    if (!analysisRoot.isEmpty()) {
        loadAndAnalyzeLogsFromPath(analysisRoot);
    } else {
        QFileInfo fi(path);
        if (fi.isFile()) {
            loadFileToLogView(path);
        } else {
            loadMergeLogsFromPath(path, false);
        }
    }
}

// 打开一个空白新标签（无文件）
void PressAnalyzer::openNewEmptyTab()
{
    // 保存当前标签
    saveCurrentTabState();

    TabState newState;
    newState.logFont       = currentLogFont;
    newState.logFontPtSize = logFontPointSize;
    m_tabStates.append(newState);
    int newIdx = m_tabStates.size() - 1;

    m_tabBar->blockSignals(true);
    m_tabBar->addTab("新标签");
    m_tabBar->setCurrentIndex(newIdx);
    m_tabBar->setTabButton(newIdx, QTabBar::RightSide, makeTabCloseButton(newIdx));
    m_tabBar->setTabColor(newIdx, tabColorForIndex(newIdx));
    m_tabBar->blockSignals(false);

    m_currentTabIndex = newIdx;

    // 清空 UI 到初始状态
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    statusEvents.clear();
    searchResults.clear();
    currentSearchIndex = -1;
    searchHighlights.clear();
    if (searchResultView) searchResultView->clearResults();
    if (searchDock) searchDock->hide();
    if (searchEdit) searchEdit->clear();
    eventList->clear();
    cameraEventList->clear();
    heartbeatLostEventList->clear();
    setWindowTitle("日志分析工具");
    if (statusPathLabel) statusPathLabel->setText(QString());
    if (statusInfoLabel) statusInfoLabel->setText(QString());
}

// 关闭指定索引的标签页。若只剩一个标签则新建空标签而不关闭窗口。
void PressAnalyzer::closeTab(int index)
{
    if (m_tabStates.size() == 1) {
        // 最后一个标签：清空内容，重置为空白标签
        m_tabBar->setTabText(0, "新标签");

        // 清空当前文档
        logView->clear();

        // 重置所有状态
        allLogLines.clear();
        allEvents.clear();
        cameraEvents.clear();
        statusEvents.clear();
        searchResults.clear();
        currentSearchIndex = -1;
        searchHighlights.clear();
        if (searchResultView) searchResultView->clearResults();
        if (searchDock) searchDock->hide();
        if (searchEdit) searchEdit->clear();
        eventList->clear();
        cameraEventList->clear();
        heartbeatLostEventList->clear();
        setWindowTitle("日志分析工具");
        if (statusPathLabel) statusPathLabel->setText(QString());
        if (statusInfoLabel) statusInfoLabel->setText(QString());

        // 重置 TabState
        m_tabStates[0] = TabState();
        m_tabStates[0].logFont       = currentLogFont;
        m_tabStates[0].logFontPtSize = logFontPointSize;
        return;
    }

    if (index == m_currentTabIndex) {
        // 关闭当前激活标签：选择相邻标签
        // 优先选左侧；如果是第一个则选右侧（现在的 index 1）
        int newIdx = (index > 0) ? index - 1 : 1;

        // 删除 TabState（文档在 logView 中，不需要 delete）
        m_tabStates.remove(index);

        // 移除 tabBar 后，原 index 右侧的所有标签索引均左移 1。
        // newIdx 若原本大于 index，需要相应减 1 指向正确标签。
        if (newIdx > index) --newIdx;
        if (newIdx >= m_tabStates.size()) newIdx = m_tabStates.size() - 1;

        m_tabBar->blockSignals(true);
        m_tabBar->removeTab(index);
        m_tabBar->shiftColorsAfterRemove(index);
        m_tabBar->setCurrentIndex(newIdx);
        m_tabBar->blockSignals(false);

        m_currentTabIndex = -1;  // 哨兵，避免 restoreTabState 提前写入
        restoreTabState(newIdx);
        m_currentTabIndex = newIdx;

    } else {
        // 关闭非激活标签：删除存储的文档并移除
        if (m_tabStates[index].document) {
            delete m_tabStates[index].document;
            m_tabStates[index].document = nullptr;
        }
        m_tabStates.remove(index);

        m_tabBar->blockSignals(true);
        m_tabBar->removeTab(index);
        m_tabBar->shiftColorsAfterRemove(index);
        m_tabBar->blockSignals(false);

        // 如果关闭的标签在当前标签之前，当前索引需要左移
        if (index < m_currentTabIndex) {
            --m_currentTabIndex;
            m_tabBar->blockSignals(true);
            m_tabBar->setCurrentIndex(m_currentTabIndex);
            m_tabBar->blockSignals(false);
        }
    }
}

void PressAnalyzer::loadPathSmart(const QString &path)
{
    if (path.isEmpty()) return;
    QString analysisRoot = findControlEngineAnalysisRoot(path);
    if (!analysisRoot.isEmpty()) {
        loadAndAnalyzeLogsFromPath(analysisRoot);
    } else {
        QFileInfo fi(path);
        if (fi.isFile())
            loadFileToLogView(path);
        else
            loadMergeLogsFromPath(path, false);
    }
}

void PressAnalyzer::detachTabToNewWindow(int index)
{
    // 捕获 sourcePath（关闭 tab 之前）
    QString sp = m_tabStates[index].sourcePath;

    closeTab(index);

    auto *w = new PressAnalyzer(nullptr);
    w->setAttribute(Qt::WA_DeleteOnClose, true);
    // 传递当前共享字体
    if (s_sharedLogFontSet) {
        w->currentLogFont = s_sharedLogFont;
        w->logFontPointSize = s_sharedLogFont.pointSize();
        if (w->logView) {
            w->logView->setFont(s_sharedLogFont);
            w->logView->document()->setDefaultFont(s_sharedLogFont);
        }
    }
    w->show();

    if (!sp.isEmpty()) {
        // 用 QueuedConnection 确保新窗口完全初始化后再加载
        QMetaObject::invokeMethod(w, [w, sp]() {
            w->loadPathSmart(sp);
        }, Qt::QueuedConnection);
    }
}

// ============================================================
void PressAnalyzer::onParseProgress(int percent, const QString &statusText)
{
    // 进度统一推送到独立窗口；m_progressBar / 状态栏文案不再使用
    if (m_parseProgressDialog) {
        m_parseProgressDialog->setProgress(percent, statusText);
    }
}

// ============================================================
// 解析完成槽：将 ParseResult 分发到各 UI 控件
// ============================================================
void PressAnalyzer::onParseFinished(ParseResult result)
{
    // 清理线程对象（已通过 deleteLater 连接，无需手动 delete）
    m_parseThread = nullptr;
    m_parseWorker = nullptr;

    // 进度条和状态栏：在 GUI 后处理阶段继续显示，到流程结束才清空
    auto reportStage = [this](const QString &text) {
        // 阶段反馈推到独立进度窗口（保留进度条数值不变，仅刷新阶段文案）
        if (m_parseProgressDialog) m_parseProgressDialog->setProgress(-1, text);
        // 强制 paint 一次，让用户立刻看到当前阶段
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
    };
    if (m_parseProgressDialog) {
        m_parseProgressDialog->show();
        m_parseProgressDialog->setProgress(-1, tr("解析完成，正在加载到界面..."));
    }

    // -------- 同步解析结果到成员变量（用 std::move 避免 100MB 级别的隐式共享 detach 拷贝）--------
    allLogLines    = std::move(result.allLogLines);
    triggerCount   = result.triggerCount;
    flightCount    = result.flightCount;
    batteryinfo    = std::move(result.batteryinfo);
    cameraTemps    = std::move(result.cameraTemps);
    soctmp         = std::move(result.soctmp);
    if (!result.sn.isEmpty()) {
        sn = result.sn;
        if (statusInfoLabel) {
            QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                .arg(result.imageVer.isEmpty() ? "-" : result.imageVer)
                .arg(result.ipkVer.isEmpty()   ? "-" : result.ipkVer)
                .arg(result.sn.isEmpty()        ? "-" : result.sn)
                .arg(result.hwid.isEmpty()      ? "-" : result.hwid);
            statusInfoLabel->setText(info);
        }
        if (!result.version.isEmpty()) version = result.version;
    }

    // -------- 先 setPlainText，再给 EventItem.block 赋值 --------
    logView->setUpdatesEnabled(false);
    // 关键：Qt 的 QPlainTextEdit::setPlainText 会“跨越 clear 保留 cursor 的 charFormat”
    //（内部会先保存 charFormatForInsertion，doc->clear() 之后再把它应用回去），
    // 所以如果用户上一次点击时光标停在被 highlightLine 染红的行，重新解析进入此路径
    // 时整个新文档会继承那个红底 charFormat。这里在 setPlainText 之前把 cursor 的
    // charFormat 显式重置为默认，避免污染。
    {
        QTextCursor cur = logView->textCursor();
        cur.setCharFormat(QTextCharFormat());
        cur.setBlockCharFormat(QTextCharFormat());
        logView->setTextCursor(cur);
        logView->setCurrentCharFormat(QTextCharFormat());
    }
    m_loadingFile = true;
    reportStage(tr("加载文本到视图（%1 行）...").arg(allLogLines.size()));
    // 关键优化：setPlainText 期间临时卸载语法高亮器，避免对每一个 block（可能 100 万行）反复
    // 触发 highlightBlock + 多正则匹配。setPlainText 完成后再绑回去，让 QSyntaxHighlighter
    // 自身的延迟机制只对当前可视区域立刻高亮，剩余 block 滚动到时再处理。
    QTextDocument *prevHlDoc = nullptr;
    if (m_codeHighlighter) {
        prevHlDoc = m_codeHighlighter->document();
        m_codeHighlighter->setDocument(nullptr);
    }
    // 一次性把所有行 join 成大文本，避免 worker 端额外维护一份 textBuffer（节省 ~100MB 内存 + 一次大拷贝）
    logView->setPlainText(allLogLines.join(QLatin1Char('\n')));
    if (m_codeHighlighter && prevHlDoc) {
        m_codeHighlighter->setDocument(logView->document());
    }
    m_loadingFile = false;
    markTabModified(false);
    // 行号已由 LogView 独立行号栏显示，正文不再嵌入数字前缀
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].hasLinePrefix = false;
    // setPlainText 内部调用 document->clear()，会重置 defaultFont，需要重新应用
    logView->setFont(currentLogFont);
    logView->document()->setDefaultFont(currentLogFont);  // 显式同步文档默认字体

    // -------- 拆分事件到各自列表（block 赋值必须在 setPlainText 之后）--------
    allEvents.clear();
    cameraEvents.clear();
    statusEvents.clear();
    eventList->clear();
    cameraEventList->clear();
    heartbeatLostEventList->clear();

    // 大批量 addItem 时关闭三个列表的更新与排序，结束后再统一刷新（10K+ 事件时差异显著）
    eventList->setUpdatesEnabled(false);
    cameraEventList->setUpdatesEnabled(false);
    heartbeatLostEventList->setUpdatesEnabled(false);
    reportStage(tr("填充事件列表（共 %1 条事件）...").arg(result.events.size()));
    // 提前 reserve，避免 QList 反复扩容
    allEvents.reserve(result.events.size());
    cameraEvents.reserve(result.events.size() / 4 + 1);
    statusEvents.reserve(result.events.size() / 8 + 1);

    // main 事件配色表抽到循环外（原先每条事件都重新构造一次 QList）
    static const QList<QColor> kMainBgColors = {
        QColor("#FFCCCC"), QColor("#CCE5FF"), QColor("#CCFFCC"),
        QColor("#FFF2CC"), QColor("#E5CCFF"), QColor("#FFCCE5"),
        QColor("#CCE5FF"), QColor("#CCFFE5"), QColor("#FFE5CC"), QColor("#CCFFFF")
    };

    // 使用顺序遍历 block 替代逐个 findBlockByNumber，性能从 O(events*log(blocks)) 优化到 O(blocks)
    QTextBlock currentBlock = logView->document()->begin();
    int currentBlockNum = 0;

    for (const ParsedEventItem &ev : result.events) {
        const int targetBlockNum = ev.lineNumber - 1;
        // 顺序前进到目标 block（事件按行号递增排列）
        while (currentBlockNum < targetBlockNum && currentBlock.isValid()) {
            currentBlock = currentBlock.next();
            currentBlockNum++;
        }

        if (ev.eventCategory == "camera") {
            QListWidgetItem *item = new QListWidgetItem(ev.display);
            int startBracket = ev.display.indexOf('[');
            int endBracket   = ev.display.indexOf(']', startBracket);
            QString note = (startBracket >= 0 && endBracket > startBracket)
                               ? ev.display.mid(startBracket + 1, endBracket - startBracket - 1)
                               : "";
            QColor bg;
            if (note == "close")      bg = QColor(169,169,169);
            else if (note == "init")  bg = QColor(211,211,211);
            else if (note.contains("recording")) bg = Qt::red;
            else if (note.contains("stream") && note.contains("preview")) bg = QColor(255,165,0);
            else if (note.contains("stream"))    bg = Qt::green;
            else if (note.contains("preview"))   bg = Qt::yellow;
            else if (note.contains("snapshot"))  bg = Qt::cyan;
            else                                 bg = Qt::white;
            item->setBackground(bg);
            cameraEventList->addItem(item);

            EventItem camEv;
            camEv.lineNumber = ev.lineNumber;
            camEv.display    = ev.display;
            camEv.block      = currentBlock;
            camEv.timestamp  = ev.timestamp;
            camEv.bgColor    = bg;
            cameraEvents.push_back(camEv);

        } else if (ev.eventCategory == "heartbeat") {
            QListWidgetItem *item = new QListWidgetItem(ev.display);
            item->setBackground(QColor(255, 182, 193));
            heartbeatLostEventList->addItem(item);

            EventItem stEv;
            stEv.lineNumber = ev.lineNumber;
            stEv.display    = ev.display;
            stEv.block      = currentBlock;
            stEv.timestamp  = ev.timestamp;
            statusEvents.push_back(stEv);

        } else {
            // "main" 事件 — 直接填充，确保 block 在 setPlainText 之后赋值
            int colorIndex = ev.triggerCount % kMainBgColors.size();
            QListWidgetItem *item = new QListWidgetItem(ev.display);
            item->setBackground(kMainBgColors[colorIndex]);
            eventList->addItem(item);

            EventItem mainEv;
            mainEv.lineNumber = ev.lineNumber;
            mainEv.display    = ev.display;
            mainEv.block      = currentBlock;
            mainEv.bgColor    = kMainBgColors[colorIndex];
            allEvents.push_back(mainEv);

            if (eventList->count() == 1) {
                m_sideBar->showPanel(1);
            }
        }
    }

    // 三个列表批量 addItem 完毕，恢复绘制
    eventList->setUpdatesEnabled(true);
    cameraEventList->setUpdatesEnabled(true);
    heartbeatLostEventList->setUpdatesEnabled(true);

    // -------- 高亮与 UI 更新 --------
    reportStage(tr("应用事件高亮..."));
    highlightAllEvents();
    // 高亮过程通过 setCharFormat 改变了文档状态，这里把“已修改”位归零，
    // 使得用户真正编辑时 modificationChanged(true) 才会触发
    logView->document()->setModified(false);
    logView->setUpdatesEnabled(true);

    // -------- 填充事件时间轴 --------
    if (eventTimeline) {
        QList<TimelineEvent> tlEvents;
        for (const auto &ev : cameraEvents) {
            TimelineEvent te;
            te.lineNumber = ev.lineNumber;
            te.timestamp  = ev.timestamp;
            te.display    = ev.display;
            te.category   = "camera";
            // 从 display 中提取 note（格式："%1 | [%2] ..."）
            int s = ev.display.indexOf('[');
            int e = ev.display.indexOf(']', s);
            te.note = (s >= 0 && e > s) ? ev.display.mid(s + 1, e - s - 1) : "";
            tlEvents.append(te);
        }
        for (const auto &ev : statusEvents) {
            TimelineEvent te;
            te.lineNumber = ev.lineNumber;
            te.timestamp  = ev.timestamp;
            te.display    = ev.display;
            te.category   = "heartbeat";
            te.note       = "";
            tlEvents.append(te);
        }
        // 按时间戳（有效则按时间，否则按行号）排序
        std::sort(tlEvents.begin(), tlEvents.end(),
                  [](const TimelineEvent &a, const TimelineEvent &b) {
                      if (a.timestamp.isValid() && b.timestamp.isValid())
                          return a.timestamp < b.timestamp;
                      return a.lineNumber < b.lineNumber;
                  });
        eventTimeline->setEvents(tlEvents);
    }

    titleLabel->setText(QString("心跳丢失次数:%1").arg(heartbeatLostEventList->count()));

    // 状态面板：仅 CE 标签才更新图表
    bool isCE = (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
                    ? m_tabStates[m_currentTabIndex].isControlEngine : false;
    if (isCE) {
        reportStage(tr("绘制图表（电池/温度/SOC）..."));
        batteryChart->setData(batteryinfo);
        cameraTempChart->setData(cameraTemps);
        socChart->clear();
        socChart->addData(soctmp);
    }
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3")
                       .arg(sn.isEmpty() ? QString("-") : sn)
                       .arg(triggerCount)
                       .arg(flightCount));

    // 更新标签栏文字（使用 SN 或 sourcePath 来命名）
    if (m_tabBar && m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size()) {
        const QString &sp = m_tabStates[m_currentTabIndex].sourcePath;
        QString label;
        if (isCE) {
            label = "control_engine";
        } else {
            label = sp.isEmpty() ? "标签" : QFileInfo(sp).fileName();
        }
        m_tabBar->setTabText(m_currentTabIndex, label);
    }

    // 解析 top_log（这些文件通常较小，同步即可）
    if (!m_pendingTopLogs.isEmpty())
        reportStage(tr("解析 top_log..."));
    for (const QString &fp : m_pendingTopLogs)
        parseTopFile(fp);
    if (isCE) usageChart->setData(allusage);
    m_pendingTopLogs.clear();

    // 全部完成：标记进度窗口为完成状态并自动关闭
    if (m_parseProgressDialog) {
        m_parseProgressDialog->markFinished(tr("全部完成"));
        m_parseProgressDialog->close();
    }

    // statusPathLabel intentionally left unchanged — path was set before parsing started
}

void PressAnalyzer::loadAndAnalyzeLog()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择日志文件", "", "日志文件 (*.txt *.log *.hlog);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(filePath));

    // 更新当前标签的 sourcePath（onParseFinished 用它命名标签）
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].sourcePath = filePath;

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    if (m_sideBar->currentPanelIndex() == 1) m_sideBar->hidePanel();  // 隐藏分析结果面板
    heartbeatLostEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    searchResults.clear();
    searchResultView->clearResults();
    batteryinfo.clear();
    cameraTemps.clear();
    allusage.clear();
    soctmp.clear();
    triggerCount = 0;
    flightCount = 0;

    // 查找 top 文件路径（单文件模式）
    auto getTopFilePath = [](const QString &selectedFilePath) -> QString {
        QFileInfo fi(selectedFilePath);
        QString topPath = fi.absolutePath() + "/../system_log/top_log/top.1.log";
        topPath = QFileInfo(topPath).canonicalFilePath();
        return topPath;
    };
    QString topFilePath = getTopFilePath(filePath);
    m_pendingTopLogs.clear();
    if (!topFilePath.isEmpty() && QFileInfo::exists(topFilePath))
        m_pendingTopLogs << topFilePath;

    // ---- 启动后台解析线程 ----
    m_parseWorker = new LogParserWorker();
    m_parseWorker->setFiles(QStringList() << filePath);
    m_parseWorker->setVersion(version);

    m_parseThread = new QThread();
    ++m_parseGeneration;
    startBackgroundParse(m_parseWorker, m_parseThread, m_parseGeneration);
}

void PressAnalyzer::markTabModified(bool mod)
{
    if (m_currentTabIndex < 0 || m_currentTabIndex >= m_tabStates.size()) return;
    TabState &ts = m_tabStates[m_currentTabIndex];
    if (ts.modified == mod) return;
    ts.modified = mod;

    if (!m_tabBar) return;
    QString label = m_tabBar->tabText(m_currentTabIndex);
    if (mod) {
        if (!label.startsWith("* "))
            m_tabBar->setTabText(m_currentTabIndex, "* " + label);
    } else {
        if (label.startsWith("* "))
            m_tabBar->setTabText(m_currentTabIndex, label.mid(2));
    }
}
