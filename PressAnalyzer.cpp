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

    // 日志字体固定使用 Menlo 11pt，不从 QSettings 读取（避免历史设置污染默认值）
    currentLogFont = QFont("Menlo", 11);
    currentLogFont.setStyleHint(QFont::Monospace);
    currentLogFont.setFixedPitch(true);

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
    setupFileBrowserDock();   // sidebar panel 0 = 文件浏览器
    setupEventDock();         // sidebar panel 1 = 分析结果

    // sidebar panel 2 = 动态图表（嵌入侧边栏）
    m_chartManager = new DynamicChartManager(allLogLines, this);
    m_chartPanelIndex = m_sideBar->addPanel(
        SideBarIcons::dynamicChart(), "动态图表", m_chartManager, "动态图表");

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
