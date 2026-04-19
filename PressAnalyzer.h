#ifndef PRESSANALYZER_H
#define PRESSANALYZER_H

#include <QMainWindow>
#include <QCloseEvent>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
class SearchComboBox : public QComboBox {
    Q_OBJECT
public:
    using QComboBox::QComboBox;
signals:
    void aboutToShowPopup();
    void popupShown();
protected:
    void showPopup() override {
        emit aboutToShowPopup();
        QComboBox::showPopup();
        emit popupShown();
    }
};

#include <QDateTime>
#include <QStringList>
#include <QDockWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QMenu>
#include <QTextBlock>
#include <QTextEdit>
#include <QLabel>
#include "SearchResultTextView.h"
#include "BatteryChartWidget.h"
#include "SocTempChartWidget.h"
#include "ModuleUsageChart.h"
#include "ChartStyleManager.h"
#include "CameraTempChartWidget.h"
#include <QSettings>
#include <QAction>
#include <QWidgetAction>
#include <QTreeView>
#include <QFileSystemModel>
#include <QDir>
#include <QTableView>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QStackedWidget>
#include <QThread>
#include <QProgressBar>
#include <QTabBar>
#include <QScrollBar>
#include <QPainter>
#include <QHash>

// QTabBar subclass that draws a colored strip at the bottom of each tab
class ColoredTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit ColoredTabBar(QWidget *parent = nullptr) : QTabBar(parent) {
        setUsesScrollButtons(false);
        setElideMode(Qt::ElideNone);
    }

    QSize tabSizeHint(int index) const override {
        // Use bold font metrics (selected tab is bold) to ensure full text fits
        QFont boldFont = font();
        boldFont.setBold(true);
        QFontMetrics fmBold(boldFont);
        int textW = fmBold.horizontalAdvance(tabText(index));
        // left padding(10) + text + gap(8) + close button(16) + right padding(8)
        int w = 10 + textW + 8 + 16 + 8;
        QSize s = QTabBar::tabSizeHint(index);
        return QSize(qMax(s.width(), w), s.height());
    }

    QSize minimumTabSizeHint(int index) const override {
        return tabSizeHint(index);
    }

    void setTabColor(int index, const QColor &color) {
        m_colors[index] = color;
        update();
    }

    QColor tabColor(int index) const {
        return m_colors.value(index, Qt::transparent);
    }

    // Call after removing a tab to keep color indices in sync
    void shiftColorsAfterRemove(int removedIndex) {
        QHash<int, QColor> newColors;
        for (auto it = m_colors.begin(); it != m_colors.end(); ++it) {
            if (it.key() < removedIndex)
                newColors[it.key()] = it.value();
            else if (it.key() > removedIndex)
                newColors[it.key() - 1] = it.value();
        }
        m_colors = newColors;
        update();
    }

protected:
    void changeEvent(QEvent *event) override {
        QTabBar::changeEvent(event);
        if (event->type() == QEvent::StyleChange ||
            event->type() == QEvent::FontChange) {
            // Re-lock ElideNone; style changes can reset it
            setElideMode(Qt::ElideNone);
            updateGeometry();
        }
    }

    void paintEvent(QPaintEvent *event) override {
        QTabBar::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (int i = 0; i < count(); ++i) {
            if (!m_colors.contains(i)) continue;
            QRect r = tabRect(i);
            QColor bg = m_colors[i];
            bg.setAlpha(45);  // very light tint over the full tab
            painter.fillRect(r, bg);
        }
    }

private:
    QHash<int, QColor> m_colors;
};
#include "LogParserWorker.h"
#include "EventTimelineWidget.h"
#include "DynamicChartWidget.h"
#include "DynamicChartWindow.h"
#include "DynamicChartManager.h"

class QStandardItemModel;

struct EventItem {
    int lineNumber;    // 日志行号
    QString display;   // 显示文本
    QTextBlock block;  // 对应 viewLog 的文本块
    QDateTime timestamp; // 事件时间戳（供 timeline 使用）
    QColor bgColor;    // 列表项背景色（用于标签页切换后复原）
};

// 每个标签页保存的完整状态快照
struct TabState {
    // 文档指针：nullptr 表示该标签当前处于激活状态（文档在 logView 中）
    QTextDocument           *document              = nullptr;

