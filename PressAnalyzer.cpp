// PressAnalyzer.cpp - Core: constructor, destructor, event filter, static data
#include "PressAnalyzer.h"
#include <QCompleter>
#include <QSettings>
#include <QTimer>
#include <QKeyEvent>

// ==================== 颜色标记：Notepad++ 风格5色 ====================
const QColor PressAnalyzer::s_markColors[5] = {
    QColor(  0, 180, 255),
    QColor(255,  80,   0),
    QColor(180,   0, 220),
    QColor(  0, 180,  60),
    QColor(160,  80,   0),
};

// 跨窗口共享字体
QFont PressAnalyzer::s_sharedLogFont;
bool  PressAnalyzer::s_sharedLogFontSet = false;

// 前向声明已移除：startBackgroundParseHelper 已替换为成员方法 PressAnalyzer::startBackgroundParse

PressAnalyzer::PressAnalyzer(QWidget *parent)
    : QMainWindow(parent), currentSearchIndex(-1),
      fileBrowserTree(nullptr), fileSystemModel(nullptr), fileBrowserButton(nullptr)
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
    // 清除历史保存的 logFont，确保每次启动都用 Menlo 11pt
    settings.remove("fonts/logFont");

    // 日志字体固定使用平台等宽字体 11pt，不从 QSettings 读取（避免历史设置污染默认值）
    // macOS: Menlo, Windows: Consolas, Linux: DejaVu Sans Mono
    currentLogFont = platformMonoFont(11);

    // 加载事件列表字体：macOS=Courier New 11pt，Windows=Segoe UI 9pt（更协调）
    currentEventFont = QFont(
#if defined(Q_OS_WIN)
        "Segoe UI", 9
#else
        "Courier New", 11
#endif
    );
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
    setupFileBrowserDock();   // sidebar panel 0 = 文件浏览器
    setupEventDock();         // sidebar panel 1 = 分析结果

    // 动态图表：改为独立浮动窗口，侧边栏用外部切换按钮
    m_chartManager = new DynamicChartManager(allLogLines, this);

    // 创建动态图表浮动窗口
    // parent=this：与主窗口绑定屏幕归属，避免首次 show 跑到主屏
    m_chartFloatWin = new QWidget(this,
        Qt::Window | Qt::Tool | Qt::WindowTitleHint |
        Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    m_chartFloatWin->setWindowTitle("动态图表");
    m_chartFloatWin->setAttribute(Qt::WA_DeleteOnClose, false);
    m_chartFloatWin->resize(900, 650);
    {
        QVBoxLayout *lay = new QVBoxLayout(m_chartFloatWin);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(m_chartManager);
    }
    // 侧边栏按钮在 setupConnections 中注册（确保在状态面板按钮之后添加，使其位于下方）

    setupSearchDock();
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

bool PressAnalyzer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == logView) {
        if (event->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasUrls()) { de->acceptProposedAction(); return true; }
        } else if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent*>(event);
            dropEvent(de);
            return true;
        }
    }

    // 同步浮动窗口可见性到侧边栏按钮激活状态
    if (obj == statusFloatWin) {
        if (event->type() == QEvent::Show)
            m_sideBar->setExternalToggleActive(m_statusToggleIndex, true);
        else if (event->type() == QEvent::Hide)
            m_sideBar->setExternalToggleActive(m_statusToggleIndex, false);
    } else if (obj == m_chartFloatWin) {
        if (event->type() == QEvent::Show)
            m_sideBar->setExternalToggleActive(m_chartPanelIndex, true);
        else if (event->type() == QEvent::Hide)
            m_sideBar->setExternalToggleActive(m_chartPanelIndex, false);
    }

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
    // 非激活标签存储的文档 parent=this，Qt 析构时自动清理，无需手动 delete。
    QMainWindow::closeEvent(event);
}
