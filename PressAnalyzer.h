#ifndef PRESSANALYZER_H
#define PRESSANALYZER_H

#include <QMainWindow>
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
#include "SearchResultHighlighter.h"
#include "SearchResultTextView.h"
#include "BatteryChartWidget.h"
#include "SocTempChartWidget.h"
#include "DockToggleButton.h"
#include "ModuleUsageChart.h"
#include "ChartStyleManager.h"
#include "CameraTempChartWidget.h"
#include <QSettings>
#include <QAction>
#include <QTreeView>
#include <QFileSystemModel>

class QStandardItemModel;

struct EventItem {
    int lineNumber;    // 日志行号
    QString display;   // 显示文本
    QTextBlock block;  // 对应 viewLog 的文本块
};

class PressAnalyzer : public QMainWindow
{
    Q_OBJECT

public:
    explicit PressAnalyzer(QWidget *parent = nullptr);

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
private:
    // 日志解析
    void analyzeFile(const QString &filePath,
                     int &lineNumber,
                     QDateTime &currentTakeoffTime,
                     QString &textBuffer,
                     bool &inRecvException,
                     QStringList &recvExceptionLines);

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
    bool eventFilter(QObject *obj, QEvent *event);
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

    // 构造函数初始化方法
    void setupMainWindow();
    void setupCentralWidget();
    void setupEventDock();
    void setupSearchDock();
    void setupFileBrowserDock();
    void setupToolBar();
    void setupStatusBar();
    void setupCameraDock();
    void setupStatusDock();
    void setupMenuBar();
    void setupUsageContainer();
    void setupConnections();
    void setupSearchCompleter();
    void applyButtonStyles();
    void applySavedFonts();
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
    DockToggleButton *toggleBtn;
    DockToggleButton *fileBrowserToggleBtn;
    // ==================== 中心控件 ====================
    QPlainTextEdit *logView;
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
    QDockWidget *cameraDock;
    QListWidget *cameraEventList;
    QPushButton *cameraButton;   // 工具栏按钮
    QList<EventItem> cameraEvents;
    // ==================== status ====================
    QDockWidget *statusDock;
    QListWidget *heartbeatLostEventList;
    QPushButton *statusButton;   // 工具栏按钮
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

    // 文本缩放：当前字体大小
    int logFontPointSize;

    // 字体设置
    QFont currentLogFont;      // 当前日志字体
    QFont currentEventFont;    // 当前事件列表字体
    QFont currentChartFont;     // 当前图表字体

    // 全局按钮点击状态跟踪
    bool anyFileButtonClicked;
};

#endif // PRESSANALYZER_H
