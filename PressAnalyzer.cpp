#include "PressAnalyzer.h"
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QMessageBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QColor>
#include <QTextBlock>
#include <QRegExp>
#include <QRegularExpression>
#include <QDebug>
#include <QProcess>
#include <QCheckBox>
#include <QCompleter>
#include <QStringListModel>
#include <QShortcut>
#include <QKeyEvent>
#include <QTimer>
#include <QMenuBar>
#include <QAction>
#include <QInputDialog>
#include <QMap>
#include <QApplication>
#include <QThread>
#include <QEventLoop>
#include <QProgressBar>
#include <QScrollBar>
#include <QChar>
#include <functional>
#include <algorithm>
#include "LogNumberHighlighter.h"
#include <QListView>
#include <QFontMetrics>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QMenu>
#include <QStyle>
#include <QDirIterator>
#include <QTabWidget>
#include <QFontDialog>
#include "HLogBinaryParser.h"
#include <QHeaderView>
#include <QSqlQuery>
#include <QSqlError>
#include <QStyledItemDelegate>
#include <QClipboard>
#include <QScrollArea>
#include <QSet>
#include <QDialog>

// 根据可用宽度自动省略路径，从左侧省略以保留末尾目录名
class ElidedPathLabel : public QLabel {
public:
    explicit ElidedPathLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    void setFullText(const QString &text) {
        m_fullText = text;
        updateElidedText();
        setToolTip(text);
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QLabel::resizeEvent(e);
        updateElidedText();
    }

private:
    void updateElidedText() {
        QFontMetrics fm(font());
        QString elided = fm.elidedText(m_fullText, Qt::ElideLeft, width() - 8);
        setText(elided);
    }

    QString m_fullText;
};

// ==================== 颜色标记：Notepad++ 风格5色 ====================
const QColor PressAnalyzer::s_markColors[5] = {
    QColor(  0, 180, 255),   // 0: 颜色1  深天蓝  (对比搜索浅黄/浅绿)
    QColor(255,  80,   0),   // 1: 颜色2  深橙红  (对比搜索浅蓝/浅粉)
    QColor(180,   0, 220),   // 2: 颜色3  深紫    (对比搜索浅黄/浅绿)
    QColor(  0, 180,  60),   // 3: 颜色4  深绿    (对比搜索浅粉/浅蓝)
    QColor(160,  80,   0),   // 4: 颜色5  深棕    (对比搜索浅色系，与其他4色差异大)
};

// 前向声明：定义在本文件下方的 static helper
static void startBackgroundParseHelper(PressAnalyzer *self,
                                        LogParserWorker *worker,
                                        QThread *thread);

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
    m_colorMarkHighlights.clear();
    updateVisibleHighlights();
    searchResultView->setColorMarkPatterns({});
}

void PressAnalyzer::rebuildColorMarkSelections()
{
    m_colorMarkHighlights.clear();
    m_searchViewColorHighlights.clear();
    if (m_colorMarks.isEmpty()) {
        updateVisibleHighlights();
        return;
    }

    const QString logText = logView->toPlainText();
    QMap<QString, int> marks = m_colorMarks;

    using Hit = std::tuple<int,int,int>;
    QFuture<QList<Hit>> future = QtConcurrent::run(
        [logText, marks]() -> QList<Hit> {
            QList<Hit> hits;
            for (auto it = marks.constBegin(); it != marks.constEnd(); ++it) {
                const QString &kw = it.key();
                int colorIdx = it.value();
                if (kw.isEmpty()) continue;
                int pos = 0;
                while ((pos = logText.indexOf(kw, pos, Qt::CaseSensitive)) != -1) {
                    hits.append({pos, kw.length(), colorIdx});
                    pos += kw.length();
                }
            }
            return hits;
        }
    );

    auto *watcher = new QFutureWatcher<QList<Hit>>(this);
    connect(watcher, &QFutureWatcher<QList<Hit>>::finished, this,
        [this, watcher](){
            using Hit = std::tuple<int,int,int>;
            const auto &hits = watcher->result();
            watcher->deleteLater();

            QList<QTextEdit::ExtraSelection> sels;
            for (const auto &h : hits) {
                int pos  = std::get<0>(h);
                int len  = std::get<1>(h);
                int cidx = std::get<2>(h);
                QTextCursor c(logView->document());
                c.setPosition(pos);
                c.setPosition(pos + len, QTextCursor::KeepAnchor);
                QTextEdit::ExtraSelection sel;
                sel.cursor = c;
                QTextCharFormat fmt;
                fmt.setBackground(s_markColors[cidx]);
                fmt.setForeground(Qt::white);  // 深色背景配白色文字，对比度更高
                sel.format = fmt;
                sels.append(sel);
            }
            m_colorMarkHighlights = sels;
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
    // 颜色标记高亮（全文，置于最底层，先加入）
    QList<QTextEdit::ExtraSelection> allSelections = m_colorMarkHighlights + selections;
    logView->setExtraSelections(allSelections);
}

bool PressAnalyzer::extractZipFile(const QString &zipPath, const QString &extractDir)
{
    if (!QFileInfo::exists(zipPath)) {
        qWarning() << "ERROR: zipPath does not exist!";
        return false;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels); // 合并输出，避免阻塞

#ifdef Q_OS_WIN
    QStringList args;
    args << "-xf" << zipPath << "-C" << extractDir;
    process.start("tar.exe", args);
#else
    // Linux/macOS: 使用unzip命令
    QStringList args;
    args << "-o" << "-q" << zipPath << "-d" << extractDir; // -q 静默模式，提高性能
    process.start("unzip", args);
#endif

    bool hasFiles = false;
    while (!process.waitForFinished(50)) {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        // 实时检查解压目录，提前判断成功
        QDir extractDirObj(extractDir);
        if (extractDirObj.exists()) {
            QStringList files = extractDirObj.entryList(QDir::Files | QDir::NoDotAndDotDot);
            if (!files.isEmpty()) {
                hasFiles = true;
            }
        }
    }

    if (hasFiles) {
        return true;
    }
    QThread::msleep(20);
    bool success = false;
    QDir extractDirObj(extractDir);
    if (extractDirObj.exists()) {
        QStringList files = extractDirObj.entryList(QDir::Files | QDir::NoDotAndDotDot);
        success = !files.isEmpty();
    }

    if (!success) {
        QByteArray output = process.readAllStandardOutput();
        qWarning() << "ERROR: extract failed, exit code:" << process.exitCode();
    }

    return success;
}

bool PressAnalyzer::waitForFile(const QString &filePath, int maxWaitMs)
{
    if (QFileInfo::exists(filePath)) {
        return true;
    }

    int waited = 0;
    const int checkInterval = 50; // 每50ms检查一次

    while (waited < maxWaitMs) {
        QThread::msleep(checkInterval);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        waited += checkInterval;

        if (QFileInfo::exists(filePath)) {
            return true;
        }
    }

    return false;
}

PressAnalyzer::PressAnalyzer(QWidget *parent)
    : QMainWindow(parent), currentSearchIndex(-1),
      fileBrowserDock(nullptr), fileBrowserTree(nullptr), fileSystemModel(nullptr), fileBrowserButton(nullptr)
{
    // 注册跨线程 signal/slot 所需的自定义类型
    qRegisterMetaType<ParseResult>("ParseResult");

    // 初始化成员变量
    triggerCount = 0;
    flightCount = 0;

    // 初始化全局按钮点击状态
    anyFileButtonClicked = false;

    // 从 QSettings 加载字体设置
    QSettings settings("ZZTools", "HoverLogAnalyzer");

    // 加载日志字体
    currentLogFont = QFont("Menlo", 11);
    currentLogFont.setStyleHint(QFont::Monospace);
    currentLogFont.setFixedPitch(true);
    if (settings.contains("fonts/logFont")) {
        currentLogFont = settings.value("fonts/logFont").value<QFont>();
    }

    // 加载事件列表字体
    currentEventFont = QFont("Courier New", 11);
    if (settings.contains("fonts/eventFont")) {
        currentEventFont = settings.value("fonts/eventFont").value<QFont>();
    }

    // 加载图表字体
    currentChartFont = QFont("Arial", 12);
    if (settings.contains("fonts/chartFont")) {
        currentChartFont = settings.value("fonts/chartFont").value<QFont>();
    }

    // 按顺序初始化各个组件
    setupMainWindow();
    setupCentralWidget();
    setupEventDock();
    setupSearchDock();
    setupFileBrowserDock();
    setupToolBar();
    setupStatusBar();
    setupStatusDock();
    setupDbViewerDock();
    setupMenuBar();
    setupUsageContainer();
    setupConnections();

    // 初始化后应用加载的字体到各个控件
    applySavedFonts();

    // 颜色标记防抖 timer（单次触发，80ms 后执行全文扫描）
    m_colorMarkRebuildTimer = new QTimer(this);
    m_colorMarkRebuildTimer->setSingleShot(true);
    connect(m_colorMarkRebuildTimer, &QTimer::timeout, this, &PressAnalyzer::rebuildColorMarkSelections);

}

// removed dynamic width adjustment

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

// ==================== 私有初始化方法 ====================

void PressAnalyzer::setupMainWindow()
{
    setWindowTitle("Hover日志分析助手");
    setWindowIcon(QIcon(":/new/image/logo.icns"));
    resize(1200, 700);

    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::South);

    // 撤销自定义居中标题栏
}
// 撤销自定义更新接口，保持系统默认标题行为

void PressAnalyzer::setupCentralWidget()
{
    centralStack = new QStackedWidget(this);
    setCentralWidget(centralStack);

    // Page 0: 日志视图
    logView = new QPlainTextEdit(this);
    centralStack->addWidget(logView);  // index 0

    new LogNumberHighlighter(logView->document(), 7);

    QFont f("Menlo");
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPointSize(11);
    logView->setFont(f);
    logView->setStyleSheet(
        "QPlainTextEdit{selection-background-color:#80BFFF; selection-color:white;}"
    );

    // ---- logView 右键菜单（含颜色标记） ----
    logView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(logView, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint &pos){
        QMenu *menu = new QMenu(logView);
        // 统一菜单行高（一级菜单无图标，宽度按文字自适应）
        const QString menuQss =
            "QMenu {"
            "  font-size: 13px;"
            "  padding: 4px 0px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 8px 6px 20px;"
            "  min-width: 100px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #D2E3FC;"
            "  color: #1A1A1A;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: #DDDDDD;"
            "  margin: 3px 8px;"
            "}";
        menu->setStyleSheet(menuQss);

        // ---- 标准编辑操作（中文） ----
        QAction *actUndo  = menu->addAction("撤销");
        actUndo->setShortcut(QKeySequence::Undo);
        actUndo->setEnabled(logView->document()->isUndoAvailable());
        QAction *actRedo  = menu->addAction("重做");
        actRedo->setShortcut(QKeySequence::Redo);
        actRedo->setEnabled(logView->document()->isRedoAvailable());
        menu->addSeparator();
        QAction *actCut   = menu->addAction("剪切");
        actCut->setShortcut(QKeySequence::Cut);
        actCut->setEnabled(logView->textCursor().hasSelection());
        QAction *actCopy  = menu->addAction("拷贝");
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(logView->textCursor().hasSelection());
        QAction *actPaste = menu->addAction("粘贴");
        actPaste->setShortcut(QKeySequence::Paste);
        actPaste->setEnabled(!QApplication::clipboard()->text().isEmpty());
        QAction *actDelete = menu->addAction("删除");
        actDelete->setEnabled(logView->textCursor().hasSelection());
        menu->addSeparator();
        QAction *actSelectAll = menu->addAction("全选");
        actSelectAll->setShortcut(QKeySequence::SelectAll);
        menu->addSeparator();

        connect(actUndo,      &QAction::triggered, logView, &QPlainTextEdit::undo);
        connect(actRedo,      &QAction::triggered, logView, &QPlainTextEdit::redo);
        connect(actCut,       &QAction::triggered, logView, &QPlainTextEdit::cut);
        connect(actCopy,      &QAction::triggered, logView, &QPlainTextEdit::copy);
        connect(actPaste,     &QAction::triggered, logView, &QPlainTextEdit::paste);
        connect(actDelete,    &QAction::triggered, logView, [this](){ logView->textCursor().removeSelectedText(); });
        connect(actSelectAll, &QAction::triggered, logView, &QPlainTextEdit::selectAll);

        // ---- 颜色标记子菜单（QWidgetAction 自定义布局，完全控制色块大小和间距） ----
        const QString subMenuQss =
            "QMenu {"
            "  font-size: 13px;"
            "  padding: 2px 0px;"
            "}"
            "QMenu::item {"
            "  padding: 0px 0px 0px 0px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: transparent;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: #DDDDDD;"
            "  margin: 2px 6px;"
            "}";
        QMenu *markMenu = menu->addMenu("颜色标记");
        markMenu->setStyleSheet(subMenuQss);
        markMenu->setMinimumWidth(120);
        struct ColorInfo { QString name; QColor color; };
        const ColorInfo infos[5] = {
            {"颜色 1", s_markColors[0]},
            {"颜色 2", s_markColors[1]},
            {"颜色 3", s_markColors[2]},
            {"颜色 4", s_markColors[3]},
            {"颜色 5", s_markColors[4]},
        };
        for (int i = 0; i < 5; ++i) {
            QWidgetAction *wa = new QWidgetAction(markMenu);
            QWidget *row = new QWidget();
            row->setFixedHeight(24);
            row->setObjectName("colorRow");
            // 悬停高亮
            row->setStyleSheet(
                "QWidget#colorRow { background: transparent; }"
                "QWidget#colorRow:hover { background: #D2E3FC; }"
            );
            row->setAttribute(Qt::WA_Hover, true);

            QHBoxLayout *hl = new QHBoxLayout(row);
            hl->setContentsMargins(6, 0, 12, 0);
            hl->setSpacing(6);

            // 色块 label
            QLabel *colorBox = new QLabel();
            colorBox->setFixedSize(18, 18);
            colorBox->setStyleSheet(QString(
                "background-color: %1;"
                "border: 1px solid rgba(0,0,0,80);"
            ).arg(infos[i].color.name()));
            colorBox->setAttribute(Qt::WA_TransparentForMouseEvents, true);

            QLabel *textLabel = new QLabel(infos[i].name);
            textLabel->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

            hl->addWidget(colorBox);
            hl->addWidget(textLabel);
            hl->addStretch();

            wa->setDefaultWidget(row);
            markMenu->addAction(wa);
            connect(wa, &QWidgetAction::triggered, this, [this, i](){
                QString kw = logView->textCursor().selectedText().trimmed();
                applyLineColorMark(i, kw);
            });
        }
        markMenu->addSeparator();

        // 清除选择标记
        QWidgetAction *waClearSel = new QWidgetAction(markMenu);
        QWidget *rowClearSel = new QWidget();
        rowClearSel->setFixedHeight(22);
        rowClearSel->setObjectName("colorRow");
        rowClearSel->setStyleSheet(
            "QWidget#colorRow { background: transparent; }"
            "QWidget#colorRow:hover { background: #D2E3FC; }"
        );
        rowClearSel->setAttribute(Qt::WA_Hover, true);
        {
            QHBoxLayout *hl = new QHBoxLayout(rowClearSel);
            hl->setContentsMargins(6, 0, 12, 0);
            QLabel *lbl = new QLabel("清除选择标记");
            lbl->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            hl->addWidget(lbl);
            hl->addStretch();
        }
        waClearSel->setDefaultWidget(rowClearSel);
        markMenu->addAction(waClearSel);
        connect(waClearSel, &QWidgetAction::triggered, this, [this](){
            QString sel = logView->textCursor().selectedText().trimmed();
            if (!sel.isEmpty()) clearColorMarkByText(sel);
        });

        // 清除全部标记
        QWidgetAction *waClearAll = new QWidgetAction(markMenu);
        QWidget *rowClearAll = new QWidget();
        rowClearAll->setFixedHeight(22);
        rowClearAll->setObjectName("colorRow");
        rowClearAll->setStyleSheet(
            "QWidget#colorRow { background: transparent; }"
            "QWidget#colorRow:hover { background: #D2E3FC; }"
        );
        rowClearAll->setAttribute(Qt::WA_Hover, true);
        {
            QHBoxLayout *hl = new QHBoxLayout(rowClearAll);
            hl->setContentsMargins(6, 0, 12, 0);
            QLabel *lbl = new QLabel("清除全部标记");
            lbl->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            hl->addWidget(lbl);
            hl->addStretch();
        }
        waClearAll->setDefaultWidget(rowClearAll);
        markMenu->addAction(waClearAll);
        connect(waClearAll, &QWidgetAction::triggered, this, &PressAnalyzer::clearAllColorMarks);

        menu->exec(logView->mapToGlobal(pos));
        delete menu;
    });

    // Page 1: DB 查看器（由 setupDbViewerDock() 填充后加入）
    dbViewerWidget = new QWidget(this);
    centralStack->addWidget(dbViewerWidget);  // index 1

    centralStack->setCurrentIndex(0);  // 默认显示日志视图
}

void PressAnalyzer::setupEventDock()
{
    eventList = new QListWidget(this);
    eventDock = new QDockWidget("分析结果", this);
    eventDock->setWidget(eventList);
    eventDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    eventDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, eventDock);
    eventDock->hide();
}