    // 日志数据
    QStringList              allLogLines;
    QList<EventItem>         allEvents;
    QList<EventItem>         cameraEvents;
    QList<EventItem>         statusEvents;
    QVector<BatteryTimeInfo> batteryinfo;
    QVector<SocTempInfo>     soctmp;
    QVector<CameraTempSample>cameraTemps;
    QVector<AllModuleUsage>  allusage;

    // 颜色标记
    QMap<QString, int>       colorMarks;
    QList<QTextEdit::ExtraSelection> colorMarkHighlights;
    QList<QTextEdit::ExtraSelection> searchViewColorHighlights;

    // 搜索状态
    QList<int>               searchResults;
    int                      currentSearchIndex    = -1;
    QList<QTextEdit::ExtraSelection> searchHighlights;

    // 统计
    int                      triggerCount          = 0;
    int                      flightCount           = 0;
    QString                  sn;
    QStringList              pendingTopLogs;

    // UI 状态
    bool                     isControlEngine       = false;  // 是否 CE 日志（决定状态面板）
    bool                     statusDockVisible      = false;
    int                      scrollValue            = 0;
    int                      cursorPosition         = 0;

    // 每标签字体状态（记录该标签激活时的日志字体与缩放字号）
    QFont                    logFont;              // 该标签的日志字体快照
    int                      logFontPtSize         = -1; // -1 表示未初始化，使用窗口默认

    // 标题/路径
    QString                  windowTitle;
    QString                  statusPath;
    QString                  statusInfo;
    QString                  sourcePath;           // 来源路径（用于标签名推断）
};

