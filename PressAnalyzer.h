#ifndef PRESSANALYZER_H
#define PRESSANALYZER_H

#include <QMainWindow>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLineEdit>
#include <QDateTime>
#include <QStringList>
#include <QDockWidget>
#include <QToolBar>
#include <QStatusBar>
#include <QMenu>
#include <QTextBlock>
#include <QLabel>
#include "HighlightDelegate.h"
#include "BatteryWidget.h"
#include "SocTempWidget.h"
#include "EventToggleButton.h"
#include "AllModuleUsage.h"

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
    void saveEventListToFile();
    void clearWindow();

    // 事件列表操作
    void addEventToList(int triggerCount, int lineNumber, const QString &display);
    void onEventClicked(QListWidgetItem *item);

    // 搜索功能
    void searchAll();
    void goToPrevSearch();
    void goToNextSearch();
    void onSearchResultClicked(QListWidgetItem *item);
    void onCameraEventClicked(QListWidgetItem *item);
    void onStatusEventClicked(QListWidgetItem *item);
private:
    // 日志解析
    void analyzeFile(const QString &filePath,
                     int &lineNumber,
                     QDateTime &currentTakeoffTime,
                     QStringList &lines,
                     bool &inRecvException,
                     QStringList &recvExceptionLines);

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
private:
    // ==================== 工具栏控件 ====================
    QPushButton *dirloadButton;
    QPushButton *fileloadButton;
    QPushButton *saveButton;
    QPushButton *clearButton;

    QLineEdit *searchEdit;
    QPushButton *searchAllButton;
    QPushButton *searchPrevButton;
    QPushButton *searchNextButton;
    QWidget *checkBoxContainer;
    // ==================== Dock 控件 ====================
    QDockWidget *eventDock;
    QListWidget *eventList;

    QDockWidget *searchDock;
    QListWidget *searchResultList;
    QPushButton *clearSearchButton;
    QPushButton *closeSearchButton;
    EventToggleButton *toggleBtn;
    // ==================== 中心控件 ====================
    QPlainTextEdit *logView;
    // ==================== 状态栏控件 ====================
    QStatusBar *statusBar;          // 状态栏
    // ==================== 数据 ====================
    QList<EventItem> allEvents;
    QStringList allLogLines;

    QList<int> searchResults;
    int currentSearchIndex;

    int triggerCount;
    int flightCount;
    // ==================== camera ====================
    QDockWidget *cameraDock;
    QListWidget *cameraEventList;
    QPushButton *cameraButton;   // 工具栏按钮
    QList<EventItem> cameraEvents;
    // ==================== status ====================
    QDockWidget *statusDock;
    QListWidget *statusEventList;
    QPushButton *statusButton;   // 工具栏按钮
    QList<EventItem> statusEvents;
    QString sn;      //飞机sn号
    // ==================== battery ====================
    QWidget *statusContainer;
    BatteryWidget *batteryChart;
    QVector<BatteryTimeInfo> batteryinfo;
    QLabel *titleLabel;
    // ==================== Soc temp ====================
    SocTempChart *socChart;
    QVector<SocTempInfo> soctmp;
    // ============== 各个模块cpu/mem占用率 ================
    QWidget *usageContainer;
    ModuleUsageChart *usageChart;
    QVector<AllModuleUsage> allusage;
};

#endif // PRESSANALYZER_H