void PressAnalyzer::setupSearchDock()
{
    searchResultView = new SearchResultTextView(this);
    searchResultView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 初次创建时使用与 logView 相同的字体
    if (logView) {
        searchResultView->setFont(logView->font());
    }

    searchDock = new QDockWidget("查找结果", this);
    searchDock->setWidget(searchResultView);
    searchDock->setMinimumHeight(150);
    addDockWidget(Qt::BottomDockWidgetArea, searchDock);
    searchDock->hide();

    // 设置右键菜单
    searchDock->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(searchDock, &QDockWidget::customContextMenuRequested, this, [=](const QPoint &pos){
        QMenu menu;
        QAction *clearAction = menu.addAction("Clear");
        QAction *closeAction = menu.addAction("Close");

        QAction *selected = menu.exec(searchDock->mapToGlobal(pos));
        if (selected == clearAction) {
            searchResults.clear();
            currentSearchIndex = 0;
            searchResultView->clearResults();
            searchHighlights.clear();
            updateVisibleHighlights();  // 清空搜索结果，但保留颜色标记
            searchDock->setWindowTitle("查找结果");
        } else if (selected == closeAction) {
            searchDock->hide();
        }
    });

    // 在搜索结果视图内右键菜单：拷贝 / 全选 / 清空 / 关闭
    searchResultView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(searchResultView, &SearchResultTextView::customContextMenuRequested, this, [=](const QPoint &pos){
        QMenu *menu = new QMenu(searchResultView);
        const QString menuQss =
            "QMenu {"
            "  font-size: 13px;"
            "  padding: 4px 0px;"
            "}"
            "QMenu::item {"
            "  padding: 6px 20px 6px 16px;"
            "  min-width: 140px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #D2E3FC;"
            "  color: #1A1A1A;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: #DDDDDD;"
            "  margin: 3px 8px;"
            "}";
        menu->setStyleSheet(menuQss);

        QAction *actCopy = menu->addAction("拷贝");
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(searchResultView->textCursor().hasSelection());
        QAction *actSelectAll = menu->addAction("全选");
        actSelectAll->setShortcut(QKeySequence::SelectAll);
        menu->addSeparator();

        connect(actCopy,      &QAction::triggered, searchResultView, &SearchResultTextView::copy);
        connect(actSelectAll, &QAction::triggered, searchResultView, &SearchResultTextView::selectAll);

        // ---- 颜色标记子菜单 ----
        menu->addSeparator();
        const QString subMenuQss2 =
            "QMenu { font-size: 13px; padding: 2px 0px; }"
            "QMenu::item { padding: 0px 0px 0px 0px; }"
            "QMenu::item:selected { background-color: transparent; }"
            "QMenu::separator { height: 1px; background: #DDDDDD; margin: 2px 6px; }";
        QMenu *markMenu2 = menu->addMenu("颜色标记");
        markMenu2->setStyleSheet(subMenuQss2);
        markMenu2->setMinimumWidth(120);
        struct ColorInfo2 { QString name; QColor color; };
        const ColorInfo2 infos2[5] = {
            {"颜色 1", s_markColors[0]},
            {"颜色 2", s_markColors[1]},
            {"颜色 3", s_markColors[2]},
            {"颜色 4", s_markColors[3]},
            {"颜色 5", s_markColors[4]},
        };
        for (int i = 0; i < 5; ++i) {
            QWidgetAction *wa = new QWidgetAction(markMenu2);
            QWidget *row = new QWidget();
            row->setFixedHeight(24);
            row->setObjectName("colorRow");
            row->setStyleSheet(
                "QWidget#colorRow { background: transparent; }"
                "QWidget#colorRow:hover { background: #D2E3FC; }"
            );
            row->setAttribute(Qt::WA_Hover, true);
            QHBoxLayout *hl = new QHBoxLayout(row);
            hl->setContentsMargins(6, 0, 12, 0);
            hl->setSpacing(6);
            QLabel *colorBox = new QLabel();
            colorBox->setFixedSize(18, 18);
            colorBox->setStyleSheet(QString(
                "background-color: %1; border: 1px solid rgba(0,0,0,80);"
            ).arg(infos2[i].color.name()));
            colorBox->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            QLabel *textLabel = new QLabel(infos2[i].name);
            textLabel->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            textLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            hl->addWidget(colorBox);
            hl->addWidget(textLabel);
            hl->addStretch();
            wa->setDefaultWidget(row);
            markMenu2->addAction(wa);
            connect(wa, &QWidgetAction::triggered, this, [this, i](){
                QString kw = searchResultView->textCursor().selectedText().trimmed();
                applyLineColorMark(i, kw);
            });
        }
        markMenu2->addSeparator();
        QWidgetAction *waClearSel2 = new QWidgetAction(markMenu2);
        QWidget *rowClearSel2 = new QWidget();
        rowClearSel2->setFixedHeight(22);
        rowClearSel2->setObjectName("colorRow");
        rowClearSel2->setStyleSheet(
            "QWidget#colorRow { background: transparent; }"
            "QWidget#colorRow:hover { background: #D2E3FC; }"
        );
        rowClearSel2->setAttribute(Qt::WA_Hover, true);
        {
            QHBoxLayout *hl = new QHBoxLayout(rowClearSel2);
            hl->setContentsMargins(6, 0, 12, 0);
            QLabel *lbl = new QLabel("清除选择标记");
            lbl->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            hl->addWidget(lbl); hl->addStretch();
        }
        waClearSel2->setDefaultWidget(rowClearSel2);
        markMenu2->addAction(waClearSel2);
        connect(waClearSel2, &QWidgetAction::triggered, this, [this](){
            QString sel = searchResultView->textCursor().selectedText().trimmed();
            if (!sel.isEmpty()) clearColorMarkByText(sel);
        });
        QWidgetAction *waClearAll2 = new QWidgetAction(markMenu2);
        QWidget *rowClearAll2 = new QWidget();
        rowClearAll2->setFixedHeight(22);
        rowClearAll2->setObjectName("colorRow");
        rowClearAll2->setStyleSheet(
            "QWidget#colorRow { background: transparent; }"
            "QWidget#colorRow:hover { background: #D2E3FC; }"
        );
        rowClearAll2->setAttribute(Qt::WA_Hover, true);
        {
            QHBoxLayout *hl = new QHBoxLayout(rowClearAll2);
            hl->setContentsMargins(6, 0, 12, 0);
            QLabel *lbl = new QLabel("清除全部标记");
            lbl->setStyleSheet("font-size: 13px; color: #1A1A1A; background: transparent;");
            lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            hl->addWidget(lbl); hl->addStretch();
        }
        waClearAll2->setDefaultWidget(rowClearAll2);
        markMenu2->addAction(waClearAll2);
        connect(waClearAll2, &QWidgetAction::triggered, this, &PressAnalyzer::clearAllColorMarks);
        menu->addSeparator();

        QAction *clearAction = menu->addAction("清空搜索结果");
        QAction *closeAction = menu->addAction("关闭面板");

        QAction *selected = menu->exec(searchResultView->mapToGlobal(pos));
        if (selected == clearAction) {
            searchResults.clear();
            currentSearchIndex = 0;
            searchResultView->clearResults();
            searchHighlights.clear();
            updateVisibleHighlights();  // 清空搜索结果，但保留颜色标记
            searchDock->setWindowTitle("查找结果");
        } else if (selected == closeAction) {
            searchDock->hide();
        }
        delete menu;
    });
}

void PressAnalyzer::setupFileBrowserDock()
{
    // 创建文件系统模型
    fileSystemModel = new QFileSystemModel(this);
    fileSystemModel->setReadOnly(true);
    // 显示所有文件，不过滤
    fileSystemModel->setNameFilterDisables(false);

    // 创建树形视图
    fileBrowserTree = new QTreeView(this);
    fileBrowserTree->setModel(fileSystemModel);
    fileBrowserTree->setAnimated(true);
    fileBrowserTree->setIndentation(16);
    fileBrowserTree->setSortingEnabled(true);
    fileBrowserTree->sortByColumn(0, Qt::AscendingOrder);

    // 只显示名称和大小列，隐藏类型和修改日期
    fileBrowserTree->setColumnHidden(2, true); // Type
    fileBrowserTree->setColumnHidden(3, true); // Date Modified

    // 设置列宽
    fileBrowserTree->setColumnWidth(0, 250); // Name
    fileBrowserTree->setColumnWidth(1, 80);  // Size

    // 设置头部
    fileBrowserTree->header()->setStretchLastSection(false);
    fileBrowserTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    // 设置样式 - 移除箭头图标
    fileBrowserTree->setStyleSheet(
        "QTreeView {"
        "  border: none;"
        "  background-color: #FAFAFA;"
        "  font-size: 12px;"
        "  show-decoration-selected: 0;"
        "}"
        "QTreeView::item {"
        "  padding: 3px 4px;"
        "  border-radius: 3px;"
        "}"
        "QTreeView::item:hover {"
        "  background-color: #E8F0FE;"
        "}"
        "QTreeView::item:selected {"
        "  background-color: #D2E3FC;"
        "  color: #1A73E8;"
        "}"
        "QHeaderView::section {"
        "  background-color: #F0F0F0;"
        "  border: none;"
        "  border-bottom: 1px solid #D0D0D0;"
        "  padding: 4px 6px;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
        "QTreeView::branch {"
        "  background: transparent;"
        "}"
        "QTreeView::branch:has-children:!has-siblings:closed,"
        "QTreeView::branch:closed:has-children:has-siblings {"
        "  border-image: none;"
        "  background: transparent;"
        "}"
        "QTreeView::branch:open:has-children:!has-siblings,"
        "QTreeView::branch:open:has-children:has-siblings {"
        "  border-image: none;"
        "  background: transparent;"
        "}"
    );

    // 创建容器布局（带路径栏和导航按钮）
    QWidget *browserContainer = new QWidget(this);
    QVBoxLayout *browserLayout = new QVBoxLayout(browserContainer);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(2);

    // 顶部导航栏：返回上级 + 当前路径 + 选择目录
    QHBoxLayout *navLayout = new QHBoxLayout();
    navLayout->setContentsMargins(4, 4, 4, 2);
    navLayout->setSpacing(4);

    QPushButton *goUpButton = new QPushButton(this);
    goUpButton->setIcon(QIcon(":/icons/icons/dir-arrow.png"));
    goUpButton->setToolTip("返回上级目录");
    goUpButton->setFixedSize(28, 28);
    goUpButton->setStyleSheet(
        "QPushButton{"
        "  background-color:#E3F2FD;"
        "  border:1px solid #90CAF9;"
        "  border-radius:4px;"
        "  padding:2px;"
        "}"
        "QPushButton:hover{ background-color:#BBDEFB; }"
        "QPushButton:pressed{ background-color:#90CAF9; }"
    );

    ElidedPathLabel *pathLabel = new ElidedPathLabel(this);
    pathLabel->setObjectName("fileBrowserPathLabel");
    pathLabel->setFullText("未选择目录");
    pathLabel->setStyleSheet(
        "QLabel {"
        "  color: #666666;"
        "  font-size: 11px;"
        "  padding: 2px 4px;"
        "  background-color: #F8F8F8;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 3px;"
        "}"
    );

    QPushButton *chooseDirButton = new QPushButton(this);
    chooseDirButton->setIcon(QIcon(":/icons/icons/analytics_ce.png"));
    chooseDirButton->setToolTip("智能解析Control Engine日志");
    chooseDirButton->setFixedSize(28, 28);
    chooseDirButton->setStyleSheet(
        "QPushButton{"
        "  background-color:#E8F5E8;"
        "  border:1px solid #81C784;"
        "  border-radius:4px;"
        "  padding:2px;"
        "}"
        "QPushButton:hover{ background-color:#C8E6C9; }"
        "QPushButton:pressed{ background-color:#81C784; }"
    );

    navLayout->addWidget(goUpButton);
    navLayout->addWidget(pathLabel, 1);
    navLayout->addWidget(chooseDirButton);
    browserLayout->addLayout(navLayout);
    browserLayout->addWidget(fileBrowserTree, 1);

    // 创建Dock - 放左侧
    fileBrowserDock = new QDockWidget("文件浏览器", this);
    fileBrowserDock->setWidget(browserContainer);
    fileBrowserDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    fileBrowserDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    fileBrowserDock->setMinimumWidth(150);
    addDockWidget(Qt::LeftDockWidgetArea, fileBrowserDock);
    fileBrowserDock->hide(); // 初始隐藏

    tabifyDockWidget(fileBrowserDock, eventDock);
    eventDock->raise();

    installEventFilter(this);

    // 返回上级按钮连接
    connect(goUpButton, &QPushButton::clicked, this, [this, pathLabel](){
        if (fileBrowserRootPath.isEmpty()) return;
        QDir dir(fileBrowserRootPath);
        if (dir.cdUp()) {
            openDirectoryInBrowser(dir.absolutePath());
        }
    });

    // 智能解析按钮连接
    connect(chooseDirButton, &QPushButton::clicked, this, [this](){
        if (fileBrowserRootPath.isEmpty()) {
            QMessageBox::information(this, "提示", "请先在文件浏览器中打开一个日志目录");
            return;
        }

        const QString analysisRoot = findControlEngineAnalysisRoot(fileBrowserRootPath);
        if (analysisRoot.isEmpty()) {
            QMessageBox::warning(this, "提示", "当前目录下未找到 control_engine_log 目录");
            return;
        }

        loadAndAnalyzeLogsFromPath(analysisRoot);
    });

    // 双击文件/目录处理
    connect(fileBrowserTree, &QTreeView::doubleClicked, this, &PressAnalyzer::onFileBrowserDoubleClicked);

    // 右键菜单
    fileBrowserTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileBrowserTree, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos){
        QModelIndex index = fileBrowserTree->indexAt(pos);
        if (!index.isValid()) return;

        QString filePath = fileSystemModel->filePath(index);
        QFileInfo info(filePath);

        const QString fileBrowserMenuQss =
            "QMenu {"
            "  font-size: 13px;"
            "  padding: 4px 0px;"
            "  min-width: 160px;"
            "}"
            "QMenu::item {"
            "  padding: 7px 24px 7px 16px;"
            "  min-width: 160px;"
            "}"
            "QMenu::item:selected {"
            "  background-color: #D2E3FC;"
            "  color: #1A1A1A;"
            "}"
            "QMenu::separator {"
            "  height: 1px;"
            "  background: #DDDDDD;"
            "  margin: 3px 8px;"
            "}";
        QMenu menu;
        menu.setStyleSheet(fileBrowserMenuQss);
        if (info.isDir()) {
            QAction *actDelete  = menu.addAction("删除目录");
            QAction *actRename  = menu.addAction("重命名目录");
            QAction *actOpen    = menu.addAction("打开目录");
            QAction *actLoadDir = menu.addAction("智能解析日志");
            QAction *chosen = menu.exec(fileBrowserTree->mapToGlobal(pos));
            if (chosen == actDelete) {
                QMessageBox::StandardButton reply =
                    QMessageBox::question(this, "确认删除",
                        QString("确定要删除目录 %1 吗？").arg(info.fileName()),
                        QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    QDir dir(filePath);
                    if (!dir.removeRecursively()) {
                        QMessageBox::warning(this, "删除失败", "无法删除目录，请检查权限。");
                    }
                }
            } else if (chosen == actRename) {
                QMessageBox::StandardButton reply =
                    QMessageBox::question(this, "确认重命名",
                        QString("确定要重命名目录 %1 吗？").arg(info.fileName()),
                        QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    if (index.isValid() && fileSystemModel->flags(index) & Qt::ItemIsEditable) {
                        fileBrowserTree->edit(index);
                    } else {
                        bool ok;
                        QString newName = QInputDialog::getText(this, "重命名目录",
                            QString("请输入目录 %1 的新名称：").arg(info.fileName()),
                            QLineEdit::Normal, info.fileName(), &ok);
                        if (ok && !newName.isEmpty()) {
                            QDir dir;
                            if (!dir.rename(filePath, info.absolutePath() + "/" + newName)) {
                                QMessageBox::warning(this, "重命名失败", "无法重命名目录，请检查权限及名称是否合法。");
                            }
                        }
                    }
                }
            } else if (chosen == actOpen) {
                openDirectoryInBrowser(filePath);
            } else if (chosen == actLoadDir) {
                loadMergeLogsFromPath(filePath, false);
            }
        } else {
            QAction *actOpen   = menu.addAction("打开文件");
            QAction *actDelete = menu.addAction("删除文件");
            QAction *actRename = menu.addAction("重命名文件");
            QAction *chosen = menu.exec(fileBrowserTree->mapToGlobal(pos));
            if (chosen == actOpen) {
                openFileFromBrowser(filePath);
            } else if (chosen == actDelete) {
                QMessageBox::StandardButton reply =
                    QMessageBox::question(this, "确认删除",
                        QString("确定要删除文件 %1 吗？").arg(info.fileName()),
                        QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    QFile file(filePath);
                    if (!file.remove()) {
                        QMessageBox::warning(this, "删除失败", "无法删除文件，请检查权限。");
                    }
                }
            } else if (chosen == actRename) {
                QMessageBox::StandardButton reply =
                    QMessageBox::question(this, "确认重命名",
                        QString("确定要重命名文件 %1 吗？").arg(info.fileName()),
                        QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    if (index.isValid() && fileSystemModel->flags(index) & Qt::ItemIsEditable) {
                        fileBrowserTree->edit(index);
                    } else {
                        bool ok;
                        QString newName = QInputDialog::getText(this, "重命名文件",
                            QString("请输入文件 %1 的新名称：").arg(info.fileName()),
                            QLineEdit::Normal, info.fileName(), &ok);
                        if (ok && !newName.isEmpty()) {
                            QFile file(filePath);
                            if (!file.rename(info.absolutePath() + "/" + newName)) {
                                QMessageBox::warning(this, "重命名失败", "无法重命名文件，请检查权限及名称是否合法。");
                            }
                        }
                    }
                }
            }
        }
    });
}

void PressAnalyzer::openDirectoryInBrowser(const QString &dirPath)
{
    fileBrowserRootPath = dirPath;
    fileSystemModel->setRootPath(dirPath);
    fileBrowserTree->setRootIndex(fileSystemModel->index(dirPath));

    // 更新路径标签：传入完整路径，由 ElidedPathLabel 根据宽度自动省略
    ElidedPathLabel *pathLabel = fileBrowserDock->findChild<ElidedPathLabel*>("fileBrowserPathLabel");
    if (pathLabel) {
        pathLabel->setFullText(dirPath);
    }

    // 展开根目录
    QModelIndex rootIndex = fileSystemModel->index(dirPath);
    fileBrowserTree->expand(rootIndex);

    // 折叠所有子目录，保持界面整洁
    int rowCount = fileSystemModel->rowCount(rootIndex);
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex childIndex = fileSystemModel->index(i, 0, rootIndex);
        fileBrowserTree->collapse(childIndex);
    }

    // 如果dock没有显示则显示
    if (!fileBrowserDock->isVisible()) {
        fileBrowserDock->show();
    }
}

void PressAnalyzer::onFileBrowserClicked(const QModelIndex &index)
{
    // 单击暂不处理，使用双击
}

void PressAnalyzer::onFileBrowserDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    QString filePath = fileSystemModel->filePath(index);
    QFileInfo info(filePath);

    if (info.isDir()) {
        // 双击目录：直接进入目录视图（浏览用）
        openDirectoryInBrowser(filePath);
    } else {
        // 双击文件：打开
        openFileFromBrowser(filePath, false); // 传入false表示不解压后进入目录
    }
}

void PressAnalyzer::openFileFromBrowser(const QString &filePath, bool enterExtractDir)
{
    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();

    if (suffix == "zip") {
        // 压缩包：自动解压到同名目录
        QString extractDir = info.absolutePath() + "/" + info.completeBaseName();
        QDir().mkpath(extractDir);

        if (statusPathLabel) statusPathLabel->setText(QString("正在解压: %1").arg(info.fileName()));
        QApplication::processEvents();

        bool ok = extractZipFile(filePath, extractDir);
        if (ok) {
            // 解压成功，根据参数决定是否进入解压后的目录视图
            if (enterExtractDir) {
                openDirectoryInBrowser(extractDir);
            } else {
                // 保持当前目录视图不变，显示解压成功消息
                if (statusPathLabel) statusPathLabel->setText(QString("解压完成: %1").arg(info.fileName()));
                QApplication::processEvents();
                QTimer::singleShot(1000, this, [this]() {
                    if (statusPathLabel) statusPathLabel->setText("就绪");
                });
            }
        } else {
            QMessageBox::warning(this, "解压失败", QString("无法解压文件: %1").arg(info.fileName()));
            if (statusPathLabel) statusPathLabel->setText("解压失败");
        }
    } else if (suffix == "log" || suffix == "txt" || suffix == "hlog" || suffix == "csv" || suffix == "ulg") {
        // 日志文件：直接加载
        if (statusPathLabel) statusPathLabel->setText(filePath);
        loadFileToLogView(filePath);
    } else if (suffix == "db" || suffix == "sqlite" || suffix == "sqlite3") {
        // SQLite 数据库：在 DB 查看器中打开
        openDatabaseFile(filePath);
    } else {
        // 其他文件类型（无扩展名等）：当作文本文件直接打开
        if (statusPathLabel) statusPathLabel->setText(filePath);
        loadFileToLogView(filePath);
    }
}

void PressAnalyzer::loadFileToLogView(const QString &filePath)
{
    // 切换到日志视图页
    centralStack->setCurrentIndex(0);

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(filePath));

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    eventDock->hide();
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
    // 清空颜色标记
    m_colorMarks.clear();
    m_colorMarkHighlights.clear();

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
    startBackgroundParseHelper(this, m_parseWorker, m_parseThread);
}