class PressAnalyzer : public QMainWindow
{
    Q_OBJECT

public:
    explicit PressAnalyzer(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // 文件操作
    void loadAndAnalyzeLog();
    void loadAndAnalyzeLogs();
    void loadAndMergeLogs();  // 新增：通用目录打开和log合并功能
    void saveEventListToFile();
    void clearWindow();

    // 事件列表操作
    void addEventToList(int triggerCount, int lineNumber, const QString &display);
    void onEventClicked(QListWidgetItem *item);

    // 字体设置功能
    void setLogFont();
    void setEventFont();
    void setChartFont();
    void resetAllFonts();

    // 搜索功能
    void searchAll();
    void goToPrevSearch();
    void goToNextSearch();
    void onSearchResultRowClicked(int row);
    void onSearchResultRowDoubleClicked(int row);
    void onCameraEventClicked(QListWidgetItem *item);
    void onStatusEventClicked(QListWidgetItem *item);
    void onTimelineJumpToLine(int lineNumber);

public slots:
    // 后台解析槽（需要 public 以便 static helper 函数通过函数指针连接）
    void onParseFinished(ParseResult result);
    void onParseProgress(int percent, const QString &statusText);
private:
    // 日志解析
    void analyzeFile(const QString &filePath,
                     int &lineNumber,
                     QDateTime &currentTakeoffTime,
                     QString &textBuffer,
                     bool &inRecvException,
                     QStringList &recvExceptionLines);

    // 单行解析（供文本日志与 .hlog 复用）
    void analyzeLogLine(const QString &line,
                        int &lineNumber,
                        QDateTime &currentTakeoffTime,
                        QString &textBuffer,
                        bool &inRecvException,
                        QStringList &recvExceptionLines);
    bool analyzeLogSourceFile(const QString &filePath,
                              int &lineNumber,
                              QDateTime &currentTakeoffTime,
                              QString &textBuffer,
                              bool &inRecvException,
                              QStringList &recvExceptionLines,
                              bool showWarning = true);
    QStringList collectOrderedLogFiles(const QString &baseDir,
                                       const QString &subDir,
                                       const QString &baseName,
                                       const QStringList &extensions);

    // 文件加载
    void loadSelectedFiles(const QStringList &filePaths);
    void loadSelectedFilesInOrder(const QStringList &filePaths);
    void loadAndAnalyzeLogsFromPath(const QString &path);

    // 高亮
    void highlightAllEvents();
    void highlightLine(int lineNumber, const QString &eventType);
    void highlightSearchResults(int index);
    void jumpToSearchIndex(int index);
    void parseCameraStatus(int lineNumber,const QString &line);
    void parseStatusHeartbeat(int lineNumber, const QString &line);
    void parseStatusBattery(int lineNumber, const QString &line);
    void parseStatusSocTemp(int lineNumber, const QString &line);
    QDateTime parseTopTime(const QString &line);
    void parseTopFile(const QString &filePath);
    bool eventFilter(QObject *obj, QEvent *event) override;
    void addSearchHistory(const QString &text);
    void showSearchHints();
    void updateCompleterWithSmartHints();
    void updateSearchDropdownItems();
    void loadPinnedHints();
    void savePinnedHints();
    void rebuildFixedHints();
    void onDropdownContextMenu(const QPoint &pos);
    void onPinClicked();
    void updatePinButtonState();
    void updateVisibleHighlights();
    QColor getEventColor(const QString &eventType);

    // 颜色标记
    void applyLineColorMark(int colorIndex, const QString &keyword = QString()); // 对选中文本全文匹配应用颜色标记
    void clearColorMarkByText(const QString &text); // 清除指定文本的颜色标记
    void clearLineColorMark(int lineNumber);        // 清除指定行颜色标记（保留兼容）
    void clearAllColorMarks();                      // 清除所有颜色标记
    void rebuildColorMarkSelections();              // 重新生成 colorMarkHighlights

    // 解压工具函数
    bool extractZipFile(const QString &zipPath, const QString &extractDir);
    // 等待文件出现（用于解压后确保文件系统同步）
    bool waitForFile(const QString &filePath, int maxWaitMs = 500);

    // 文件浏览器
    void onFileBrowserClicked(const QModelIndex &index);
    void onFileBrowserDoubleClicked(const QModelIndex &index);
    void openFileFromBrowser(const QString &filePath, bool enterExtractDir = true);
    void openDirectoryInBrowser(const QString &dirPath);
    void loadFileToLogView(const QString &filePath);
    void loadMergeLogsFromPath(const QString &path, bool navigate = true);
    QString findControlEngineAnalysisRoot(const QString &basePath) const;

    // 多标签页管理
    void saveCurrentTabState();
    void restoreTabState(int index);
    void openInNewTab(const QString &path);
    void closeTab(int index);
    void detachTabToNewWindow(int index);
    void loadPathSmart(const QString &path);
    QString tabLabelForPath(const QString &path) const;
    void repopulateEventLists();
    QAbstractButton *makeTabCloseButton(int tabIndex);

    // 后台解析（成员方法，支持 generation 守卫）
    void startBackgroundParse(LogParserWorker *worker, QThread *thread, int generation);

    // 构造函数初始化方法
    void setupMainWindow();
    void setupCentralWidget();
    void setupEventDock();
    void setupSearchDock();
    void setupFileBrowserDock();
    void setupToolBar();
    void setupStatusBar();
    void setupStatusDock();
    void setupMenuBar();
    void setupUsageContainer();
    void setupConnections();
    void setupSearchCompleter();
    void applyButtonStyles();
    void applySavedFonts();
    // DB Viewer
    void setupDbViewerDock();
    void openDatabaseFile(const QString &filePath);
    void loadDbTable(const QString &tableName);
    // 搜索结果键值对提取并加入动态图表（由 searchAll 调用）
    void offerChartFromSearchResults();
private:
    // ==================== 工具栏控件 ====================
    QPushButton *analyzeControlButton;  // 专用分析按钮
    QPushButton *clearButton;
    QPushButton *newWindowButton;  // 新建窗口按钮

    SearchComboBox *searchCombo;
    QLineEdit *searchEdit;
    QStandardItemModel *searchDropdownModel = nullptr;
    QPushButton *searchAllButton;
    QPushButton *searchPrevButton;
    QPushButton *searchNextButton;
    QPushButton *pinButton;
    QAction *pinAction = nullptr;
    QPushButton *pinTextButton;
    QWidget *checkBoxContainer;
    QString version = "";
    // ==================== Dock 控件 ====================
    QDockWidget *eventDock;
    QListWidget *eventList;

    QDockWidget *searchDock;
    SearchResultTextView *searchResultView;
    QPushButton *clearSearchButton;
    QPushButton *closeSearchButton;
    // ==================== 中心控件 ====================
    QPlainTextEdit *logView;
    QStackedWidget *centralStack;   // 0=logView, 1=dbViewerWidget
    // ==================== 状态栏控件 ====================
    QStatusBar *statusBar;          // 状态栏
    QLabel *statusPathLabel;        // 左侧：路径/状态
    QLabel *statusInfoLabel;        // 右侧：持久信息（Image/IPK/SN/HW）
    QString modeText;
    // ==================== 数据 ====================
    QList<EventItem> allEvents;
    QStringList allLogLines;

    QList<int> searchResults;
    int currentSearchIndex;
    QList<QTextEdit::ExtraSelection> searchHighlights; // 全局搜索高亮，黄色

    int triggerCount;
    int flightCount;
    QStringList fixedHints;       // combined: pinned first, then base
    QStringList baseFixedHints;   // built-in fixed hints
    QStringList pinnedHints;      // user pinned hints (persistent)
    QStringList historyHints;
    // ==================== camera ====================
    QListWidget *cameraEventList;
    QList<EventItem> cameraEvents;
    EventTimelineWidget *eventTimeline = nullptr;  // 事件时间轴
    // ==================== status ====================
    QDockWidget *statusDock;
    QListWidget *heartbeatLostEventList;
    QPushButton *statusButton;   // 工具栏按钮（状态面板，含 camera 信息）
    QList<EventItem> statusEvents;
    QString sn;      //飞机sn号
    // ==================== battery ====================
    QWidget *statusContainer;
    BatteryChartWidget *batteryChart;
    QVector<BatteryTimeInfo> batteryinfo;
    QLabel *titleLabel;
    // ==================== Soc temp ====================
    SocTempChartWidget *socChart;
    QVector<SocTempInfo> soctmp;
    // ==================== Camera temp ====================
    CameraTempChartWidget *cameraTempChart;
    QVector<CameraTempSample> cameraTemps;
    // ============== 各个模块cpu/mem占用率 ================
    QWidget *usageContainer;
    ModuleUsageChart *usageChart;
    QVector<AllModuleUsage> allusage;
    // ==================== 文件浏览器 ====================
    QDockWidget *fileBrowserDock;
    QTreeView *fileBrowserTree;
    QFileSystemModel *fileSystemModel;
    QPushButton *fileBrowserButton;   // 工具栏按钮
    QString fileBrowserRootPath;       // 当前浏览根目录

    // ==================== DB Viewer ====================
    QWidget *dbViewerWidget;       // DB查看器页面（作为centralStack的第1页）
    QListWidget *dbTableList;
    QTableView *dbTableView;
    QSqlDatabase currentDb;
    QSqlTableModel *dbTableModel = nullptr;
    QString currentDbPath;

     // ==================== 动态图表 ====================
     QPushButton          *chartSearchButton  = nullptr;   // 工具栏：打开动态图表管理窗口
     DynamicChartManager  *m_chartManager     = nullptr;   // 单例管理窗口

    // 文本缩放：当前字体大小
    int logFontPointSize;

    // 字体设置
    QFont currentLogFont;      // 当前日志字体
    QFont currentEventFont;    // 当前事件列表字体
    QFont currentChartFont;     // 当前图表字体

    // 跨窗口共享字体（所有实例共用）
    static QFont s_sharedLogFont;
    static bool  s_sharedLogFontSet;  // 是否已被用户手动设置过

    // 全局按钮点击状态跟踪
    bool anyFileButtonClicked;

    // ==================== 颜色标记 ====================
    // 存储每个标记词的颜色索引：key=标记文本, value=颜色索引(0-4)
    QMap<QString, int> m_colorMarks;
    // 5种默认颜色
    static const QColor s_markColors[5];
    // logView 颜色标记高亮列表
    QList<QTextEdit::ExtraSelection> m_colorMarkHighlights;
    // searchResultView 颜色标记高亮列表（独立维护，不与搜索高亮冲突）
    QList<QTextEdit::ExtraSelection> m_searchViewColorHighlights;
    // 防抖 timer：避免连续触发时频繁全文扫描
    QTimer *m_colorMarkRebuildTimer = nullptr;

    // ==================== 后台解析线程 ====================
    QThread         *m_parseThread  = nullptr;
    LogParserWorker *m_parseWorker  = nullptr;
    QProgressBar    *m_progressBar  = nullptr;   // 状态栏进度条
    QStringList      m_pendingTopLogs;            // 等待解析的 top_log 列表

    // ==================== 多标签页 ====================
    ColoredTabBar   *m_tabBar           = nullptr;
    QWidget         *m_tabContainer     = nullptr;
    QVector<TabState> m_tabStates;                // 每个标签的状态快照
    int              m_currentTabIndex  = 0;      // 当前激活标签索引
    int              m_parseGeneration  = 0;      // 解析代次（防止旧线程污染新标签）
};

#endif // PRESSANALYZER_H