void PressAnalyzer::setupToolBar()
{
    QToolBar *toolBar = addToolBar("主工具栏");

    // 创建图标按钮
    analyzeControlButton = new QPushButton(this);
    analyzeControlButton->setIcon(QIcon(":/icons/icons/analysis.png"));
    analyzeControlButton->setToolTip("分析Control Engine日志(专用)");
    analyzeControlButton->setIconSize(QSize(20, 20));

    clearButton = new QPushButton(this);
    clearButton->setIcon(QIcon(":/icons/icons/clear.png"));
    clearButton->setToolTip("清除窗口");
    clearButton->setIconSize(QSize(20, 20));

    newWindowButton = new QPushButton(this);
    QIcon windowIcon(":/icons/icons/window-new.png");
    if (windowIcon.isNull() || windowIcon.pixmap(20, 20).isNull()) {
        newWindowButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    } else {
        newWindowButton->setIcon(windowIcon);
    }
    newWindowButton->setToolTip("新建窗口");
    newWindowButton->setIconSize(QSize(20, 20));

    searchAllButton = new QPushButton(this);
    searchAllButton->setIcon(QIcon(":/icons/icons/search.png"));
    searchAllButton->setToolTip("搜索");
    searchAllButton->setIconSize(QSize(20, 20));

    searchPrevButton = new QPushButton(this);
    searchPrevButton->setIcon(QIcon(":/icons/icons/arrow-up.png"));
    searchPrevButton->setToolTip("向前搜索");
    searchPrevButton->setIconSize(QSize(20, 20));

    searchNextButton = new QPushButton(this);
    searchNextButton->setIcon(QIcon(":/icons/icons/arrow-down.png"));
    searchNextButton->setToolTip("向后搜索");
    searchNextButton->setIconSize(QSize(20, 20));

    // 创建搜索下拉（可编辑），不点下拉也可直接输入
    searchCombo = new SearchComboBox(this);
    searchCombo->setEditable(true);
    searchCombo->setInsertPolicy(QComboBox::NoInsert);
    searchCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // 设置最小宽度，并允许随窗口变化自动扩展（Expanding）
    searchCombo->setMinimumWidth(50);
    searchCombo->setStyleSheet(
        "QComboBox {"
        "  min-height:30px;"
        "  border: 1px solid #CCCCCC;"
        "  border-radius: 8px;"
        "  background-color: white;"
        "}"
        "QComboBox::drop-down {"
        "  width: 26px;"
        "  border: 0px;"
        "}"
        "QComboBox::down-arrow {"
        "  image: url(:/icons/icons/down-arrow.png);"
        "  width: 14px; height: 14px;"
        "  margin-right: 6px;"
        "}"
        "QComboBox:hover {"
        "  border-color: #999999;"
        "}"
        "QComboBox:focus {"
        "  border-color: #4A90E2;"
        "  background-color: #F8F9FA;"
        "}"
    );
    // 视图在 showPopup() 时重建并应用样式，这里无需设置
    searchCombo->installEventFilter(this);
    // 获取内部编辑器，复用原有行为与样式
    searchEdit = searchCombo->lineEdit();
    if (searchEdit) {
        searchEdit->setMinimumWidth(50); // 与下拉框一致的最小宽度基线
        searchEdit->setPlaceholderText("输入搜索内容... ");
        // 直接设置控件字体，避免某些平台样式表对字体的忽略
        QFont seFont = searchEdit->font();
        seFont.setPointSize(12);
        searchEdit->setFont(seFont);
        // 同步设置给 QComboBox 本体，确保高度与布局计算一致
        QFont cbFont = searchCombo->font();
        cbFont.setPointSize(12);
        searchCombo->setFont(cbFont);
    }

    // 文件浏览器按钮
    fileBrowserButton = new QPushButton(this);
    fileBrowserButton->setIcon(QIcon(":/icons/icons/folder.png"));
    fileBrowserButton->setToolTip("文件浏览器");
    fileBrowserButton->setIconSize(QSize(20, 20));

    // 动态图表搜索按钮
    chartSearchButton = new QPushButton(this);
    chartSearchButton->setIcon(QIcon(":/icons/icons/motion-graphics.png"));
    chartSearchButton->setToolTip("动态图表搜索");
    chartSearchButton->setIconSize(QSize(20, 20));

    // 工具栏按钮顺序: 文件浏览器|分析ControlEngine|新开窗口|清除窗口
    toolBar->addWidget(fileBrowserButton);
    toolBar->addWidget(analyzeControlButton);
    toolBar->addWidget(newWindowButton);
    toolBar->addWidget(clearButton);
    toolBar->addSeparator();
    toolBar->addWidget(searchCombo);
    // 在搜索框后面紧跟搜索按钮
    toolBar->addWidget(searchAllButton);
    toolBar->addWidget(searchPrevButton);
    toolBar->addWidget(searchNextButton);

    // 设置搜索提示与下拉项目
    // 初始化固定提示词（内置）
    baseFixedHints.clear();
    baseFixedHints << "[rpc] Req:"
                   << "otaReportEvent"
                   << "MediaRequest_MediaRequestType_"
                   << "GET_MEDIA_FILE media_file_transfer_request";
    loadPinnedHints();
    rebuildFixedHints();
    setupSearchCompleter();
    // 准备一次性模型并绑定，后续仅清空并填充，避免偶发显示问题
    searchDropdownModel = new QStandardItemModel(searchCombo);
    searchCombo->setModel(searchDropdownModel);
    updateSearchDropdownItems();
    // 选择条目即触发文本更新
    connect(searchCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int){
        if (searchEdit) searchEdit->setText(searchCombo->currentText());
        updatePinButtonState();
    });
    // 打开下拉前强制刷新，修复偶现不显示提示
    connect(searchCombo, &SearchComboBox::aboutToShowPopup, this, [this](){
        updateSearchDropdownItems();
        if (searchEdit && searchEdit->completer() && searchEdit->completer()->popup()) {
            searchEdit->completer()->popup()->hide();
        }
    });
    // 右键菜单：固定/取消固定
    if (searchCombo->view()) {
        searchCombo->view()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(searchCombo->view(), &QListView::customContextMenuRequested, this, [this](const QPoint &p){ onDropdownContextMenu(p); });
    }
    // 在弹出真正显示后再获取 popup 实例并移动到正确屏幕
    connect(searchCombo, &SearchComboBox::popupShown, this, [this](){
        if (!searchCombo->view()) return;
        QWidget *popup = searchCombo->view()->window();
        if (!popup) return;
        QPoint belowLeft = searchCombo->mapToGlobal(QPoint(0, searchCombo->height()));
        QScreen *screen = QGuiApplication::screenAt(belowLeft);
        if (!screen) screen = QGuiApplication::primaryScreen();
        QRect sg = screen->availableGeometry();
        QPoint pos = belowLeft;
        int popupWidth = popup->sizeHint().width();
        if (popupWidth <= 0) popupWidth = searchCombo->width();
        if (pos.x() + popupWidth > sg.right()) pos.setX(qMax(sg.left(), sg.right() - popupWidth));
        int popupHeight = popup->sizeHint().height();
        if (popupHeight <= 0) popupHeight = 200;
        if (pos.y() + popupHeight > sg.bottom()) pos.setY(searchCombo->mapToGlobal(QPoint(0, 0)).y() - popupHeight);
        popup->move(pos);
    });

    // 应用按钮样式
    applyButtonStyles();
}

void PressAnalyzer::setupSearchCompleter()
{
    // fixedHints 已由 rebuildFixedHints() 生成
    QStringList completerHints = fixedHints + historyHints;
    QCompleter *completer = new QCompleter(completerHints, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    if (searchEdit) searchEdit->setCompleter(completer);

    if (searchEdit) searchEdit->installEventFilter(this);

    // 添加键盘快捷键支持
    QShortcut *searchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (searchEdit) {
            searchEdit->setFocus();
            searchEdit->selectAll();
        }
    });

    // 添加Esc键快捷键支持
    QShortcut *escapeShortcut = new QShortcut(QKeySequence("Escape"), this);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        // 只有当搜索框有焦点时才处理Esc键
        if (searchEdit && searchEdit->hasFocus()) {
            // 先隐藏补全弹窗
            if (searchEdit->completer() && searchEdit->completer()->popup()->isVisible()) {
                searchEdit->completer()->popup()->hide();
            }
            // 清空搜索框内容
            searchEdit->clear();
            // 强制将焦点转移到主窗口
            logView->setFocus();
        }
    });

    // 回车键触发搜索
    if (searchEdit) {
        connect(searchEdit, &QLineEdit::returnPressed, this, [this]() {
            searchAll();
            if (!searchResults.isEmpty()) searchDock->show();
            updatePinButtonState();
        });
    }

    // 实时搜索提示：输入时自动更新提示
    if (searchEdit) {
        connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (text.length() > 0) {
                // 延迟更新，避免频繁刷新
                QTimer::singleShot(200, this, &PressAnalyzer::updateCompleterWithSmartHints);
            }
            updatePinButtonState();
        });
    }
    if (pinAction) {
        connect(pinAction, &QAction::triggered, this, &PressAnalyzer::onPinClicked);
    }
    updatePinButtonState();
}

void PressAnalyzer::setupStatusBar()
{
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);
    statusPathLabel = new QLabel(this);
    statusPathLabel->setText("就绪");
    statusBar->addWidget(statusPathLabel, 1); // 左侧可变信息：路径/进度

    // 解析进度条（默认隐藏，解析时显示）
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedWidth(160);
    m_progressBar->hide();
    statusBar->addWidget(m_progressBar);

    statusInfoLabel = new QLabel(this);
    statusInfoLabel->setText("");
    statusInfoLabel->setMinimumWidth(420);
    statusBar->addPermanentWidget(statusInfoLabel);    // 右侧永久信息：Image/IPK/SN/HW
}

void PressAnalyzer::setupStatusDock()
{
    QToolBar *toolBar = findChild<QToolBar*>();
    if (!toolBar) return;

    statusButton = new QPushButton(this);
    statusButton->setIcon(QIcon(":/icons/icons/chart.png"));
    statusButton->setToolTip("状态面板");
    statusButton->setIconSize(QSize(20, 20));
    toolBar->addWidget(statusButton);
    toolBar->addWidget(chartSearchButton);

    // 应用样式 - 使用与主工具栏一致的样式
    struct ButtonTheme { QString bg; QString hover; QString pressed; QString fg; };
    const ButtonTheme themeIndigo   {"#EDF2FF", "#E0E7FF", "#D0D8FF", "#1F2D3D"};

    auto styleButton = [](QPushButton *button,
                          const QString &bg,
                          const QString &hover,
                          const QString &pressed,
                          const QString &fg = QString("#1F2D3D")){
        if (!button) return;
        button->setFlat(false);
        button->setStyleSheet(
            QString(
                "QPushButton{"
                "  background-color:%1;"
                "  border:1px solid #CCCCCC;"
                "  border-radius:4px;"
                "  padding:4px;"
                "  min-width:28px;"
                "  min-height:28px;"
                "}"
                "QPushButton:hover{"
                "  background-color:%2;"
                "}"
                "QPushButton:pressed{"
                "  background-color:%3;"
                "}"
            ).arg(bg, hover, pressed)
        );
    };
    styleButton(statusButton, themeIndigo.bg, themeIndigo.hover, themeIndigo.pressed, themeIndigo.fg);

    // ---- Camera 区域（嵌入 statusContainer 顶部）----
    // 心跳丢失事件列表 / Camera事件列表：仅用于数据跟踪，不显示
    heartbeatLostEventList = new QListWidget(this);
    heartbeatLostEventList->hide();
    cameraEventList = new QListWidget(this);
    cameraEventList->hide();

    // 标题标签 - 显示心跳丢失次数
    titleLabel = new QLabel("心跳丢失次数:0", this);
    titleLabel->setStyleSheet(ChartStyleManager::getTitleStyle());
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setMinimumHeight(28);

    // 事件时间轴
    eventTimeline = new EventTimelineWidget(this);
    eventTimeline->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    eventTimeline->setMinimumHeight(100);
    eventTimeline->setMaximumHeight(160);

    // Camera section widget
    QWidget *cameraSection = new QWidget(this);
    cameraSection->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "}"
    );
    QVBoxLayout *camLayout = new QVBoxLayout(cameraSection);
    camLayout->setContentsMargins(8, 6, 8, 6);
    camLayout->setSpacing(4);
    camLayout->addWidget(titleLabel);
    camLayout->addWidget(eventTimeline, 1);

    // ---- 创建状态容器 ----
    statusContainer = new QWidget(this);
    statusContainer->setStyleSheet(ChartStyleManager::getStatusContainerStyle());
    statusContainer->setMinimumWidth(400);

    QVBoxLayout *vLayout = new QVBoxLayout(statusContainer);
    vLayout->setContentsMargins(10, 10, 10, 10);
    vLayout->setSpacing(10);

    // Camera 信息放在最顶部
    vLayout->addWidget(cameraSection);

    // 电池图表
    batteryChart = new BatteryChartWidget(statusContainer);
    batteryChart->setMinimumHeight(500);
    batteryChart->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"
    );
    vLayout->addWidget(batteryChart);

    // 相机温度曲线图
    cameraTempChart = new CameraTempChartWidget(statusContainer);
    cameraTempChart->setMinimumHeight(200);
    cameraTempChart->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"
    );
    vLayout->addWidget(cameraTempChart);

    vLayout->addStretch(1);
    statusContainer->setLayout(vLayout);
}

void PressAnalyzer::setupMenuBar()
{
    // 文件菜单
    QMenu *fileMenu = menuBar()->addMenu("文件");
    QAction *actNewWindow = fileMenu->addAction("新建窗口");
    actNewWindow->setShortcut(QKeySequence::New);
    fileMenu->addSeparator();


    // 通用按钮：打开文件浏览器
    QAction *actOpenDir = fileMenu->addAction("文件浏览器");
    actOpenDir->setIcon(QIcon(":/icons/folder-open.png")); // 使用文件夹图标
    actOpenDir->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));

    // 专用按钮：分析control_engine_log
    QAction *actAnalyzeControlLog = fileMenu->addAction("分析Control Engine日志");
    actAnalyzeControlLog->setIcon(QIcon(":/icons/analysis.png")); // 使用分析图标
    actAnalyzeControlLog->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));

    fileMenu->addSeparator();
    QAction *actSave = fileMenu->addAction("保存分析结果");
    actSave->setShortcut(QKeySequence::Save);
    QAction *actClear = fileMenu->addAction("清除窗口");
    QAction *actClose = fileMenu->addAction("关闭窗口");
    actClose->setShortcut(QKeySequence::Close);

    connect(actNewWindow, &QAction::triggered, this, [=](){
        auto *w = new PressAnalyzer(nullptr);
        w->setAttribute(Qt::WA_DeleteOnClose, true);
        w->show();
    });

    connect(actOpenDir, &QAction::triggered, this, &PressAnalyzer::loadAndMergeLogs);
    connect(actAnalyzeControlLog, &QAction::triggered, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(actSave, &QAction::triggered, this, &PressAnalyzer::saveEventListToFile);
    connect(actClear, &QAction::triggered, this, &PressAnalyzer::clearWindow);
    connect(actClose, &QAction::triggered, this, &QMainWindow::close);

    // 编辑菜单（常用文本工具）
    QMenu *editMenu = menuBar()->addMenu("编辑");
    QAction *actUndo = editMenu->addAction("撤销");
    QAction *actRedo = editMenu->addAction("重做");
    actUndo->setShortcut(QKeySequence::Undo);
    actRedo->setShortcut(QKeySequence::Redo);
    editMenu->addSeparator();
    QAction *actCut = editMenu->addAction("剪切");
    QAction *actCopy = editMenu->addAction("复制");
    QAction *actPaste = editMenu->addAction("粘贴");
    QAction *actSelectAll = editMenu->addAction("全选");
    actCut->setShortcut(QKeySequence::Cut);
    actCopy->setShortcut(QKeySequence::Copy);
    actPaste->setShortcut(QKeySequence::Paste);
    actSelectAll->setShortcut(QKeySequence::SelectAll);

    // 将编辑动作派发到当前具有焦点的文本部件（QPlainTextEdit/QTextEdit/QLineEdit）
    auto dispatchEditAction = [this](const char *methodName){
        QWidget *fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QPlainTextEdit*>(fw) || qobject_cast<QTextEdit*>(fw) || qobject_cast<QLineEdit*>(fw))) {
            QMetaObject::invokeMethod(fw, methodName, Qt::DirectConnection);
        } else if (logView) {
            // 回退到日志视图
            QMetaObject::invokeMethod(logView, methodName, Qt::DirectConnection);
        }
    };

    connect(actUndo, &QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("undo"); });
    connect(actRedo, &QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("redo"); });
    connect(actCut,  &QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("cut"); });
    connect(actCopy, &QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("copy"); });
    connect(actPaste,&QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("paste"); });
    connect(actSelectAll,&QAction::triggered, this, [dispatchEditAction](){ dispatchEditAction("selectAll"); });

    // 搜索菜单
    QMenu *searchMenu = menuBar()->addMenu("搜索");
    QAction *actSearchAll = searchMenu->addAction("搜索");
    QAction *actSearchPrev = searchMenu->addAction("向前");
    QAction *actSearchNext = searchMenu->addAction("向后");
    actSearchAll->setShortcut(QKeySequence::Find);
    actSearchNext->setShortcut(QKeySequence::FindNext);
    actSearchPrev->setShortcut(QKeySequence::FindPrevious);
    connect(actSearchAll, &QAction::triggered, this, [this](){
        searchAll();
        if (!searchResults.isEmpty()) searchDock->show();
    });
    connect(actSearchPrev, &QAction::triggered, this, &PressAnalyzer::goToPrevSearch);
    connect(actSearchNext, &QAction::triggered, this, &PressAnalyzer::goToNextSearch);

    // 视图菜单
    QMenu *viewMenu = menuBar()->addMenu("视图");
    QAction *actToggleFileBrowser = viewMenu->addAction("切换 文件浏览器 面板");
    actToggleFileBrowser->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    QAction *actToggleCamera = viewMenu->addAction("切换 Camera状态 面板");
    QAction *actToggleStatus = viewMenu->addAction("切换 状态面板");
    QAction *actToggleEvent  = viewMenu->addAction("切换 分析结果 面板");
    QAction *actToggleSearchDock = viewMenu->addAction("切换 搜索结果 面板");
    QAction *actToggleDbViewer = viewMenu->addAction("切换 数据库浏览器 面板");
    actToggleDbViewer->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    viewMenu->addSeparator();
    QAction *actZoomIn = viewMenu->addAction("放大文本");
    QAction *actZoomOut = viewMenu->addAction("缩小文本");
    QAction *actZoomReset = viewMenu->addAction("重置文本大小");
    actZoomIn->setShortcut(QKeySequence::ZoomIn);
    actZoomOut->setShortcut(QKeySequence::ZoomOut);
    actZoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    viewMenu->addSeparator();
    QAction *actSetLogFont = viewMenu->addAction("设置日志字体");
    QAction *actSetEventFont = viewMenu->addAction("设置事件列表字体");
    QAction *actSetChartFont = viewMenu->addAction("设置图表字体");
    viewMenu->addSeparator();
    QAction *actResetFonts = viewMenu->addAction("重置所有字体");
    actResetFonts->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));

    connect(actToggleFileBrowser, &QAction::triggered, this, [this](){
        if (fileBrowserDock->isVisible() || eventDock->isVisible()) {
            fileBrowserDock->hide();
            eventDock->hide();
        } else {
            if (fileBrowserRootPath.isEmpty()) {
                QString dir = QFileDialog::getExistingDirectory(this, "选择浏览目录", QDir::homePath());
                if (!dir.isEmpty()) {
                    openDirectoryInBrowser(dir);
                }
            } else {
                fileBrowserDock->show();
                if (eventList->count() > 0) {
                    eventDock->show();
                }
            }
        }
    });
    connect(actToggleCamera, &QAction::triggered, this, [this](){
        statusDock->setVisible(!statusDock->isVisible());
    });
    connect(actToggleStatus, &QAction::triggered, this, [this](){
        statusDock->setVisible(!statusDock->isVisible());
    });
    connect(actToggleEvent, &QAction::triggered, this, [this](){
        eventDock->setVisible(!eventDock->isVisible());
    });
    connect(actToggleSearchDock, &QAction::triggered, this, [this](){
        searchDock->setVisible(!searchDock->isVisible());
    });
    connect(actToggleDbViewer, &QAction::triggered, this, [this](){
        if (centralStack->currentIndex() == 1)
            centralStack->setCurrentIndex(0);
        else if (currentDb.isOpen())
            centralStack->setCurrentIndex(1);
    });

    connect(actSetLogFont, &QAction::triggered, this, &PressAnalyzer::setLogFont);
    connect(actSetEventFont, &QAction::triggered, this, &PressAnalyzer::setEventFont);
    connect(actSetChartFont, &QAction::triggered, this, &PressAnalyzer::setChartFont);
    connect(actResetFonts, &QAction::triggered, this, &PressAnalyzer::resetAllFonts);

    // 放大/缩小/重置文本大小
    const int basePointSize = 11; // 基准字号固定为 11pt
    logFontPointSize = 11; // 默认 11pt
    auto applyLogFont = [this](int pt){
        QFont f = this->logView->font();
        f.setPointSize(pt);
        this->logView->setFont(f);
        if (this->searchResultView) {
            this->searchResultView->setFont(f);
        }
    };
    connect(actZoomIn, &QAction::triggered, this, [=](){ logFontPointSize += 1; applyLogFont(logFontPointSize); });
    connect(actZoomOut, &QAction::triggered, this, [=](){ logFontPointSize = std::max(8, logFontPointSize - 1); applyLogFont(logFontPointSize); });
    connect(actZoomReset, &QAction::triggered, this, [=](){ logFontPointSize = basePointSize; applyLogFont(logFontPointSize); });

    // 应用默认字号
    applyLogFont(logFontPointSize);

    // 帮助菜单
    QMenu *helpMenu = menuBar()->addMenu("帮助");
    QAction *actAbout = helpMenu->addAction("关于");
    connect(actAbout, &QAction::triggered, this, [this](){
        QMessageBox::about(this, "关于",
            "Hover日志分析助手\n\n"
            "Designed by: 代战胜\n"
            "Email: zhansheng_hello@163.com");
    });
}

void PressAnalyzer::setupUsageContainer()
{
    // 使用容器 - 应用统一样式
    usageContainer = new QWidget(this);
    usageContainer->setStyleSheet(ChartStyleManager::getStatusContainerStyle());
    usageContainer->setMinimumWidth(400);  // 设置最小宽度400

    QVBoxLayout *usageLayout = new QVBoxLayout(usageContainer);
    usageLayout->setContentsMargins(10, 10, 10, 10);
    // usageLayout->setSpacing(10);

    // SOC温度图表 - 应用统一样式
    socChart = new SocTempChartWidget(usageContainer);
    socChart->setMinimumHeight(250);
    socChart->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"
    );

    // 曲线图 - 应用统一样式
    usageChart = new ModuleUsageChart(usageContainer);
    usageChart->setMinimumHeight(250);
    usageChart->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"
    );

    // 复选框容器 - 应用统一样式
    checkBoxContainer = new QWidget(usageContainer);
    checkBoxContainer->setStyleSheet(
        "QWidget {"
        "  background-color: white;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 6px;"
        "  padding: 10px;"
        "}"
    );

    QGridLayout *gridLayout = new QGridLayout(checkBoxContainer);
    // gridLayout->setContentsMargins(10, 10, 10, 10);
    gridLayout->setHorizontalSpacing(0);
    gridLayout->setVerticalSpacing(0);

    const auto &moduleKeys = usageChart->getModuleVisibility().keys();
    int total = moduleKeys.size();
    int cols = 4;                              // 固定3列，减少水平空间需求
    int rows = (total + cols - 1) / cols;      // 每行3列，行数自动计算
    int index = 0;

    int maxLength = 15; // 固定显示长度

    for (const auto &name : moduleKeys) {
        int row = index / cols;
        int col = index % cols;

        QString displayName = name;
        if (displayName.length() > maxLength) {
            displayName = displayName.left(maxLength - 3) + "..."; // 超长显示为前部分+...
        }

        QCheckBox *cb = new QCheckBox(displayName);

        // 应用统一的复选框样式
        cb->setStyleSheet(
            "QCheckBox {"
            "  font-family: 'Arial';"
            "  font-size: 10px;"
            "  padding: 2px;"
            "  border-radius: 3px;"
            "}"
            "QCheckBox:checked {"
            "  background-color:rgb(239, 242, 245);"
            "}"
            "QCheckBox:hover {"
            "  background-color: #ecf0f1;"
            "}"
        );

        // 设置字体颜色为对应曲线颜色
        QPalette pal = cb->palette();
        pal.setColor(QPalette::WindowText, usageChart->getModuleColor(name));
        cb->setPalette(pal);

        // 默认选中 control_engine
        if (name == "control_engine") {
            cb->setChecked(true);
            usageChart->getModuleVisibility()[name] = true;
        }

        gridLayout->addWidget(cb, row, col);

        connect(cb, &QCheckBox::toggled, this, [this, name](bool checked){
            usageChart->getModuleVisibility()[name] = checked;
            usageChart->update();
        });

        index++;
    }

    checkBoxContainer->setLayout(gridLayout);

    // 添加到布局
    usageLayout->addWidget(socChart);
    usageLayout->addWidget(usageChart);
    usageLayout->addWidget(checkBoxContainer);
    usageLayout->addStretch(1);
    usageContainer->setLayout(usageLayout);

    // 与状态面板组合 - 应用统一样式
    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setStyleSheet(
        "QSplitter::handle {"
        "  background-color: #bdc3c7;"
        "  border: 1px solid #95a5a6;"
        "}"
        "QSplitter::handle:horizontal {"
        "  width: 4px;"
        "}"
    );
    mainSplitter->addWidget(statusContainer);
    mainSplitter->addWidget(usageContainer);
    mainSplitter->setStretchFactor(0, 1);  // 左边拉伸因子
    mainSplitter->setStretchFactor(1, 1);  // 右边拉伸因子，完全均分

    statusDock = new QDockWidget("状态面板", this);
    statusDock->setWidget(mainSplitter);
    statusDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, statusDock);
    statusDock->hide();
}

// 将 BLOB（QByteArray）列以十进制字节逗号分隔形式显示的委托
class BlobDecimalDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QString displayText(const QVariant &value, const QLocale &) const override {
        if (value.type() == QVariant::ByteArray) {
            const QByteArray ba = value.toByteArray();
            if (ba.isEmpty()) return QString();
            QStringList parts;
            parts.reserve(ba.size());
            for (unsigned char byte : ba) {
                parts.append(QString::number(static_cast<int>(byte)));
            }
            return parts.join(',');
        }
        return value.toString();
    }
};

void PressAnalyzer::setupDbViewerDock()
{
    // dbViewerWidget 已在 setupCentralWidget() 中创建并加入 centralStack (index 1)
    QVBoxLayout *mainLayout = new QVBoxLayout(dbViewerWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 顶部一行：[文件名] [表标签滚动区(中间)] [关闭按钮]
    QHBoxLayout *topLayout = new QHBoxLayout();
    topLayout->setSpacing(6);

    QLabel *dbPathLabel = new QLabel("未打开数据库");
    dbPathLabel->setObjectName("dbPathLabel");
    dbPathLabel->setStyleSheet("color: #444; font-size: 12px; font-weight: bold;");
    dbPathLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    dbPathLabel->setWordWrap(false);
    dbPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // 表名标签区：QScrollArea 内放 QHBoxLayout + QPushButton，窗口缩小时横向滚动
    QScrollArea *tableScrollArea = new QScrollArea();
    tableScrollArea->setObjectName("dbTableScrollArea");
    tableScrollArea->setFrameShape(QFrame::NoFrame);
    tableScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tableScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tableScrollArea->setFixedHeight(28);
    tableScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tableScrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:horizontal { height: 6px; }"
    );

    QWidget *tableButtonBar = new QWidget();
    tableButtonBar->setObjectName("dbTableButtonBar");
    tableButtonBar->setStyleSheet("background: transparent;");
    QHBoxLayout *tableBarLayout = new QHBoxLayout(tableButtonBar);
    tableBarLayout->setContentsMargins(0, 0, 0, 0);
    tableBarLayout->setSpacing(4);
    tableBarLayout->addStretch();   // 初始占位，有表时清掉重填
    tableScrollArea->setWidget(tableButtonBar);
    tableScrollArea->setWidgetResizable(true);

    // dbTableList 保留为空 QListWidget（供 loadDbTable 兼容），但不显示
    dbTableList = new QListWidget();
    dbTableList->hide();

    QPushButton *closeDbBtn = new QPushButton("关闭数据库");
    closeDbBtn->setFixedWidth(90);
    closeDbBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    closeDbBtn->setStyleSheet(
        "QPushButton { background:#888; color:white; border:none; border-radius:3px; padding:3px 8px; font-size:12px; }"
        "QPushButton:hover { background:#999; }"
        "QPushButton:pressed { background:#777; }"
    );

    topLayout->addWidget(dbPathLabel);
    topLayout->addWidget(tableScrollArea, 1);
    topLayout->addWidget(closeDbBtn);
    mainLayout->addLayout(topLayout);

    // 表格
    dbTableView = new QTableView();
    dbTableView->setAlternatingRowColors(true);
    dbTableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    dbTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    dbTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dbTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    dbTableView->horizontalHeader()->setStretchLastSection(true);
    dbTableView->setStyleSheet(
        "QTableView { border: 1px solid #ddd; font-size: 12px; }"
        "QHeaderView::section { background: #f0f0f0; border: 1px solid #ccc; padding: 3px; font-weight: bold; }"
    );
    dbTableView->setSortingEnabled(true);

    // 复制逻辑
    auto doCopy = [this](){
        QItemSelectionModel *sel = dbTableView->selectionModel();
        if (!sel || !dbTableModel) return;
        QModelIndexList indexes = sel->selectedIndexes();
        if (indexes.isEmpty()) return;

        std::sort(indexes.begin(), indexes.end(), [](const QModelIndex &a, const QModelIndex &b){
            return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column();
        });

        QString result;
        int prevRow = indexes.first().row();
        for (const QModelIndex &idx : indexes) {
            if (idx.row() != prevRow) {
                result += '\n';
                prevRow = idx.row();
            } else if (!result.isEmpty()) {
                result += '\t';
            }
            QVariant v = dbTableModel->data(idx, Qt::DisplayRole);
            if (v.type() == QVariant::ByteArray) {
                const QByteArray ba = v.toByteArray();
                QStringList parts;
                for (unsigned char byte : ba)
                    parts.append(QString::number(static_cast<int>(byte)));
                result += parts.join(',');
            } else {
                result += v.toString();
            }
        }
        QApplication::clipboard()->setText(result);
    };

    // 右键菜单
    dbTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dbTableView, &QTableView::customContextMenuRequested, this, [this, doCopy](const QPoint &pos){
        QItemSelectionModel *sel = dbTableView->selectionModel();
        bool hasSelection = sel && !sel->selectedIndexes().isEmpty();
        QMenu menu(dbTableView);
        QAction *actCopy = menu.addAction("Copy");
        actCopy->setShortcut(QKeySequence::Copy);
        actCopy->setEnabled(hasSelection);
        connect(actCopy, &QAction::triggered, this, doCopy);
        menu.exec(dbTableView->viewport()->mapToGlobal(pos));
    });

    // Cmd+C / Ctrl+C：在 dbTableView 和其 viewport 双层都装 filter
    // 用局部 struct，parent 设为 dbTableView 自动管理生命周期
    struct CopyFilter : public QObject {
        std::function<void()> fn;
        CopyFilter(QObject *parent, std::function<void()> f) : QObject(parent), fn(std::move(f)) {}
        bool eventFilter(QObject *, QEvent *e) override {
            if (e->type() == QEvent::KeyPress) {
                QKeyEvent *ke = static_cast<QKeyEvent*>(e);
                if (ke->matches(QKeySequence::Copy)) {
                    fn();
                    return true;
                }
            }
            return false;
        }
    };
    auto *cf = new CopyFilter(dbTableView, doCopy);
    dbTableView->installEventFilter(cf);
    dbTableView->viewport()->installEventFilter(cf);

    QLabel *rowCountLabel = new QLabel("行数: 0");
    rowCountLabel->setObjectName("dbRowCountLabel");
    rowCountLabel->setStyleSheet("color:#666; font-size:11px; padding:2px;");

    mainLayout->addWidget(dbTableView, 1);
    mainLayout->addWidget(rowCountLabel);

    // 关闭按钮
    connect(closeDbBtn, &QPushButton::clicked, this, [this](){
        centralStack->setCurrentIndex(0);
    });
}

void PressAnalyzer::openDatabaseFile(const QString &filePath)
{
    if (filePath.isEmpty()) return;

    // 关闭旧连接
    if (currentDb.isOpen()) {
        currentDb.close();
    }
    QString connName = "dbviewer_conn";
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }

    currentDb = QSqlDatabase::addDatabase("QSQLITE", connName);
    currentDb.setDatabaseName(filePath);

    if (!currentDb.open()) {
        QMessageBox::warning(this, "打开失败",
            QString("无法打开数据库文件：\n%1\n\n错误：%2")
                .arg(filePath)
                .arg(currentDb.lastError().text()));
        return;
    }

    currentDbPath = filePath;

    // 更新路径标签
    QLabel *pathLabel = dbViewerWidget->findChild<QLabel*>("dbPathLabel");
    if (pathLabel) {
        QFileInfo fi(filePath);
        pathLabel->setText(fi.fileName());
        pathLabel->setToolTip(filePath);
    }

    // 列举所有表，填充顶部标签按钮
    QStringList tables = currentDb.tables();
    if (tables.isEmpty()) {
        QMessageBox::information(this, "提示", "该数据库中没有找到任何表。");
    }

    // 清空旧按钮
    QWidget *btnBar = dbViewerWidget->findChild<QWidget*>("dbTableButtonBar");
    if (btnBar) {
        QLayout *oldLayout = btnBar->layout();
        QLayoutItem *item;
        while (oldLayout && (item = oldLayout->takeAt(0))) {
            delete item->widget();
            delete item;
        }
        // 重新填充
        QHBoxLayout *barLayout = qobject_cast<QHBoxLayout*>(oldLayout);
        if (!barLayout) {
            barLayout = new QHBoxLayout(btnBar);
            barLayout->setContentsMargins(0, 0, 0, 0);
            barLayout->setSpacing(4);
        }
        for (const QString &tbl : tables) {
            QPushButton *btn = new QPushButton(tbl, btnBar);
            btn->setMinimumWidth(btn->fontMetrics().horizontalAdvance(tbl) + 24);
            btn->setFixedHeight(22);
            btn->setCheckable(true);
            btn->setStyleSheet(
                "QPushButton { padding: 2px 10px; border: 1px solid #ccc; border-radius: 3px;"
                "  background: #fff; font-size: 12px; }"
                "QPushButton:hover { background: #e8f0fe; }"
                "QPushButton:checked { background: #4A90D9; color: white; border-color: #4A90D9; }"
            );
            connect(btn, &QPushButton::clicked, this, [this, tbl, btnBar](bool){
                // 取消其他按钮选中
                for (QPushButton *b : btnBar->findChildren<QPushButton*>())
                    b->setChecked(false);
                QPushButton *self = qobject_cast<QPushButton*>(sender());
                if (self) self->setChecked(true);
                loadDbTable(tbl);
            });
            barLayout->addWidget(btn);
        }
        barLayout->addStretch();
    }

    // 清空旧表格
    if (dbTableModel) {
        dbTableView->setModel(nullptr);
        delete dbTableModel;
        dbTableModel = nullptr;
    }
    QLabel *rowLbl = dbViewerWidget->findChild<QLabel*>("dbRowCountLabel");
    if (rowLbl) rowLbl->setText("行数: 0");

    // 自动加载第一张表，并选中第一个按钮
    if (!tables.isEmpty()) {
        if (btnBar) {
            QPushButton *first = btnBar->findChild<QPushButton*>();
            if (first) first->setChecked(true);
        }
        loadDbTable(tables.first());
    }

    // 切换到 DB 查看器页面
    centralStack->setCurrentIndex(1);
    if (statusPathLabel) statusPathLabel->setText(filePath);
}

void PressAnalyzer::loadDbTable(const QString &tableName)
{
    if (!currentDb.isOpen()) return;

    // 清理旧模型
    if (dbTableModel) {
        dbTableView->setModel(nullptr);
        delete dbTableModel;
        dbTableModel = nullptr;
    }

    dbTableModel = new QSqlTableModel(this, currentDb);
    dbTableModel->setTable(tableName);
    dbTableModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    dbTableModel->select();

    // 若数据超过 10000 行，分批加载全部行
    while (dbTableModel->canFetchMore()) {
        dbTableModel->fetchMore();
    }

    dbTableView->setModel(dbTableModel);
    // 应用 BLOB 十进制委托，将二进制列显示为逗号分隔的十进制字节
    dbTableView->setItemDelegate(new BlobDecimalDelegate(dbTableView));
    dbTableView->resizeColumnsToContents();

    // 更新行数标签
    QLabel *rowLbl = dbViewerWidget->findChild<QLabel*>("dbRowCountLabel");
    if (rowLbl) {
        // 从数据库直接查询精确行数
        QSqlQuery q(currentDb);
        q.exec(QString("SELECT COUNT(*) FROM \"%1\"").arg(tableName));
        int rowCount = 0;
        if (q.next()) rowCount = q.value(0).toInt();
        rowLbl->setText(QString("表: %1   行数: %2   列数: %3")
                            .arg(tableName)
                            .arg(rowCount)
                            .arg(dbTableModel->columnCount()));
    }
}

void PressAnalyzer::setupConnections()
{
    // 文件操作连接 - 始终在当前窗口操作
    connect(analyzeControlButton, &QPushButton::clicked, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(clearButton, &QPushButton::clicked, this, &PressAnalyzer::clearWindow);

    // 文件浏览器按钮 - 每次点击都允许重新选择目录；取消时仅显示当前浏览器
    connect(fileBrowserButton, &QPushButton::clicked, this, [this](){
        const QString initialDir = fileBrowserRootPath.isEmpty() ? QDir::homePath() : fileBrowserRootPath;
        const QString dir = QFileDialog::getExistingDirectory(this, "选择浏览目录", initialDir);

        if (!dir.isEmpty()) {
            clearWindow();
            openDirectoryInBrowser(dir);
            fileBrowserDock->show();
            fileBrowserDock->raise();
            return;
        }

        if (!fileBrowserRootPath.isEmpty()) {
            fileBrowserDock->show();
            fileBrowserDock->raise();
        }
    });

    // 新建窗口按钮
    connect(newWindowButton, &QPushButton::clicked, this, [this](){
        auto *w = new PressAnalyzer(nullptr);
        w->setAttribute(Qt::WA_DeleteOnClose, true);
        w->show();
    });

    // 事件列表连接
    connect(eventList, &QListWidget::itemClicked, this, &PressAnalyzer::onEventClicked);
    connect(eventList, &QListWidget::itemDoubleClicked, this, &PressAnalyzer::onEventClicked);

    // 搜索功能连接
    connect(searchAllButton, &QPushButton::clicked, this, [this](){
        searchAll();
        if (!searchResults.isEmpty()) searchDock->show();
    });
    connect(searchPrevButton, &QPushButton::clicked, this, &PressAnalyzer::goToPrevSearch);
    connect(searchNextButton, &QPushButton::clicked, this, &PressAnalyzer::goToNextSearch);
    connect(searchResultView, &SearchResultTextView::rowClicked, this, &PressAnalyzer::onSearchResultRowClicked);
    connect(searchResultView, &SearchResultTextView::rowDoubleClicked, this, &PressAnalyzer::onSearchResultRowDoubleClicked);

    // Camera事件列表（仅数据跟踪）
    connect(cameraEventList, &QListWidget::itemClicked, this, &PressAnalyzer::onCameraEventClicked);
    // 时间轴点击跳转
    connect(eventTimeline, &EventTimelineWidget::jumpToLine,
            this, &PressAnalyzer::onTimelineJumpToLine);

    // 状态面板连接：仅切换右侧状态面板的显示/隐藏，不影响左侧 dock
    connect(statusButton, &QPushButton::clicked, this, [this](){
        statusDock->setVisible(!statusDock->isVisible());
    });

    // 动态图表搜索按钮 — 单例管理窗口，已打开则 raise
    connect(chartSearchButton, &QPushButton::clicked, this, [this](){
        if (!m_chartManager) {
            m_chartManager = new DynamicChartManager(allLogLines, this);
            m_chartManager->setAttribute(Qt::WA_DeleteOnClose, false);
        }
        m_chartManager->show();
        m_chartManager->raise();
        m_chartManager->activateWindow();
    });
    connect(heartbeatLostEventList, &QListWidget::itemClicked, this, &PressAnalyzer::onStatusEventClicked);
    // 文本改变（例如跳转/选择变动）后也刷新一次可见黄色（节流 50ms）
    {
        static QTimer *cursorTimer = nullptr;
        if (!cursorTimer) {
            cursorTimer = new QTimer(this);
            cursorTimer->setSingleShot(true);
            cursorTimer->setInterval(50);
            connect(cursorTimer, &QTimer::timeout, this, [this](){ updateVisibleHighlights(); });
        }
        connect(logView, &QPlainTextEdit::cursorPositionChanged, this, [this](){
            if (cursorTimer) cursorTimer->start();
        });
    }
    // 滚动节流：按可见区域增量更新搜索高亮
    connect(logView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int){
        static QTimer t; static bool inited = false;
        if (!inited) { t.setSingleShot(true); t.setInterval(30); inited = true; }
        QObject::disconnect(&t, nullptr, nullptr, nullptr);
        QObject::connect(&t, &QTimer::timeout, this, [this](){ updateVisibleHighlights(); });
        t.start();
    });
}

// ---------------- eventFilter ----------------
bool PressAnalyzer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == searchEdit) {
        if (event->type() == QEvent::FocusIn) {
            // 获得焦点时显示提示，但延迟一点避免干扰用户
            QTimer::singleShot(100, this, [this]() {
                if (searchEdit->hasFocus() && searchEdit->text().isEmpty()) {
                    showSearchHints();
                }
            });
        } else if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up) {
                // 上下箭头键时显示提示
                if (searchEdit->completer()) {
                    searchEdit->completer()->setCompletionPrefix(searchEdit->text());
                    searchEdit->completer()->complete();
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_Tab) {
                // Tab键补全
                if (searchEdit->completer()) {
                    searchEdit->completer()->setCompletionPrefix(searchEdit->text());
                    searchEdit->completer()->complete();
                }
                return true;
            } else if (keyEvent->key() == Qt::Key_Escape) {
                // Esc键处理已移到QShortcut中，这里不再处理
                // 让事件继续传播到QShortcut处理
                return false;
            }
        }
    }
    return QObject::eventFilter(obj, event);
}

// 已移除 Ctrl/Command + 滚轮缩放，保留菜单缩放

    // 5. 在搜索或确认输入时，记录历史
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
    // 1. 固定提示词总是放在前面
    // 2. 然后显示匹配的历史记录
    QString currentText = searchEdit->text();

    QStringList hints;

    // 1. 首先添加固定提示词（总是显示）
    hints.append(fixedHints);

    // 2. 然后添加匹配的历史记录
    if (currentText.isEmpty()) {
        // 空搜索框：显示所有历史记录
        hints.append(historyHints);
    } else {
        // 正在输入：只显示匹配的历史记录
        for (const QString &history : historyHints) {
            if (history.contains(currentText, Qt::CaseInsensitive)) {
                hints.append(history);
            }
        }
    }

    // 限制提示数量，避免过多
    if (hints.size() > 20) {
        hints = hints.mid(0, 20);
    }

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

    // 计算最宽项，设置弹出视图宽度，避免省略
    if (searchCombo->view()) {
        QFontMetrics fm(searchCombo->view()->font());
        int maxw = 0;
        for (int i = 0; i < searchCombo->count(); ++i) {
            maxw = qMax(maxw, fm.horizontalAdvance(searchCombo->itemText(i)) + 30);
        }
        // 弹出宽度至少为组合框宽度
        int popupWidth = qMax(maxw, searchCombo->width());
        searchCombo->view()->setMinimumWidth(popupWidth);
        searchCombo->view()->setTextElideMode(Qt::ElideNone);
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
    // 只允许将“历史提示词”固定，若已是固定则直接返回
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

    // 智能显示提示：优先显示历史记录，然后是固定提示
    QStringList hints;

    // 添加最近使用的历史记录（最多显示10个）
    int historyCount = qMin(10, historyHints.size());
    for (int i = 0; i < historyCount; ++i) {
        hints.append(historyHints[i]);
    }

    // 添加固定提示（如果历史记录不够10个）
    int remaining = 10 - hints.size();
    if (remaining > 0) {
        int fixedCount = qMin(remaining, fixedHints.size());
        for (int i = 0; i < fixedCount; ++i) {
            hints.append(fixedHints[i]);
        }
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

void PressAnalyzer::addEventToList(int triggerCount, int lineNumber, const QString &display)
{
    // 仅保存必要信息，避免在解析阶段进行文档查找
    allEvents.push_back({lineNumber, display, QTextBlock()});

    // eventList 背景颜色
    QList<QColor> bgColors = {
        QColor("#FFCCCC"), QColor("#CCE5FF"), QColor("#CCFFCC"),
        QColor("#FFF2CC"), QColor("#E5CCFF"), QColor("#FFCCE5"),
        QColor("#CCE5FF"), QColor("#CCFFE5"), QColor("#FFE5CC"), QColor("#CCFFFF")
    };
    int colorIndex = triggerCount % bgColors.size();
    QListWidgetItem *item = new QListWidgetItem(display);
    item->setBackground(bgColors[colorIndex]);
    eventList->addItem(item);

    // 如果是第一个事件，显示eventDock并切换到分析结果标签页
    if (eventList->count() == 1) {
        eventDock->show();
        eventDock->raise(); // 切换到分析结果标签页
    }
}

bool PressAnalyzer::analyzeLogSourceFile(const QString &filePath,
                                        int &lineNumber,
                                        QDateTime &currentTakeoffTime,
                                        QString &textBuffer,
                                        bool &inRecvException,
                                        QStringList &recvExceptionLines,
                                        bool showWarning)
{
    const QString extension = QFileInfo(filePath).suffix().toLower();
    if (extension == "hlog") {
        HLogBinaryParser binaryParser;
        const QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);
        if (entries.isEmpty()) {
            if (showWarning) {
                QMessageBox::warning(this, "错误", "无法解析 .hlog 文件：" + filePath);
            } else {
                qWarning() << "无法解析 .hlog 文件:" << filePath;
            }
            return false;
        }

        for (const HLogEntry &entry : entries) {
            const QStringList lines = entry.fullText.split('\n');
            for (const QString &line : lines) {
                analyzeLogLine(line, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
            }
        }
        return true;
    }

    analyzeFile(filePath, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
    return true;
}

QStringList PressAnalyzer::collectOrderedLogFiles(const QString &baseDir,
                                                 const QString &subDir,
                                                 const QString &baseName,
                                                 const QStringList &extensions)
{
    QStringList result;
    QDir dir(baseDir + "/" + subDir);
    if (!dir.exists() || extensions.isEmpty()) {
        return result;
    }

    auto buildFilePatterns = [&](bool zipped) {
        QStringList patterns;
        for (const QString &ext : extensions) {
            patterns << QString("%1*.%2%3").arg(baseName, ext, zipped ? ".zip" : "");
        }
        return patterns;
    };

    QStringList logFiles = dir.entryList(buildFilePatterns(false), QDir::Files, QDir::Name);
    if (logFiles.isEmpty()) {
        const QStringList zipFiles = dir.entryList(buildFilePatterns(true), QDir::Files, QDir::Name);
        for (const QString &zipName : zipFiles) {
            extractZipFile(dir.filePath(zipName), dir.absolutePath());
            QApplication::processEvents();
        }
        if (!zipFiles.isEmpty()) {
            QThread::msleep(100);
            QApplication::processEvents();
            logFiles = dir.entryList(buildFilePatterns(false), QDir::Files, QDir::Name);
        }
    }

    if (logFiles.isEmpty()) {
        return result;
    }

    const QString extPattern = extensions.join('|');
    const QRegularExpression numberedRe(
        QString("^%1(?:\\.?([0-9]+))?\\.(%2)$")
            .arg(QRegularExpression::escape(baseName), extPattern),
        QRegularExpression::CaseInsensitiveOption);

    QList<QPair<int, QString>> numberedFiles;
    QStringList lastFiles;
    for (const QString &fileName : logFiles) {
        const QRegularExpressionMatch match = numberedRe.match(fileName);
        if (!match.hasMatch()) {
            continue;
        }
        if (match.captured(1).isEmpty()) {
            lastFiles << dir.filePath(fileName);
        } else {
            numberedFiles.append(qMakePair(match.captured(1).toInt(), dir.filePath(fileName)));
        }
    }

    std::sort(numberedFiles.begin(), numberedFiles.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  if (a.first != b.first) {
                      return a.first < b.first;
                  }
                  return a.second < b.second;
              });

    for (const auto &item : numberedFiles) {
        result << item.second;
    }
    std::sort(lastFiles.begin(), lastFiles.end());
    result << lastFiles;
    return result;
}

void PressAnalyzer::analyzeFile(const QString &filePath,
                                int &lineNumber,
                                QDateTime &currentTakeoffTime,
                                QString &textBuffer,
                                bool &inRecvException,
                                QStringList &recvExceptionLines)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件：" + filePath);
        return;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");

    while (!in.atEnd()) {
        const QString line = in.readLine();
        analyzeLogLine(line, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
    }
}

void PressAnalyzer::analyzeLogLine(const QString &line,
                                  int &lineNumber,
                                  QDateTime &currentTakeoffTime,
                                  QString &textBuffer,
                                  bool &inRecvException,
                                  QStringList &recvExceptionLines)
{
    lineNumber++;
    allLogLines << line;

    textBuffer.reserve(textBuffer.size() + line.size() + 16);
    textBuffer.append(QString("%1 %2\n")
                          .arg(lineNumber, 6, 10, QChar(' '))
                          .arg(line));

    // ==================== 预编译正则表达式 ====================
    static const QRegularExpression rePressPower(R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*\])");
    static const QRegularExpression reTakeoff(R"(trigger source:\s*(\d+)\s+flight mode:\s*(\w+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTs(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reFcState(R"(Detected fc state changed to\s+(\d+))");
    static const QRegularExpression reInsertSql(R"(insertMediaDataIntoDb insert media sql.*\[(.*)\])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSqlValues(R"('([^']*)'|(\d+))");
    static const QRegularExpression reCameraAction(R"(recv action from camera\s*:\s*(\d+))", QRegularExpression::CaseInsensitiveOption);

    // 不再从日志正文解析 SN，改为从 system_log/user.log 中获取

    // ==================== press once power key ====================
    if (line.contains("press once power key", Qt::CaseInsensitive)) {
        QString timestamp;
        auto m = rePressPower.match(line);
        if (m.hasMatch()) timestamp = m.captured(2);
        triggerCount++;
        QString display = QString("%1 | %2# press power key : [%3]")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(timestamp);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== 起飞事件 ====================
    if (line.contains("start takeoff. powerkey trigger source", Qt::CaseInsensitive)) {
        QString triggerText = "未知";
        auto m = reTakeoff.match(line);
        if (m.hasMatch()) {
            int src = m.captured(1).toInt();
            modeText = m.captured(2);
            if (version == "H151") {
                switch (src) {
                case 0: triggerText = "NONE"; break;
                case 1: triggerText = "APP"; break;
                case 2: triggerText = "VOICE"; break;
                case 3: triggerText = "THROW"; break;
                case 4: triggerText = "RC102"; break;
                case 5: triggerText = "RC100"; break;
                case 6: triggerText = "ROPE"; break;
                case 10: triggerText = "BOARD"; break;
                case 11: triggerText = "MCU"; break;
                default: triggerText = "UNKNOWN"; break;
                }
            } else {
                switch (src) {
                case 0: triggerText = "BOARD"; break;
                case 1: triggerText = "MCU"; break;
                case 2: triggerText = "APP"; break;
                case 3: triggerText = "RC"; break;
                case 4: triggerText = "VOICE"; break;
                case 5: triggerText = "THROW"; break;
                default: triggerText = "UNKNOWN"; break;
                }
            }
        }
        QString display = QString("%1 | %2# starting takeoff : 方式:%3 模式:%4")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(triggerText)
                              .arg(modeText);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== fly_power: takeoff success ====================
    if (line.contains("fly_power: takeoff success", Qt::CaseInsensitive)) {
        auto m = reTs.match(line);
        if (m.hasMatch()) {
            currentTakeoffTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");
        }

        QString display = QString("%1 | %2# takeoff success,Flying")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== FC STATE ====================
    auto mFc = reFcState.match(line);
    if (mFc.hasMatch()) {
        int stateValue = mFc.captured(1).toInt();
        QString stateName;
        switch (stateValue) {
        case 0: stateName = "DISARM"; break;
        case 1: stateName = "ARM"; break;
        case 2: stateName = "TAKINGOFF"; break;
        case 3: stateName = "FLYING"; break;
        case 4: stateName = "LANDING"; break;
        case 5: stateName = "TURTLE_ROLLING"; break;
        default: stateName = QString("UNKNOWN(%1)").arg(stateValue); break;
        }
        QString display = QString("%1 | %2# FC STATE -> %3")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(stateName, -12);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== fly_power: will landing ====================
    if (line.contains("fly_power: will landing", Qt::CaseInsensitive)) {
        auto m = reTs.match(line);
        if (m.hasMatch()) {
            QDateTime landingTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");
            if (currentTakeoffTime.isValid() && landingTime.isValid()) {
                qint64 flightSeconds = currentTakeoffTime.secsTo(landingTime);
                QString display = QString("%1 | %2# will Landing, Flight Duration: %3 seconds")
                                      .arg(lineNumber, 6, 10, QChar(' '))
                                      .arg(triggerCount)
                                      .arg(flightSeconds);
                addEventToList(triggerCount, lineNumber, display);
                currentTakeoffTime = QDateTime();
            }
        }
    }

    // ==================== insertMediaDataIntoDb ====================
    auto mSql = reInsertSql.match(line);
    if (mSql.hasMatch()) {
        QString sql = mSql.captured(1);

        QRegularExpression reCols(R"(INSERT\s+INTO\s+MEDIADATA\s*\(([^)]*)\)\s*VALUES)",
                                  QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch mc = reCols.match(sql);
        QStringList colNames;
        if (mc.hasMatch()) {
            QString colsStr = mc.captured(1);
            for (QString c : colsStr.split(',', Qt::SkipEmptyParts)) {
                colNames << c.trimmed().toLower();
            }
        }

        QStringList values;
        auto it = reSqlValues.globalMatch(sql);
        while (it.hasNext()) {
            auto mm = it.next();
            values << (mm.captured(1).isEmpty() ? mm.captured(2) : mm.captured(1));
        }

        auto indexOfCol = [&](const QString &name, int fallback) -> int {
            int idx = colNames.indexOf(name);
            return idx >= 0 ? idx : fallback;
        };

        int idxUuid = indexOfCol("uuid", 0);
        int idxType = indexOfCol("type", 1);
        int idxPath = indexOfCol("path", 3);

        if (!colNames.isEmpty() && colNames.contains("flightid")) {
            idxType = indexOfCol("type", 2);
            idxPath = indexOfCol("path", 4);
        }

        if (values.size() > qMax(idxPath, qMax(idxType, idxUuid))) {
            QString uuid = values.value(idxUuid);
            int type = values.value(idxType).toInt();
            QString path = values.value(idxPath);

            auto typeToString = [](int type) -> QString {
                switch (type) {
                case 1: return "METADATA";
                case 2: return "THUMBNAIL";
                case 3: return "VIDEO";
                case 4: return "PICTURE";
                case 5: return "IMU_DATA";
                case 6: return "ANIMATED_THUMBNAIL";
                case 7: return "GROUP_DATA";
                case 8: return "AUDIO";
                case 9: return "TRAJECTORY_DATA";
                default: return "UNKNOWN";
                }
            };

            QString typeStr = typeToString(type);

            QString storagePrefix;
            QString displayPath;
            if (path.startsWith("/media/internal/")) {
                storagePrefix = "Internal:";
                displayPath = path.mid(16);
            } else if (path.startsWith("/media/external/")) {
                storagePrefix = "External:";
                displayPath = path.mid(16);
            } else {
                storagePrefix = "Unknown:";
                displayPath = path;
            }

            QString display = QString("%1 | %2# Media UUID:%3 Type:%4 %5%6")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(triggerCount)
                                  .arg(uuid)
                                  .arg(typeStr)
                                  .arg(storagePrefix)
                                  .arg(displayPath);
            addEventToList(triggerCount, lineNumber, display);
        }
    }

    // ==================== recv exception ====================
    if (line.contains("recv exception :", Qt::CaseInsensitive)) {
        inRecvException = true;
        recvExceptionLines.clear();
        return;
    }
    if (inRecvException) {
        recvExceptionLines << line.trimmed();
        if (line.contains('}')) {
            inRecvException = false;
            for (const QString &l : recvExceptionLines) {
                if (l.startsWith("event:", Qt::CaseInsensitive) ||
                    l.startsWith("errors:", Qt::CaseInsensitive)) {
                    if (l.contains("NOTIFY_TURTLE_FLIP", Qt::CaseInsensitive)) {
                        triggerCount++;
                        QString display = QString("%1 | %2# %3")
                                             .arg(lineNumber - 1, 6, 10, QChar(' '))
                                             .arg(triggerCount)
                                             .arg(l.trimmed());
                        addEventToList(triggerCount, lineNumber - 1, display);
                    } else {
                        QString display = QString("%1 | %2# %3")
                                             .arg(lineNumber - 1, 6, 10, QChar(' '))
                                             .arg(triggerCount)
                                             .arg(l.trimmed());
                        addEventToList(triggerCount, lineNumber - 1, display);
                    }
                }
            }
        }
        return;
    }

    // ==================== manual_control_takeover_request ====================
    if (line.contains("manual_control_takeover_request", Qt::CaseInsensitive)) {
        QString display = QString("%1 | %2# 模式:%3 -> MANUAL")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(modeText);
        addEventToList(triggerCount, lineNumber, display);
        return;
    }

    // ==================== Camera recv action from camera ====================
    if (line.contains("recv action from camera", Qt::CaseInsensitive)) {
        auto mCamAct = reCameraAction.match(line);
        if (mCamAct.hasMatch()) {
            int actionValue = mCamAct.captured(1).toInt();
            QString actionName;
            switch (actionValue) {
            case 0: actionName = "INIT"; break;
            case 1: actionName = "START_VIDEO"; break;
            case 2: actionName = "FINISH_VIDEO"; break;
            case 3: actionName = "SNAP_DONE"; break;
            case 4: actionName = "START_PREVIEW"; break;
            case 5: actionName = "STOP_PREVIEW"; break;
            case 6: actionName = "START_CONTINOUS_PICTURE"; break;
            case 7: actionName = "STOP_CONTINOUS_PICTURE"; break;
            case 8: actionName = "IN_PREVIEWING"; break;
            case 9: actionName = "IN_VIDEO_RECORDING"; break;
            case 10: actionName = "SNAP_FILE_SAVE_DONE"; break;
            default: actionName = QString("UNKNOWN(%1)").arg(actionValue); break;
            }

            QString display = QString("%1 | %2# CS ACTION -> %3")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(triggerCount)
                                  .arg(actionName);
            addEventToList(triggerCount, lineNumber, display);
            return;
        }
    }

    // ==================== Camera 温度 ====================
    if (line.contains("camera temp", Qt::CaseInsensitive)) {
        QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
        QDateTime ts;
        if (rxTimestamp.indexIn(line) != -1) {
            QString dtStr = rxTimestamp.cap(2);
            QString tsStr = rxTimestamp.cap(1);
            
        // 解析日期和时间
        QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
        QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
        
        // 创建本地时间（日志中的时间是北京时间）
        ts = QDateTime(date, time, Qt::LocalTime);
            
            // 提取毫秒部分
            int dotPos = tsStr.indexOf('.');
            int msecs = 0;
            if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
                QString msecStr = tsStr.mid(dotPos + 1);
                // 确保毫秒部分有3位数字
                while (msecStr.length() < 3) {
                    msecStr += '0';
                }
                msecStr = msecStr.left(3); // 只取前3位
                msecs = msecStr.toInt();
            }
            ts = ts.addMSecs(msecs);
        }

        int tempVal = 0;
        bool found = false;
        QRegExp rxTempBoth("soc\\s+temp\\s+and\\s+camera\\s+temp\\s+(\\-?\\d+)\\s*:\\s*(\\-?\\d+)", Qt::CaseInsensitive);
        if (rxTempBoth.indexIn(line) != -1) {
            tempVal = rxTempBoth.cap(2).toInt();
            found = true;
        } else {
            QRegExp rxTemp("camera\\s+temp:?\\s*(\\-?\\d+)", Qt::CaseInsensitive);
            if (rxTemp.indexIn(line) != -1) {
                tempVal = rxTemp.cap(1).toInt();
                found = true;
            }
        }

        if (found && tempVal != -128) {
            cameraTemps.push_back({ts, tempVal});
        }
    }

    parseCameraStatus(lineNumber, line);
    parseStatusHeartbeat(lineNumber, line);
    parseStatusBattery(lineNumber, line);
    parseStatusSocTemp(lineNumber, line);
}

// loadAndAnalyzeLog 保持之前逻辑
// ============================================================
// 后台解析公共辅助：连接信号并启动线程
// ============================================================
static void startBackgroundParseHelper(PressAnalyzer *self,
                                        LogParserWorker *worker,
                                        QThread *thread)
{
    worker->moveToThread(thread);
    QObject::connect(thread,  &QThread::started,
                     worker,  &LogParserWorker::run);
    QObject::connect(worker,  &LogParserWorker::progressChanged,
                     self,    &PressAnalyzer::onParseProgress,
                     Qt::QueuedConnection);
    QObject::connect(worker,  &LogParserWorker::parseFinished,
                     self,    &PressAnalyzer::onParseFinished,
                     Qt::QueuedConnection);
    QObject::connect(worker,  &LogParserWorker::parseError,
                     self,    [self](const QString &msg){
                         QMessageBox::warning(self, "解析错误", msg);
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
void PressAnalyzer::onParseProgress(int percent, const QString &statusText)
{
    if (m_progressBar) {
        m_progressBar->show();
        m_progressBar->setValue(percent);
    }
    // statusPathLabel 保持显示路径，不用进度文字覆盖
}

// ============================================================
// 解析完成槽：将 ParseResult 分发到各 UI 控件
// ============================================================
void PressAnalyzer::onParseFinished(ParseResult result)
{
    // 清理线程对象（已通过 deleteLater 连接，无需手动 delete）
    m_parseThread = nullptr;
    m_parseWorker = nullptr;

    if (m_progressBar) m_progressBar->hide();

    // -------- 同步解析结果到成员变量 --------
    allLogLines    = result.allLogLines;
    triggerCount   = result.triggerCount;
    flightCount    = result.flightCount;
    batteryinfo    = result.batteryinfo;
    cameraTemps    = result.cameraTemps;
    soctmp         = result.soctmp;
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
    logView->setPlainText(result.textBuffer);

    // -------- 拆分事件到各自列表（block 赋值必须在 setPlainText 之后）--------
    allEvents.clear();
    cameraEvents.clear();
    statusEvents.clear();
    eventList->clear();
    cameraEventList->clear();
    heartbeatLostEventList->clear();

    for (const ParsedEventItem &ev : result.events) {
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
            camEv.block      = logView->document()->findBlockByNumber(ev.lineNumber - 1);
            camEv.timestamp  = ev.timestamp;
            cameraEvents.push_back(camEv);

        } else if (ev.eventCategory == "heartbeat") {
            QListWidgetItem *item = new QListWidgetItem(ev.display);
            item->setBackground(QColor(255, 182, 193));
            heartbeatLostEventList->addItem(item);

            EventItem stEv;
            stEv.lineNumber = ev.lineNumber;
            stEv.display    = ev.display;
            stEv.block      = logView->document()->findBlockByNumber(ev.lineNumber - 1);
            stEv.timestamp  = ev.timestamp;
            statusEvents.push_back(stEv);

        } else {
            // "main" 事件 — 直接填充，确保 block 在 setPlainText 之后赋值
            QList<QColor> bgColors = {
                QColor("#FFCCCC"), QColor("#CCE5FF"), QColor("#CCFFCC"),
                QColor("#FFF2CC"), QColor("#E5CCFF"), QColor("#FFCCE5"),
                QColor("#CCE5FF"), QColor("#CCFFE5"), QColor("#FFE5CC"), QColor("#CCFFFF")
            };
            int colorIndex = ev.triggerCount % bgColors.size();
            QListWidgetItem *item = new QListWidgetItem(ev.display);
            item->setBackground(bgColors[colorIndex]);
            eventList->addItem(item);

            EventItem mainEv;
            mainEv.lineNumber = ev.lineNumber;
            mainEv.display    = ev.display;
            mainEv.block      = logView->document()->findBlockByNumber(ev.lineNumber - 1);
            allEvents.push_back(mainEv);

            if (eventList->count() == 1) {
                eventDock->show();
                eventDock->raise();
            }
        }
    }

    // -------- 高亮与 UI 更新 --------
    highlightAllEvents();
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
    batteryChart->setData(batteryinfo);
    cameraTempChart->setData(cameraTemps);
    socChart->clear();
    socChart->addData(soctmp);
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3")
                       .arg(sn.isEmpty() ? QString("-") : sn)
                       .arg(triggerCount)
                       .arg(flightCount));

    // 解析 top_log（这些文件通常较小，同步即可）
    for (const QString &fp : m_pendingTopLogs)
        parseTopFile(fp);
    usageChart->setData(allusage);
    m_pendingTopLogs.clear();

    // statusPathLabel intentionally left unchanged — path was set before parsing started
}

void PressAnalyzer::loadAndAnalyzeLog()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择日志文件", "", "日志文件 (*.txt *.log *.hlog);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(filePath));

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    eventDock->hide();
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
    startBackgroundParseHelper(this, m_parseWorker, m_parseThread);
}

// ==================== 字体设置功能 ====================

void PressAnalyzer::setLogFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentLogFont, this, "设置日志字体");

    if (ok) {
        currentLogFont = font;
        logFontPointSize = font.pointSize();

        // 应用到日志视图
        if (logView) {
            logView->setFont(font);
        }

        // 应用到搜索结果视图
        if (searchResultView) {
            searchResultView->setFont(font);
        }

        // 应用到搜索框
        if (searchCombo) {
            searchCombo->setFont(font);
        }

        // 保存字体设置
        QSettings settings("ZZTools", "HoverLogAnalyzer");
        settings.setValue("fonts/logFont", font);

        QMessageBox::information(this, "字体设置", "日志字体已更新！");
    }
}

void PressAnalyzer::setChartFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentChartFont, this, "设置图表字体");

    if (ok) {
        currentChartFont = font;

        // 更新图表字体主题
        ChartStyleManager::FontTheme fontTheme;
        fontTheme.title = QFont(font.family(), font.pointSize() + 2, QFont::Bold);
        fontTheme.axis = QFont(font.family(), font.pointSize() - 3);
        fontTheme.label = QFont(font.family(), font.pointSize() - 4);
        fontTheme.tooltip = QFont(font.family(), font.pointSize() - 3);

        ChartStyleManager::setFontTheme(fontTheme);

        // 触发图表重绘
        if (batteryChart) {
            batteryChart->update();
        }
        if (socChart) {
            socChart->update();
        }
        if (usageChart) {
            usageChart->update();
        }
        if (cameraTempChart) {
            cameraTempChart->update();
        }

        // 保存字体设置
        QSettings settings("ZZTools", "HoverLogAnalyzer");
        settings.setValue("fonts/chartFont", font);

        QMessageBox::information(this, "字体设置", "图表字体已更新！");
    }
}

void PressAnalyzer::setEventFont()
{
    bool ok;
    QFont font = QFontDialog::getFont(&ok, currentEventFont, this, "设置事件列表字体");

    if (ok) {
        currentEventFont = font;

        // 应用到到主事件列表
        if (eventList) {
            eventList->setFont(font);
        }

        // 应用到相机事件列表
        if (cameraEventList) {
            cameraEventList->setFont(font);
        }

        // 应用到心跳丢失事件列表
        if (heartbeatLostEventList) {
            heartbeatLostEventList->setFont(font);
        }

        // 保存字体设置
        QSettings settings("ZZTools", "HoverLogAnalyzer");
        settings.setValue("fonts/eventFont", font);

        QMessageBox::information(this, "字体设置", "事件列表字体已更新！");
    }
}

void PressAnalyzer::resetAllFonts()
{
    // 重置日志字体
    currentLogFont = QFont("Menlo", 11);
    currentLogFont.setStyleHint(QFont::Monospace);
    currentLogFont.setFixedPitch(true);
    logFontPointSize = 11;

    if (logView) {
        logView->setFont(currentLogFont);
    }
    if (searchResultView) {
        searchResultView->setFont(currentLogFont);
    }
    if (searchCombo) {
        searchCombo->setFont(currentLogFont);
    }

    // 重置事件列表字体
    currentEventFont = QFont("Courier New", 11);
    if (eventList) {
        eventList->setFont(currentEventFont);
    }
    if (cameraEventList) {
        cameraEventList->setFont(currentEventFont);
    }
    if (heartbeatLostEventList) {
        heartbeatLostEventList->setFont(currentEventFont);
    }

    // 重置图表字体
    currentChartFont = QFont("Arial", 12);
    ChartStyleManager::FontTheme fontTheme;
    fontTheme.title = QFont("Arial", 12, QFont::Bold);
    fontTheme.axis = QFont("Arial", 9);
    fontTheme.label = QFont("Arial", 8);
    fontTheme.tooltip = QFont("Arial", 9);

    ChartStyleManager::setFontTheme(fontTheme);

    // 触发图表重绘
    if (batteryChart) {
        batteryChart->update();
    }
    if (socChart) {
        socChart->update();
    }
    if (usageChart) {
        usageChart->update();
    }
    if (cameraTempChart) {
        cameraTempChart->update();
    }

    // 清除保存的字体设置
    QSettings settings("ZZTools", "HoverLogAnalyzer");
    settings.remove("fonts/logFont");
    settings.remove("fonts/eventFont");
    settings.remove("fonts/chartFont");

    QMessageBox::information(this, "字体重置", "所有字体已重置为默认值！");
}

void PressAnalyzer::applySavedFonts()
{
    // 应用日志字体
    if (logView) {
        logView->setFont(currentLogFont);
    }
    if (searchResultView) {
        searchResultView->setFont(currentLogFont);
    }
    if (searchCombo) {
        searchCombo->setFont(currentLogFont);
    }

    // 应用事件列表字体
    if (eventList) {
        eventList->setFont(currentEventFont);
    }
    if (cameraEventList) {
        cameraEventList->setFont(currentEventFont);
    }
    if (heartbeatLostEventList) {
        heartbeatLostEventList->setFont(currentEventFont);
    }

    // 应用图表字体
    ChartStyleManager::FontTheme fontTheme;
    fontTheme.title = QFont(currentChartFont.family(), currentChartFont.pointSize() + 2, QFont::Bold);
    fontTheme.axis = QFont(currentChartFont.family(), currentChartFont.pointSize() - 3);
    fontTheme.label = QFont(currentChartFont.family(), currentChartFont.pointSize() - 4);
    fontTheme.tooltip = QFont(currentChartFont.family(), currentChartFont.pointSize() - 3);

    ChartStyleManager::setFontTheme(fontTheme);

    // 触发图表重绘
    if (batteryChart) {
        batteryChart->update();
    }
    if (socChart) {
        socChart->update();
    }
    if (usageChart) {
        usageChart->update();
    }
    if (cameraTempChart) {
        cameraTempChart->update();
    }
}

void PressAnalyzer::loadAndAnalyzeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志文件或目录", "");
    if (path.isEmpty()) return;

    // 在"分析Control Engine日志"场景下：仅在当前目录的下一级（直接子目录）查找 system_log
    {
        auto findChildSystemLog = [](const QString &base) -> QString {
            QDir dir(base);
            if (dir.exists("system_log")) return dir.absoluteFilePath("system_log");
            QFileInfoList level1 = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d1 : level1) {
                QDir dir1(d1.absoluteFilePath());
                if (dir1.exists("system_log")) return dir1.absoluteFilePath("system_log");
            }
            return QString();
        };

        auto parseUserLogHeaderCE = [this](const QString &userLogPath){
            QFile file(userLogPath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QTextStream in(&file);
            in.setCodec("UTF-8");
            QString imageVer, ipkVer, sn, hwid;
            QString prevLine;
            int linesRead = 0;
            while (!in.atEnd() && linesRead < 400) {
                QString line = in.readLine();
                ++linesRead;
                QString l = line.trimmed();
                if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1); else imageVer = l;
                }
                if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
                }
                if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                    if (!l.isEmpty()) hwid = l;
                }
                if (imageVer.isEmpty()) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1);
                }
                if (ipkVer.isEmpty()) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1);
                }
                if (sn.isEmpty()) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                prevLine = l;
                if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
            }
            this->sn = sn;
            QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                               .arg(imageVer.isEmpty() ? "-" : imageVer)
                               .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                               .arg(sn.isEmpty() ? "-" : sn)
                               .arg(hwid.isEmpty() ? "-" : hwid);
            version = imageVer.mid(0,4);
            if (statusInfoLabel) statusInfoLabel->setText(info);
        };

        QString syslogDir = findChildSystemLog(path);
        if (!syslogDir.isEmpty()) {
            QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
            QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
            if (QFileInfo::exists(userLog)) {
                parseUserLogHeaderCE(userLog);
            } else if (QFileInfo::exists(userLogZip)) {
                if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                    parseUserLogHeaderCE(userLog);
                }
            }
        }
    }

    // 如果用户直接选中了 control_engine_log 目录本身，自动上移到父目录
    {
        QFileInfo selInfo(path);
        if (selInfo.isDir() && selInfo.fileName() == "control_engine_log") {
            path = selInfo.absolutePath();
        }
    }

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));
    QFileInfo info(path);

    QStringList controlLogs, topLogs;

    if (info.isDir()) {
        controlLogs = collectOrderedLogFiles(path, "control_engine_log", "control_engine", {"log", "hlog"});
        topLogs = collectOrderedLogFiles(path, "system_log/top_log", "top", {"log"});

        if (controlLogs.isEmpty() && topLogs.isEmpty()) {
            QMessageBox::warning(this, "错误", "日志文件不存在");
            return;
        }
    } else if (info.isFile()) {
        controlLogs << info.filePath();
    }

    // ---------------- 清空之前的数据 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
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

    // ---- 启动后台解析线程 ----
    m_pendingTopLogs = topLogs;

    m_parseWorker = new LogParserWorker();
    m_parseWorker->setFiles(controlLogs);
    m_parseWorker->setVersion(version);

    m_parseThread = new QThread();
    startBackgroundParseHelper(this, m_parseWorker, m_parseThread);
}

void PressAnalyzer::loadAndAnalyzeLogsFromPath(const QString &path)
{
    // 切换到日志视图页
    centralStack->setCurrentIndex(0);

    // 在"分析Control Engine日志"场景下：仅在当前目录的下一级（直接子目录）查找 system_log
    {
        auto findChildSystemLog = [](const QString &base) -> QString {
            QDir dir(base);
            // 当前目录本身
            if (dir.exists("system_log")) return dir.absoluteFilePath("system_log");
            // 直接子目录
            QFileInfoList level1 = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d1 : level1) {
                QDir dir1(d1.absoluteFilePath());
                if (dir1.exists("system_log")) return dir1.absoluteFilePath("system_log");
            }
            return QString();
        };

        auto parseUserLogHeaderCE = [this](const QString &userLogPath){
            QFile file(userLogPath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QTextStream in(&file);
            in.setCodec("UTF-8");
            QString imageVer, ipkVer, sn, hwid;
            QString prevLine;
            int linesRead = 0;
            while (!in.atEnd() && linesRead < 400) {
                QString line = in.readLine();
                ++linesRead;
                QString l = line.trimmed();
                if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1); else imageVer = l;
                }
                if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
                }
                if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                    if (!l.isEmpty()) hwid = l;
                }
                if (imageVer.isEmpty()) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1);
                }
                if (ipkVer.isEmpty()) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1);
                }
                if (sn.isEmpty()) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                prevLine = l;
                if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
            }
            // 将解析到的 SN 写回成员变量，供窗口标题等使用
            this->sn = sn;

            QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                               .arg(imageVer.isEmpty() ? "-" : imageVer)
                               .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                               .arg(sn.isEmpty() ? "-" : sn)
                               .arg(hwid.isEmpty() ? "-" : hwid);
            version = imageVer.mid(0,4);
            if (statusInfoLabel) statusInfoLabel->setText(info);
        };

        QString syslogDir = findChildSystemLog(path);
        if (!syslogDir.isEmpty()) {
            QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
            QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
            if (QFileInfo::exists(userLog)) {
                parseUserLogHeaderCE(userLog);
            } else if (QFileInfo::exists(userLogZip)) {
                if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                    parseUserLogHeaderCE(userLog);
                }
            }
        }
    }

    // 减少大文件解析时的界面重绘
    // (blockers removed: parsing is now done in background thread)

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));
    QFileInfo info(path);

    QStringList controlLogs, topLogs;

    if (info.isDir()) {
        controlLogs = collectOrderedLogFiles(path, "control_engine_log", "control_engine", {"log", "hlog"});
        topLogs = collectOrderedLogFiles(path, "system_log/top_log", "top", {"log"});

        if (controlLogs.isEmpty() && topLogs.isEmpty()) {
            QMessageBox::warning(this, "错误", "日志文件不存在");
            return;
        }
    } else if (info.isFile()) {
        controlLogs << info.filePath();
    }

    // ---------------- 清空之前的数据 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
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

    // ---- 启动后台解析线程 ----
    m_pendingTopLogs = topLogs;

    m_parseWorker = new LogParserWorker();
    m_parseWorker->setFiles(controlLogs);
    m_parseWorker->setVersion(version);

    m_parseThread = new QThread();
    startBackgroundParseHelper(this, m_parseWorker, m_parseThread);
}

QString PressAnalyzer::findControlEngineAnalysisRoot(const QString &basePath) const
{
    QFileInfo baseInfo(basePath);
    if (!baseInfo.exists()) {
        return QString();
    }

    if (baseInfo.isDir() && baseInfo.fileName() == "control_engine_log") {
        return baseInfo.absolutePath();
    }

    const QDir rootDir(baseInfo.isDir() ? baseInfo.absoluteFilePath() : baseInfo.absolutePath());
    QDirIterator it(rootDir.absolutePath(),
                    QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dirPath = it.next();
        if (QFileInfo(dirPath).fileName() == "control_engine_log") {
            return QFileInfo(dirPath).absolutePath();
        }
    }

    return QString();
}

void PressAnalyzer::loadAndMergeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志目录");
    if (path.isEmpty()) {
        return;
    }
    loadMergeLogsFromPath(path);
}

void PressAnalyzer::loadMergeLogsFromPath(const QString &path, bool navigate)
{
    // 执行智能分析前先清除窗口内容，避免上次分析结果的干扰
    clearWindow();

    // 显示用户选择的通用目录路径（后续过程保持静默，不覆盖）
    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));

    // 根据参数决定是否在文件浏览器中打开此目录
    if (navigate) {
        openDirectoryInBrowser(path);
    }

    // 先在所选目录向下（最多两级）寻找 system_log 目录，并尝试解析 user.log 头部信息
    auto findSystemLogDir = [](const QString &base) -> QString {
        QDir cur(base);
        // 情况A：当前目录本身或其上级链中存在 system_log，则返回该 system_log
        QDir climb = cur;
        while (true) {
            if (climb.dirName() == QStringLiteral("system_log")) {
                return climb.absolutePath();
            }
            QDir up(climb);
            if (!up.cdUp()) break;
            climb = up;
        }

        // 情况B：在当前目录的同级目录中查找 system_log
        QDir parent(cur.absolutePath());
        if (parent.cdUp()) {
            if (parent.exists("system_log")) {
                return parent.absoluteFilePath("system_log");
            }
        }

        return QString();
    };

    auto parseUserLogHeader = [this](const QString &userLogPath){
        QFile file(userLogPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&file);
        in.setCodec("UTF-8");

        QString imageVer, ipkVer, sn, hwid;
        QString prevLine;
        int linesRead = 0;
        while (!in.atEnd() && linesRead < 400) {
            QString line = in.readLine();
            ++linesRead;
            QString l = line.trimmed();
            // 通过标记行提取下一行内容
            if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                // 目标：提取 H141B_V8.0.17
                QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reZZ.match(l);
                if (m.hasMatch()) imageVer = m.captured(1);
                if (imageVer.isEmpty()) imageVer = l;
            }
            if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                         QRegularExpression::CaseInsensitiveOption);
                auto m = reVer.match(l);
                if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
            }
            if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reSn.match(l);
                if (m.hasMatch()) sn = m.captured(1);
            }
            if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                if (!l.isEmpty()) hwid = l;
            }

            // 也支持不依赖标记的直匹配
            if (imageVer.isEmpty()) {
                QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reZZ.match(l);
                if (m.hasMatch()) imageVer = m.captured(1);
            }
            if (ipkVer.isEmpty()) {
                QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                         QRegularExpression::CaseInsensitiveOption);
                auto m = reVer.match(l);
                if (m.hasMatch()) ipkVer = m.captured(1);
            }
            if (sn.isEmpty()) {
                QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reSn.match(l);
                if (m.hasMatch()) sn = m.captured(1);
            }

            prevLine = l;
            if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
        }

        QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                           .arg(imageVer.isEmpty() ? "-" : imageVer)
                           .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                           .arg(sn.isEmpty() ? "-" : sn)
                           .arg(hwid.isEmpty() ? "-" : hwid);
        version = imageVer.mid(0,4);
        // 将持久信息放到右侧永久区域；左侧保持路径不变
        if (statusInfoLabel) statusInfoLabel->setText(info);
    };

    QString syslogDir = findSystemLogDir(path);
    if (!syslogDir.isEmpty()) {
        QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
        QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
        if (QFileInfo::exists(userLog)) {
            // 已解压，直接解析
            parseUserLogHeader(userLog);
        } else if (QFileInfo::exists(userLogZip)) {
            // 解压 user.log.zip 到同目录（若已解压则 unzip 会覆盖或直接成功返回）
            if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                parseUserLogHeader(userLog);
            }
        }
    }

    // 如果当前目录本身是 control_engine_log，或其下包含 control_engine_log 子目录，
    // 都走专用的 control_engine 分析逻辑。
    QFileInfo pathInfo(path);
    if (pathInfo.fileName() == "control_engine_log") {
        QString parentPath = pathInfo.absolutePath();
        loadAndAnalyzeLogsFromPath(parentPath);
        return;
    }
    if (pathInfo.isDir() && QDir(path).exists("control_engine_log")) {
        loadAndAnalyzeLogsFromPath(path);
        return;
    }

    // 如果不是control_engine_log目录，隐藏eventDock
    eventDock->hide();

    // 禁用界面更新，提高性能
    logView->setUpdatesEnabled(false);
    QSignalBlocker blocker1(eventList);
    QSignalBlocker blocker2(searchResultView);
    QSignalBlocker blocker3(cameraEventList);
    QSignalBlocker blocker4(heartbeatLostEventList);

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    eventDock->hide(); // 清空后隐藏eventDock
    heartbeatLostEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    searchResults.clear();
    searchResultView->clearResults();
    batteryinfo.clear();
    allusage.clear();
    soctmp.clear();
    triggerCount = 0;
    flightCount = 0;

    // 第一步：快速解压所有 zip 文件（递归扫描子目录）
    std::function<void(const QString&)> extractAllZips;
    extractAllZips = [&extractAllZips, this](const QString &dirPath) {
        QDir dir(dirPath);
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);

        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                extractAllZips(entry.filePath());
                continue;
            }

            if (entry.isFile() && entry.fileName().endsWith(".zip", Qt::CaseInsensitive)) {
                QString zipPath = entry.filePath();
                QString extractDir = entry.absolutePath();

                // 更新状态栏显示当前处理的zip文件
                // 静默解压
                QApplication::processEvents(); // 保持界面响应

                // 使用统一的解压函数（Windows使用PowerShell，Linux/macOS使用unzip）
                extractZipFile(zipPath, extractDir);

                // 处理Qt事件，保持界面响应
                QApplication::processEvents();
            }
        }
    };

    // 第二步：递归收集所有相关文件
    std::function<QStringList(const QString&)> collectAllFiles;
    collectAllFiles = [&collectAllFiles, this](const QString &dirPath) -> QStringList {
        QStringList allFiles;
        QDir dir(dirPath);
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);

        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                allFiles += collectAllFiles(entry.filePath());
                continue;
            }

            if (entry.isFile()) {
                QString fileName = entry.fileName();
                if (fileName.endsWith(".log", Qt::CaseInsensitive) ||
                    fileName.endsWith(".ulg", Qt::CaseInsensitive) ||
                    fileName.endsWith(".csv", Qt::CaseInsensitive) ||
                    fileName.endsWith(".txt", Qt::CaseInsensitive) ||
                    fileName.endsWith(".hlog", Qt::CaseInsensitive)) {
                    allFiles.append(entry.filePath());
                }
            }
        }

        return allFiles;
    };

    // 先解压所有zip文件
    // 静默
    extractAllZips(path);

    // 然后收集所有相关文件
    // 静默
    QStringList allFiles = collectAllFiles(path);

        if (allFiles.isEmpty()) {
        QMessageBox::information(this, "提示", "未找到相关文件");
        logView->setUpdatesEnabled(true);
        return;
    }

    // 如果只有一个文件，直接打开
    if (allFiles.size() == 1) {
        loadSelectedFilesInOrder(allFiles);
        return;
    }

    // 创建文件选择对话框
    QDialog dialog(this);
    dialog.setWindowTitle("选择要显示的文件");
    dialog.resize(600, 400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // 添加说明标签
    QLabel *label = new QLabel("请选择要显示的文件（支持多选，按点击顺序显示，再次点击取消选择）:");
    layout->addWidget(label);

    // 创建文件列表，禁用系统多选，由点击事件手动管理选择顺序
    QListWidget *fileList = new QListWidget();
    fileList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(fileList);

    // 用于记录按点击顺序排列的文件路径
    QStringList *clickOrderFiles = new QStringList();

    // 添加文件到列表，显示文件名和大小
    for (const QString &filePath : allFiles) {
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();
        qint64 fileSize = fileInfo.size();
        QString sizeStr = (fileSize > 1024 * 1024) ?
            QString("%1 MB").arg(fileSize / (1024.0 * 1024.0), 0, 'f', 1) :
            QString("%1 KB").arg(fileSize / 1024.0, 0, 'f', 1);

        QString displayText = QString("%1 (%2)").arg(fileName).arg(sizeStr);
        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, filePath); // 存储完整路径
        // 用 Qt::UserRole+1 存储选择序号（0 = 未选）
        item->setData(Qt::UserRole + 1, 0);
        fileList->addItem(item);
    }

    // 点击时按序号管理选择顺序
    connect(fileList, &QListWidget::itemClicked, [fileList, clickOrderFiles](QListWidgetItem *item){
        QString filePath = item->data(Qt::UserRole).toString();
        int currentOrder = item->data(Qt::UserRole + 1).toInt();

        if (currentOrder > 0) {
            // 已选中 -> 取消选择，移出列表，并更新其他项的序号
            clickOrderFiles->removeAll(filePath);
            item->setData(Qt::UserRole + 1, 0);
            item->setBackground(QBrush());
            item->setText(item->text().left(item->text().lastIndexOf(" [")));
            // 重新编号剩余选中项
            for (int i = 0; i < fileList->count(); ++i) {
                QListWidgetItem *it = fileList->item(i);
                int ord = it->data(Qt::UserRole + 1).toInt();
                if (ord > 0) {
                    int newOrd = clickOrderFiles->indexOf(it->data(Qt::UserRole).toString()) + 1;
                    it->setData(Qt::UserRole + 1, newOrd);
                    // 更新显示文本中的序号标记
                    QString baseText = it->text();
                    int bracketPos = baseText.lastIndexOf(" [");
                    if (bracketPos >= 0) baseText = baseText.left(bracketPos);
                    it->setText(QString("%1 [%2]").arg(baseText).arg(newOrd));
                }
            }
        } else {
            // 未选中 -> 追加到选择列表
            clickOrderFiles->append(filePath);
            int order = clickOrderFiles->size();
            item->setData(Qt::UserRole + 1, order);
            item->setBackground(QBrush(QColor(173, 216, 230))); // 浅蓝色背景
            item->setText(QString("%1 [%2]").arg(item->text()).arg(order));
        }
    });

    // 添加按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton("确定");
    QPushButton *cancelButton = new QPushButton("取消");
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    // 连接信号
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 显示对话框
    if (dialog.exec() == QDialog::Accepted) {
        // 按点击顺序加载文件
        if (!clickOrderFiles->isEmpty()) {
            loadSelectedFilesInOrder(*clickOrderFiles);
        }
    }

    delete clickOrderFiles;

    // 恢复界面更新
    logView->setUpdatesEnabled(true);
}

// 高亮事件行
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
    } else if (eventType.contains("FC STATE", Qt::CaseInsensitive)) {
        color = color = QColor(255, 105, 180); // 热粉色 - 飞控状态变化
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

    // 遍历统一高亮
    for (const auto &event : mergedEvents) {
        highlightLine(event.lineNumber, event.display);
    }
}

// 点击事件列表只跳转，不再修改颜色
void PressAnalyzer::onEventClicked(QListWidgetItem *item)
{
    int row = eventList->row(item);
    if (row < 0 || row >= allEvents.size()) return;

    int lineNumber = allEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);

    if (!block.isValid()) return;  // block 有效性检查

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);

    // 将光标所在行居中显示
    logView->centerCursor();
}

// 保存结果列表
void PressAnalyzer::saveEventListToFile()
{
    if (allEvents.isEmpty()) {
        QMessageBox::warning(this, "提示", "当前没有分析结果，无法保存");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this, "保存分析结果", "", "文本文件 (*.txt);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件保存");
        return;
    }

    QTextStream out(&file);
    for (const EventItem &item : allEvents) {
        out << item.display << "\n";
    }

    QMessageBox::information(this, "提示", "飞机Log分析结果已保存");
}

// 清空
void PressAnalyzer::clearWindow()
{
    // 切换回日志视图页
    centralStack->setCurrentIndex(0);

    allEvents.clear();
    allLogLines.clear();
    eventList->clear();
    logView->clear();
    searchResultView->clearResults();
    searchResultView->hide();
    searchResults.clear();
    searchDock->hide();
    currentSearchIndex = -1;
    triggerCount = 0;
    flightCount = 0;
    cameraEventList->clear();
    if (eventTimeline) eventTimeline->clear();
    heartbeatLostEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    if (socChart) socChart->clear();
    statusDock->hide();
    batteryinfo.clear();
    soctmp.clear();
    logView->moveCursor(QTextCursor::Start);   // 光标移到开头
    QTextCursor cursor = logView->textCursor();
    cursor.clearSelection();                    // 取消选中
    logView->setTextCursor(cursor);
    allusage.clear();
    if (usageChart) usageChart->setData(allusage);
    if (statusPathLabel) statusPathLabel->setText("就绪");
    if (statusInfoLabel) statusInfoLabel->setText("");
    setWindowTitle("日志分析工具");

    // 重置全局按钮点击状态，下次点击任何文件按钮都会在当前窗口显示
    anyFileButtonClicked = false;
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
    for (const QString &part : parts) {
        QString k = part.trimmed();
        if (!k.isEmpty()) {
            rawKeys.append(k);
            QString nk = k;
            nk.replace(QRegularExpression("\\s+"), " ");
            normKeys.append(nk);
        }
    }

    // 仅对可见区域做黄色高亮（避免整篇文档重绘），但结果列表不再截断
    bool isHeavy = true;

    // 准备颜色池，前两个关键字固定颜色，其余随机亮色
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

    QVector<QPair<QRegExp, QColor>> patterns;
    for (int i = 0; i < rawKeys.size(); ++i) {
        // 构建正则用于结果列表着色（仍按字面匹配）
        QRegExp rx(QRegExp::escape(rawKeys[i]), Qt::CaseInsensitive);

        QColor color;
        if (i == 0)
            color = Qt::yellow;
        else if (i == 1)
            color = Qt::green;
        else
            color = colorPool[(i - 2) % colorPool.size()];

        patterns.append(qMakePair(rx, color));
    }

    // 设置高亮（SearchResultTextView 内部使用 ExtraSelection 实现）
    searchResultView->setPatterns(patterns);

    // 遍历日志行，匹配关键字（使用 QString::indexOf 快路径，避免正则开销），并构建 logView 全局黄色高亮（重负载时跳过全量构建）
    QStringList resultLines;
    for (int i = 0; i < allLogLines.size(); ++i) {
        bool matched = false;
        const QString &lineRef = allLogLines[i];
        // 先尝试原始关键字
        for (const QString &k : rawKeys) {
            if (lineRef.indexOf(k, 0, Qt::CaseInsensitive) != -1) { matched = true; break; }
        }
        // 若未匹配，再用“归一空格”的方式
        if (!matched) {
            QString ln = lineRef;
            ln.replace(QRegularExpression("\\s+"), " ");
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
    }

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
    QStringList keys = searchEdit->text().trimmed().split('|', Qt::SkipEmptyParts);

    // 准备颜色池，前两个关键字固定颜色，其余按顺序使用
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
    for (int i = 0; i < keys.size(); ++i) {
        if (i == 0)
            colors.append(Qt::yellow);
        else if (i == 1)
            colors.append(Qt::green);
        else
            colors.append(colorPool[(i - 2) % colorPool.size()]);
    }

    // 只高亮当前索引所在行，其他行延迟到滚动时再做
    if (currentIndex >= 0 && currentIndex < searchResults.size()) {
        int lineNumber = searchResults[currentIndex];
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

    // 合并颜色标记（置于底层），再叠加搜索高亮
    QList<QTextEdit::ExtraSelection> finalSelections = m_colorMarkHighlights + selections;
    logView->setExtraSelections(finalSelections);
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

        // 双击后组合全局黄色与当前行灰色底（灰底先渲染，黄色在上层，不被覆盖）
        QList<QTextEdit::ExtraSelection> combined = searchHighlights;
        QTextEdit::ExtraSelection lineSel;
        lineSel.cursor = QTextCursor(currentBlock);
        lineSel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QTextCharFormat lineFmt;
        lineFmt.setBackground(QColor(200, 200, 200));
        lineSel.format = lineFmt;
        combined.prepend(lineSel);
        // 合并颜色标记（底层）+ 搜索高亮，避免颜色标记被清除
        QList<QTextEdit::ExtraSelection> finalCombined = m_colorMarkHighlights + combined;
        logView->setExtraSelections(finalCombined);
        // 立刻补一次可见黄色，以免需要滚轮才出现
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

void PressAnalyzer::parseCameraStatus(int lineNumber, const QString &line)
{
    auto bitToStr = [](int bit) { return bit ? "ON" : "OFF"; };

    QRegExp rx("The last five bits of\\s*([01]{5})");
    if (rx.indexIn(line) != -1) {
        QString bitsStr = rx.cap(1);
        bool ok = false;
        int last_five_bits = bitsStr.toInt(&ok, 2);
        if (!ok) return;

        bool status = (last_five_bits >> 0) & 1;
        bool stream = (last_five_bits >> 1) & 1;
        bool preview = (last_five_bits >> 2) & 1;
        bool recording = (last_five_bits >> 3) & 1;
        bool snapshot = (last_five_bits >> 4) & 1;

        // ---------------- 颜色和备注 ----------------
        QString note;
        QColor bgColor = Qt::white; // 默认白色背景

        if (!status) {
            // Camera关闭
            bgColor = QColor(169,169,169); // 深灰色
            note = "close";
        } else if (!stream && !preview && !recording && !snapshot) {
            // 初始化状态
            bgColor = QColor(211,211,211); // 浅灰色
            note = "init";
        } else {
            // 其他状态组合
            if (recording) bgColor = Qt::red;
            else if (stream && preview) bgColor = QColor(255,165,0); // 橙色
            else if (stream) bgColor = Qt::green;
            else if (preview) bgColor = Qt::yellow;
            else if (snapshot) bgColor = Qt::cyan;

            // 构造备注文字
            QStringList activeStates;
            if (stream) activeStates << "stream";
            if (preview) activeStates << "preview";
            if (recording) activeStates << "recording";
            if (snapshot) activeStates << "snapshot";
            note = activeStates.join("+");
        }
        QString display = QString("%1 | [%2] status=%3, stream=%4, preview=%5, recording=%6, snapshot=%7")
                              .arg(lineNumber)
                              .arg(note)
                              .arg(bitToStr(status))
                              .arg(bitToStr(stream))
                              .arg(bitToStr(preview))
                              .arg(bitToStr(recording))
                              .arg(bitToStr(snapshot));
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setText(display);
        item->setBackground(bgColor);
        EventItem camEvent;
        camEvent.lineNumber = lineNumber;
        camEvent.display = display;
        camEvent.block = logView->document()->findBlockByNumber(lineNumber - 1);
        cameraEvents.push_back(camEvent);
        cameraEventList->addItem(item);
    }
}
BatteryInfo parseBatteryInfo(const QString &line)
{
    BatteryInfo info;

    // 查找 "battery info:" 的位置
    int idx = line.indexOf("battery info:");
    if (idx == -1) return info; // 没找到就返回默认值

    // 去掉前面的日志前缀
    QString data = line.mid(idx + QString("battery info:").length()).trimmed();

    // 使用正则匹配 key=value 或 key: value，忽略空格
    QRegularExpression re(R"(\b(\w+)\s*[:=]\s*([^\s,]+))");
    QRegularExpressionMatchIterator i = re.globalMatch(data);

    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        QString key = match.captured(1).trimmed();
        QString value = match.captured(2).trimmed();

        if (key == "soc") info.soc = value.toInt();
        else if (key == "current") info.current = value.toInt();
        else if (key == "voltage") info.voltage = value.toInt();
        else if (key == "temp") info.temp = value.toDouble();
        else if (key == "is_abnormal") info.is_abnormal = value.toInt();
        else if (key == "is_charging") info.is_charging = value.toInt();
        else if (key == "heating") info.heating = value.toInt();
        else if (key == "can_heat") info.can_heat = value.toInt();
        else if (key == "battery_sn") info.battery_sn = value;
        else if (key == "battery_cycles_count") info.battery_cycles_count = value.toInt();
        else if (key == "battery_health") info.battery_health = value.toInt();
    }

    return info;
}
void PressAnalyzer::parseStatusHeartbeat(int lineNumber, const QString &line)
{
    // ---------------- 检查 APP/RC 超时 ----------------
    if (line.contains("APP heart timeout delay", Qt::CaseInsensitive)) {
        QString display = QString("%1 | App/RC 连接超时10s").arg(lineNumber);
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setBackground(QColor(255, 182, 193));
        EventItem stEvent;
        stEvent.lineNumber = lineNumber;
        stEvent.display = display;
        stEvent.block = logView->document()->findBlockByNumber(lineNumber - 1);
        statusEvents.push_back(stEvent);
        heartbeatLostEventList->addItem(item);
        return;
    }

}
void PressAnalyzer::parseStatusBattery(int lineNumber, const QString &line)
{
    // ---------------- 解析电池信息 ----------------
    if (line.contains("battery info", Qt::CaseInsensitive)) {
        BatteryTimeInfo timeinfo;
        // 提取时间戳和日期时间
        QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
        if (rxTimestamp.indexIn(line) != -1) {
            QString tsStr = rxTimestamp.cap(1);   // 9733.000
            QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

            // 解析日期和时间
            QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
            QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
            
            // 创建本地时间（日志中的时间是北京时间）
            timeinfo.timestamp = QDateTime(date, time, Qt::LocalTime);

            // 提取毫秒部分
            int dotPos = tsStr.indexOf('.');
            int msecs = 0;
            if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
                QString msecStr = tsStr.mid(dotPos + 1);
                // 确保毫秒部分有3位数字
                while (msecStr.length() < 3) {
                    msecStr += '0';
                }
                msecStr = msecStr.left(3); // 只取前3位
                msecs = msecStr.toInt();
            }
            timeinfo.timestamp = timeinfo.timestamp.addMSecs(msecs);
        }
        timeinfo.info = parseBatteryInfo(line);
        batteryinfo.push_back(timeinfo);
    }
}

void PressAnalyzer::onCameraEventClicked(QListWidgetItem *item)
{
    int row = cameraEventList->row(item);
    if (row < 0 || row >= cameraEvents.size()) return;

    int lineNumber = cameraEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}
void PressAnalyzer::onStatusEventClicked(QListWidgetItem *item)
{
    int row = heartbeatLostEventList->row(item);
    if (row < 0 || row >= statusEvents.size()) return;

    int lineNumber =  statusEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}

void PressAnalyzer::onTimelineJumpToLine(int lineNumber)
{
    if (lineNumber <= 0) return;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();
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

    // 确保管理窗口已创建
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
    m_chartManager->show();
    m_chartManager->raise();
    m_chartManager->activateWindow();
}

void PressAnalyzer::parseStatusSocTemp(int lineNumber, const QString &line)
{
    // 如果行中不包含关键字，直接跳过
    if (!line.contains("get soc max temp")) {
        return;
    }

    SocTempInfo info;

    // 提取时间戳和日期时间
    QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\]");
    if (rxTimestamp.indexIn(line) != -1) {
        QString tsStr = rxTimestamp.cap(1);   // 9733 或 9733.000
        QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

        // 解析日期和时间
        QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
        QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
        
        // 创建本地时间（日志中的时间是北京时间）
        info.timestamp = QDateTime(date, time, Qt::LocalTime);

        // 提取毫秒部分
        int dotPos = tsStr.indexOf('.');
        int msecs = 0;
        if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
            QString msecStr = tsStr.mid(dotPos + 1);
            // 确保毫秒部分有3位数字
            while (msecStr.length() < 3) {
                msecStr += '0';
            }
            msecStr = msecStr.left(3); // 只取前3位
            msecs = msecStr.toInt();
        }
        info.timestamp = info.timestamp.addMSecs(msecs);
    }

    // 提取 max temp
    QRegExp rxMax("get soc max temp\\s*[:=]\\s*(\\d+)");
    if (rxMax.indexIn(line) != -1) {
        info.maxTemp = rxMax.cap(1).toInt();
    }

    // 提取 core temp
    QRegExp rxCore("core temp\\s*[:=]\\s*([0-9:]+)");
    if (rxCore.indexIn(line) != -1) {
        QStringList temps = rxCore.cap(1).split(":");
        for (const QString &t : temps) {
            info.coreTemps.append(t.toInt());
        }
    }

    // 只保留有效数据点：timestamp 必须 valid，maxTemp 必须 > 0
    if (!info.timestamp.isValid() || info.maxTemp <= 0) return;
    soctmp.append(info);
}


// 将 "top - 14:03:27 ..." 这一行提取时间
QDateTime PressAnalyzer::parseTopTime(const QString &line) {
    QRegExp rx("top - (\\d{2}:\\d{2}:\\d{2})");
    if (rx.indexIn(line) != -1) {
        QString timeStr = rx.cap(1);
        QTime t = QTime::fromString(timeStr, "HH:mm:ss");
        // 使用当天日期，设置为本地时间（北京时间）
        return QDateTime(QDate::currentDate(), t, Qt::LocalTime);
    }
    return QDateTime();
}

// 从 top 文件解析所有模块数据
// 假设你在 PressAnalyzer.h 里有
// QVector<AllModuleUsage> allusage;

void PressAnalyzer::parseTopFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    AllModuleUsage usage;
    bool hasData = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        // 遇到新的 top 时间戳 -> 保存上一个 usage
        if (line.startsWith("top -")) {
            if (hasData) {
                allusage.append(usage);
                usage = AllModuleUsage(); // 重置
            }
            usage.timestamp = parseTopTime(line);
            hasData = true;
            continue;
        }

        QStringList parts = line.split(QRegExp("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 12) continue;

        QString moduleName = parts.last();
        double cpu = parts[8].toDouble();
        double mem = parts[9].toDouble();

        if (moduleName == "camera_service") usage.camera_service = {cpu, mem};
        else if (moduleName == "captain") usage.captain = {cpu, mem};
        else if (moduleName == "fcs") usage.fcs = {cpu, mem};
        else if (moduleName == "control_engine") usage.control_engine = {cpu, mem};
        else if (moduleName == "drvf_msg_monito") usage.drvf_msg_monito = {cpu, mem};
        else if (moduleName == "top") usage.top = {cpu, mem};
        else if (moduleName == "vio_hover") usage.vio_hover = {cpu, mem};
        else if (moduleName == "logd") usage.logd = {cpu, mem};
        else if (moduleName == "exception_manag") usage.exception_manag = {cpu, mem};
        else if (moduleName == "bt_service") usage.bt_service = {cpu, mem};
        else if (moduleName == "battery_service") usage.battery_service = {cpu, mem};
        else if (moduleName == "gimbal_service") usage.gimbal_service = {cpu, mem};
        else if (moduleName == "kworker/u18:1-crm_workq-icp_message_q") usage.kworker_u18_icp_message_q = {cpu, mem};
        else if (moduleName == "logcat") usage.logcat = {cpu, mem};
        else if (moduleName == "kworker/u19:1-kgsl-events") usage.kworker_u19_kgsl_events = {cpu, mem};
        else if (moduleName == "systemd") usage.systemd = {cpu, mem};
        else if (moduleName == "kthreadd") usage.kthreadd = {cpu, mem};
        else if (moduleName == "rcu_gp") usage.rcu_gp = {cpu, mem};
        else if (moduleName == "rcu_par_gp") usage.rcu_par_gp = {cpu, mem};
        else if (moduleName == "kworker/0:0-events") usage.kworker_0_events = {cpu, mem};
        else if (moduleName ==  "fpv_service") usage.fpv_service = {cpu, mem};
    }

    // 最后一组数据也要存进去
    if (hasData) {
        allusage.append(usage);
    }
}

void PressAnalyzer::loadSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        logView->setUpdatesEnabled(true);
        return;
    }

    // 对文件进行排序（按数字顺序）
    QStringList sortedFiles = filePaths;
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const QString &a, const QString &b) {
        QString fileNameA = QFileInfo(a).fileName();
        QString fileNameB = QFileInfo(b).fileName();

        // 提取基础名称、扩展名和数字
        QString baseNameA, baseNameB, extA, extB;
        int numA = -1, numB = -1;

        // 解析文件名 A
        QRegExp rxA("(.+?)(\\.(\\d+))?\\.(log|ulg|csv)$");
        if (rxA.indexIn(fileNameA) != -1) {
            baseNameA = rxA.cap(1);
            extA = rxA.cap(4);
            if (!rxA.cap(3).isEmpty()) {
                numA = rxA.cap(3).toInt();
            }
        }

        // 解析文件名 B
        QRegExp rxB("(.+?)(\\.(\\d+))?\\.(log|csv|ulg)$");
        if (rxB.indexIn(fileNameB) != -1) {
            baseNameB = rxB.cap(1);
            extB = rxB.cap(4);
            if (!rxB.cap(3).isEmpty()) {
                numB = rxB.cap(3).toInt();
            }
        }

        // 如果基础名称相同，按数字排序
        if (baseNameA == baseNameB) {
            // 按数字排序
            if (numA == -1 && numB == -1) return false; // 都是无数字后缀，保持原顺序
            if (numA == -1) return true;  // A无数字后缀，排在前面
            if (numB == -1) return false; // B无数字后缀，排在后面
            return numA < numB; // 按数字排序
        }

        // 基础名称不同，按字母排序
        return baseNameA < baseNameB;
    });

    // 检查文件大小，如果总大小超过100MB，显示警告
    qint64 totalSize = 0;
    for (const QString &filePath : sortedFiles) {
        QFileInfo fileInfo(filePath);
        totalSize += fileInfo.size();
    }

    if (totalSize > 100 * 1024 * 1024) { // 100MB
        QMessageBox::StandardButton reply = QMessageBox::question(this, "文件过大警告",
            QString("选中的文件总大小约为 %1 MB，加载可能需要较长时间。是否继续？")
            .arg(totalSize / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            logView->setUpdatesEnabled(true);
            return;
        }
    }

        // 清空之前的内容
    allLogLines.clear();
    logView->clear();

    // 使用QPlainTextEdit的append方法，避免内存问题
    int totalLineNumber = 0;
    int processedFiles = 0;

    for (const QString &filePath : sortedFiles) {
        processedFiles++;
        // 静默：原有进度提示已移除

        QFileInfo fileInfo(filePath);
        QString extension = fileInfo.suffix().toLower();

        // 添加文件分隔符
        if (totalLineNumber > 0) {
            logView->appendPlainText(QString("\n\n=== 文件: %1 ===\n\n").arg(fileInfo.fileName()));
        }

        // 检查是否是 .hlog 二进制文件
        if (extension == "hlog") {
            HLogBinaryParser binaryParser;
            QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);

            if (entries.isEmpty()) {
                qWarning() << "无法解析 .hlog 文件:" << filePath;
                continue;
            }

            // 将解析的条目添加到视图（保证多行内容逐行编号）
            for (const HLogEntry &entry : entries) {
                const QStringList lines = entry.fullText.split('\n');
                for (const QString &l : lines) {
                    totalLineNumber++;
                    allLogLines << l;
                    QString numberedLine = QString("%1 %2")
                                             .arg(totalLineNumber, 6, 10, QChar(' '))
                                             .arg(l);
                    logView->appendPlainText(numberedLine);
                }
            }
        } else {
            // 处理文本文件
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            QTextStream in(&file);
            in.setCodec("UTF-8");

            // 正则表达式用于检测日志条目开始
            static const QRegularExpression timestampPattern(R"(\[\d+\.?\d*\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\])");
            static const QRegularExpression levelFilePattern(R"(\[[IDWEF]\|[^:]+\:\d+\])");

            // 分块读取文件，避免一次性加载大文件到内存
            const int chunkSize = 1000; // 每次处理1000行
            QStringList lines;
            int lineCount = 0;
            bool inMultiLineEntry = false;

            while (!in.atEnd()) {
                lines.clear();

                // 读取一个块的行
                for (int i = 0; i < chunkSize && !in.atEnd(); ++i) {
                    QString line = in.readLine();
                    lines.append(line);
                    totalLineNumber++;
                    allLogLines << line;
                }

                // 处理这个块的行
                for (const QString &line : lines) {
                    QString trimmedLine = line.trimmed();
                    bool isLogEntryStart = false;

                    // 检查是否是日志条目开始（包含时间戳或级别信息）
                    if (!trimmedLine.isEmpty()) {
                        QRegularExpressionMatch timestampMatch = timestampPattern.match(trimmedLine);
                        QRegularExpressionMatch levelMatch = levelFilePattern.match(trimmedLine);
                        isLogEntryStart = timestampMatch.hasMatch() || levelMatch.hasMatch();
                    }

                    if (isLogEntryStart) {
                        // 新的日志条目开始，添加行号
                        inMultiLineEntry = true;
                        QString numberedLine = QString("%1 %2")
                                                 .arg(totalLineNumber - lines.size() + lineCount + 1, 6, 10, QChar(' '))
                                                 .arg(line);
                        logView->appendPlainText(numberedLine);
                    } else if (inMultiLineEntry && !trimmedLine.isEmpty()) {
                        // 多行日志的后续行，添加8个空格而不是行号
                        QString indentedLine = QString("        %1").arg(line);  // 8个空格
                        logView->appendPlainText(indentedLine);
                    } else {
                        // 空行或独立行，正常处理
                        if (trimmedLine.isEmpty()) {
                            inMultiLineEntry = false;  // 空行结束多行条目
                        }
                        QString numberedLine = QString("%1 %2")
                                                 .arg(totalLineNumber - lines.size() + lineCount + 1, 6, 10, QChar(' '))
                                                 .arg(line);
                        logView->appendPlainText(numberedLine);
                    }
                    lineCount++;
                }

                // 处理Qt事件，保持界面响应
                QApplication::processEvents();
            }

            file.close();
        }
    }

    // 更新状态栏
    // 静默

    // 设置窗口标题
    setWindowTitle(QString("日志查看器 - %1 个文件").arg(sortedFiles.size()));

    // 解析完成后恢复更新
    logView->setUpdatesEnabled(true);
}

void PressAnalyzer::loadSelectedFilesInOrder(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        logView->setUpdatesEnabled(true);
        return;
    }

    // 检查文件大小，如果总大小超过100MB，显示警告
    qint64 totalSize = 0;
    for (const QString &filePath : filePaths) {
        QFileInfo fileInfo(filePath);
        totalSize += fileInfo.size();
    }

    if (totalSize > 100 * 1024 * 1024) { // 100MB
        QMessageBox::StandardButton reply = QMessageBox::question(this, "文件过大警告",
            QString("选中的文件总大小约为 %1 MB，加载可能需要较长时间。是否继续？")
            .arg(totalSize / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            logView->setUpdatesEnabled(true);
            return;
        }
    }

        // 清空之前的内容
    allLogLines.clear();
    logView->clear();

    // 构建文本缓冲区
    QString textBuffer;
    int totalLineNumber = 0;
    int processedFiles = 0;

    for (const QString &filePath : filePaths) {
        processedFiles++;
        // 静默：原有进度提示已移除

        QFileInfo fileInfo(filePath);
        QString extension = fileInfo.suffix().toLower();

        // 添加文件分隔符
        if (totalLineNumber > 0) {
            textBuffer += QString("\n\n=== 文件: %1 ===\n\n").arg(fileInfo.fileName());
        }

        // 检查是否是 .hlog 二进制文件
        if (extension == "hlog") {
            HLogBinaryParser binaryParser;
            QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);

            if (entries.isEmpty()) {
                qWarning() << "无法解析 .hlog 文件:" << filePath;
                continue;
            }

            // 将解析的条目添加到缓冲区
            for (const HLogEntry &entry : entries) {
                const QStringList lines = entry.fullText.split('\n');
                for (const QString &l : lines) {
                    totalLineNumber++;
                    allLogLines << l;
                    QString numberedLine = QString("%1 %2\n")
                                             .arg(totalLineNumber, 6, 10, QChar(' '))
                                             .arg(l);
                    textBuffer += numberedLine;
                }
            }
        } else {
            // 处理文本文件
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            QTextStream in(&file);
            in.setCodec("UTF-8");

            // 正则表达式用于检测日志条目开始
            static const QRegularExpression timestampPattern(R"(\[\d+\.?\d*\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\])");
            static const QRegularExpression levelFilePattern(R"(\[[IDWEF]\|[^:]+\:\d+\])");

            // 分块读取文件，避免一次性加载大文件到内存
            const int chunkSize = 1000; // 每次处理1000行
            QStringList lines;
            int lineCount = 0;
            bool inMultiLineEntry = false;

            while (!in.atEnd()) {
                lines.clear();

                // 读取一个块的行
                for (int i = 0; i < chunkSize && !in.atEnd(); ++i) {
                    QString line = in.readLine();
                    lines.append(line);
                    totalLineNumber++;
                    allLogLines << line;
                }

                // 处理这个块的行
                for (const QString &line : lines) {
                    QString trimmedLine = line.trimmed();
                    bool isLogEntryStart = false;

                    // 检查是否是日志条目开始（包含时间戳或级别信息）
                    if (!trimmedLine.isEmpty()) {
                        QRegularExpressionMatch timestampMatch = timestampPattern.match(trimmedLine);
                        QRegularExpressionMatch levelMatch = levelFilePattern.match(trimmedLine);
                        isLogEntryStart = timestampMatch.hasMatch() || levelMatch.hasMatch();
                    }

                    // 确保每一行都有连续的行号，无论是否是多行日志
                    int currentLineNumber = totalLineNumber - lines.size() + lineCount + 1;

                    if (trimmedLine.isEmpty()) {
                        inMultiLineEntry = false;  // 空行结束多行条目
                    } else if (trimmedLine.startsWith('[') && timestampPattern.match(trimmedLine).hasMatch()) {
                        // 新的日志条目开始
                        inMultiLineEntry = true;
                    }

                    QString numberedLine = QString("%1 %2\n")
                                         .arg(currentLineNumber, 6, 10, QChar(' '))
                                         .arg(line);
                    textBuffer += numberedLine;
                    lineCount++;
                }

                // 处理Qt事件，保持界面响应
                QApplication::processEvents();
            }

            file.close();
        }
    }

    // 一次性设置所有内容
    logView->setPlainText(textBuffer);

    // 更新状态栏
    // 静默

    // 设置窗口标题
    setWindowTitle(QString("日志查看器 - %1 个文件").arg(filePaths.size()));

    // 解析完成后恢复更新
    logView->setUpdatesEnabled(true);
}

void PressAnalyzer::applyButtonStyles()
{
    struct ButtonTheme { QString bg; QString hover; QString pressed; QString fg; };
    const ButtonTheme themePrimary  {"#E8F3FF", "#D9ECFF", "#C6E2FF", "#1F2D3D"};
    const ButtonTheme themeSuccess  {"#EDF9E5", "#E0F3D3", "#CCE9BB", "#1F2D3D"};
    const ButtonTheme themeDanger   {"#FDECEA", "#F9DAD7", "#F3C5C1", "#611A15"};
    const ButtonTheme themeInfo     {"#F0EEFF", "#E6E3FF", "#D9D4FF", "#1F2D3D"};
    const ButtonTheme themeNeutral  {"#F4F4F5", "#ECECEC", "#E2E3E4", "#1F2D3D"};
    const ButtonTheme themeWarning  {"#FFF3E0", "#FFE4BA", "#FFDA9B", "#5C3B0A"};
    const ButtonTheme themeIndigo   {"#EDF2FF", "#E0E7FF", "#D0D8FF", "#1F2D3D"};

    auto styleButton = [](QPushButton *button,
                          const QString &bg,
                          const QString &hover,
                          const QString &pressed,
                          const QString &fg = QString("#1F2D3D")){
        if (!button) return;
        button->setFlat(false);
        button->setStyleSheet(
            QString(
                "QPushButton{"
                "  background-color:%1;"
                "  border:1px solid #CCCCCC;"
                "  border-radius:4px;"
                "  padding:4px;"
                "  min-width:28px;"
                "  min-height:28px;"
                "}"
                "QPushButton:hover{"
                "  background-color:%2;"
                "}"
                "QPushButton:pressed{"
                "  background-color:%3;"
                "}"
            ).arg(bg, hover, pressed)
        );
    };

    styleButton(analyzeControlButton, themePrimary.bg, themePrimary.hover, themePrimary.pressed, themePrimary.fg);
    styleButton(fileBrowserButton, themeInfo.bg,  themeInfo.hover,  themeInfo.pressed,  themeInfo.fg);
    styleButton(clearButton,     themeDanger.bg,  themeDanger.hover,  themeDanger.pressed,  themeDanger.fg);
    styleButton(newWindowButton, themeIndigo.bg,   themeIndigo.hover,   themeIndigo.pressed,   themeIndigo.fg);
    styleButton(searchAllButton, themeInfo.bg,    themeInfo.hover,    themeInfo.pressed,    themeInfo.fg);
    styleButton(chartSearchButton, themeIndigo.bg, themeIndigo.hover, themeIndigo.pressed, themeIndigo.fg);
    styleButton(searchPrevButton,themeNeutral.bg, themeNeutral.hover, themeNeutral.pressed, themeNeutral.fg);
    styleButton(searchNextButton,themeNeutral.bg, themeNeutral.hover, themeNeutral.pressed, themeNeutral.fg);
}

// ============================================================
// closeEvent: safely stop any running background parse thread
// before the window and its children are destroyed.
// Without this, destroying PressAnalyzer while m_parseThread is
// still running causes QThread::~QThread() to call fatal().
// ============================================================
void PressAnalyzer::closeEvent(QCloseEvent *event)
{
    if (m_parseThread && m_parseThread->isRunning()) {
        m_parseThread->quit();          // ask event loop to exit
        m_parseThread->wait(3000);      // wait up to 3 s
        if (m_parseThread->isRunning())
            m_parseThread->terminate(); // force-kill as last resort
        m_parseThread->wait(500);
    }
    QMainWindow::closeEvent(event);
}
