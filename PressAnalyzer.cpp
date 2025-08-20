#include "PressAnalyzer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QSplitter>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QColor>
#include <QTextBlock>
#include <QRegExp>
#include <QDebug>
#include <QProcess>

PressAnalyzer::PressAnalyzer(QWidget *parent)
    : QMainWindow(parent), currentSearchIndex(-1)
{
    setWindowTitle("Hover日志分析助手");
    setWindowIcon(QIcon(":/new/image/logo.icns"));
    resize(1200, 700);

    triggerCount = 0;
    flightCount = 0;

    // ==================== 中心控件 ====================
    logView = new QPlainTextEdit(this);
    setCentralWidget(logView);

    // ==================== 事件列表 Dock ====================
    eventList = new QListWidget(this);
    QDockWidget *eventDock = new QDockWidget("分析结果列表", this);
    eventDock->setWidget(eventList);
    eventDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    eventDock->setFeatures(QDockWidget::NoDockWidgetFeatures); // 禁止浮动
    addDockWidget(Qt::LeftDockWidgetArea, eventDock);

    // ==================== 搜索结果 Dock ====================
    searchResultList = new QListWidget(this);
    searchResultList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 清除和关闭按钮（竖排文字）
    clearSearchButton = new QPushButton("C\nL\nE\nA\nR", this);
    closeSearchButton = new QPushButton("C\nL\nO\nS\nE", this);
    QFont btnFont = clearSearchButton->font();
    btnFont.setPointSize(9);
    clearSearchButton->setFont(btnFont);
    closeSearchButton->setFont(btnFont);

    clearSearchButton->setFixedWidth(30);
    closeSearchButton->setFixedWidth(30);

    QWidget *searchButtonWidget = new QWidget(this);
    QVBoxLayout *buttonLayout = new QVBoxLayout(searchButtonWidget);
    buttonLayout->setContentsMargins(2, 2, 2, 2);
    buttonLayout->setSpacing(2);
    buttonLayout->addWidget(clearSearchButton, 0, Qt::AlignTop);
    buttonLayout->addStretch();
    buttonLayout->addWidget(closeSearchButton, 0, Qt::AlignBottom);

    QWidget *searchResultWidget = new QWidget(this);
    QHBoxLayout *searchResultLayout = new QHBoxLayout(searchResultWidget);
    searchResultLayout->setContentsMargins(0, 0, 0, 0);
    searchResultLayout->setSpacing(0);
    searchResultLayout->addWidget(searchResultList);
    searchResultLayout->addWidget(searchButtonWidget);

    QDockWidget *searchDock = new QDockWidget(this);
    searchDock->setWidget(searchResultWidget);
    searchDock->setMinimumHeight(150);
    addDockWidget(Qt::BottomDockWidgetArea, searchDock);
    searchDock->hide();

    // ==================== 工具栏 ====================
    QToolBar *toolBar = addToolBar("主工具栏");
    dirloadButton = new QPushButton("选择Log目录分析", this);
    fileloadButton = new QPushButton("选择Log文件分析", this);
    saveButton = new QPushButton("保存分析结果", this);
    clearButton = new QPushButton("清除窗口", this);

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("输入搜索内容...");
    searchAllButton = new QPushButton("搜索", this);
    searchPrevButton = new QPushButton("向前", this);
    searchNextButton = new QPushButton("向后", this);

    toolBar->addWidget(dirloadButton);
    toolBar->addWidget(fileloadButton);
    toolBar->addWidget(saveButton);
    toolBar->addWidget(clearButton);
    toolBar->addSeparator();
    toolBar->addWidget(searchEdit);
    toolBar->addWidget(searchAllButton);
    toolBar->addWidget(searchPrevButton);
    toolBar->addWidget(searchNextButton);

    // ==================== 状态栏 ====================
    statusBar = new QStatusBar(this);
    setStatusBar(statusBar);
    statusBar->showMessage("就绪"); // 初始状态消息
    // ==================== 信号连接 ====================
    connect(dirloadButton, &QPushButton::clicked, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(fileloadButton, &QPushButton::clicked, this, &PressAnalyzer::loadAndAnalyzeLog);
    connect(saveButton, &QPushButton::clicked, this, &PressAnalyzer::saveEventListToFile);
    connect(clearButton, &QPushButton::clicked, this, &PressAnalyzer::clearWindow);

    connect(eventList, &QListWidget::itemClicked, this, &PressAnalyzer::onEventClicked);
    connect(eventList, &QListWidget::itemDoubleClicked, this, &PressAnalyzer::onEventClicked);

    connect(searchAllButton, &QPushButton::clicked, this, [this, searchDock](){
        searchAll();
        if (!searchResults.isEmpty()) searchDock->show();
    });
    connect(searchPrevButton, &QPushButton::clicked, this, &PressAnalyzer::goToPrevSearch);
    connect(searchNextButton, &QPushButton::clicked, this, &PressAnalyzer::goToNextSearch);
    connect(searchResultList, &QListWidget::itemClicked, this, &PressAnalyzer::onSearchResultClicked);

    connect(clearSearchButton, &QPushButton::clicked, this, [this](){
        searchResults.clear();
        currentSearchIndex = 0;
        searchResultList->clear();
        highlightSearchResults(currentSearchIndex);
    });
    connect(closeSearchButton, &QPushButton::clicked, searchDock, &QDockWidget::hide);

    // ==================== Camera按钮 ====================
    cameraButton = new QPushButton("Camera状态", this);
    toolBar->addWidget(cameraButton);

    cameraEventList = new QListWidget(this);
    cameraDock = new QDockWidget("Camera状态", this);
    cameraDock->setWidget(cameraEventList);
    cameraDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, cameraDock);
    cameraDock->hide();
    connect(cameraButton, &QPushButton::clicked, this, [this](){
        cameraDock->setVisible(!cameraDock->isVisible());
    });
    connect(cameraEventList, &QListWidget::itemClicked, this, &PressAnalyzer::onCameraEventClicked);
    // ==================== RPC按钮 ====================
    rpcButton = new QPushButton("RPC调用", this);
    toolBar->addWidget(rpcButton);

    rpcEventList = new QListWidget(this);
    rpcDock = new QDockWidget("RPC调用", this);
    rpcDock->setWidget(rpcEventList);
    rpcDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, rpcDock);
    rpcDock->hide();
    connect(rpcButton, &QPushButton::clicked, this, [this](){
        rpcDock->setVisible(!rpcDock->isVisible());
    });
    connect(rpcEventList, &QListWidget::itemClicked, this, &PressAnalyzer::onRpcEventClicked);
    splitDockWidget(cameraDock, rpcDock, Qt::Horizontal);
}

// typeToString 函数保持不变
QString typeToString(int type) {
    switch(type) {
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
}

void PressAnalyzer::addEventToList(int triggerCount, int lineNumber, const QString &display)
{
    // 获取 viewLog 对应 block
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);

    allEvents.push_back({lineNumber, display, block});

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
}

void PressAnalyzer::analyzeFile(const QString &filePath, int &lineNumber,
                                QDateTime &currentTakeoffTime,
                                QStringList &lines,
                                bool &inRecvException,
                                QStringList &recvExceptionLines)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法打开文件：" + filePath);
        return;
    }

    QTextStream in(&file);

    while (!in.atEnd()) {
        QString line = in.readLine();
        allLogLines << line;
        lineNumber++;
        QString numberedLine = QString("%1 %2")
                                   .arg(lineNumber, 6, 10, QChar(' '))
                                   .arg(line);
        lines << numberedLine;

        // ==================== press once power key ====================
        if (line.contains("press once power key", Qt::CaseInsensitive)) {
            triggerCount++;
            QString timestamp;
            QRegExp tsRx("\\[\\d+\\.\\d+\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\]");
            if (tsRx.indexIn(line) != -1)
                timestamp = tsRx.cap(1);

            QString display = QString("%1 | %2# press power key : [%3]")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(triggerCount)
                                  .arg(timestamp);
            addEventToList(triggerCount, lineNumber, display);
        }

        // ==================== 起飞事件 ====================
        if (line.contains("start takeoff. powerkey trigger source", Qt::CaseInsensitive)) {
            flightCount++;
            QString triggerText = "未知";
            QString modeText;
            QRegExp rx("trigger source: (\\d+) flight mode: (\\w+)", Qt::CaseInsensitive);
            if (rx.indexIn(line) != -1) {
                int src = rx.cap(1).toInt();
                modeText = rx.cap(2);
                if (src == 1) triggerText = "MCU";
                else if (src == 2) triggerText = "APP";
                else if (src == 3) triggerText = "RC";
            }
            QString display = QString("%1 | %2# starting takeoff : 方式:%3 模式:%4")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(flightCount)
                                  .arg(triggerText)
                                  .arg(modeText);
            addEventToList(triggerCount, lineNumber, display);
        }

        // ==================== fly_power: takeoff success ====================
        if (line.contains("fly_power: takeoff success", Qt::CaseInsensitive)) {
            QRegExp tsRx("\\[\\d+\\.\\d+\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\]");
            if (tsRx.indexIn(line) != -1)
                currentTakeoffTime = QDateTime::fromString(tsRx.cap(1), "yyyy-MM-dd HH:mm:ss");

            QString display = QString("%1 | %2# takeoff success,Flying")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(flightCount);
            addEventToList(triggerCount, lineNumber, display);
        }

        // ==================== FC STATE ====================
        if (line.contains("Detected fc state changed to", Qt::CaseInsensitive)) {

            // 解析状态值
            QRegExp stateRx("Detected fc state changed to\\s+(\\d+)");
            if (stateRx.indexIn(line) != -1) {
                int stateValue = stateRx.cap(1).toInt();

                QString stateName;
                switch (stateValue) {
                case 0: stateName = "DISARM";    break;
                case 1: stateName = "ARM";       break;
                case 2: stateName = "TAKINGOFF"; break;
                case 3: stateName = "FLYING";    break;
                case 4: stateName = "LANDING";   break;
                default: stateName = QString("UNKNOWN(%1)").arg(stateValue); break;
                }

                // 直接显示当前状态
                QString display = QString("%1 | FC STATE -> %2")
                                      .arg(lineNumber, 6, 10, QChar(' '))
                                      .arg(stateName, -12);
                addEventToList(triggerCount, lineNumber, display);
            }
        }
        // ==================== fly_power: will landing ====================
        if (line.contains("fly_power: will landing", Qt::CaseInsensitive)) {
            QDateTime landingTime;
            QRegExp tsRx("\\[\\d+\\.\\d+\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\]");
            if (tsRx.indexIn(line) != -1)
                landingTime = QDateTime::fromString(tsRx.cap(1), "yyyy-MM-dd HH:mm:ss");

            if (currentTakeoffTime.isValid() && landingTime.isValid()) {
                qint64 flightSeconds = currentTakeoffTime.secsTo(landingTime);
                QString display = QString("%1 | %2# will Landing, Flight Duration: %3 seconds")
                                      .arg(lineNumber, 6, 10, QChar(' '))
                                      .arg(flightCount)
                                      .arg(flightSeconds);
                addEventToList(triggerCount, lineNumber, display);
                currentTakeoffTime = QDateTime();
            }
        }

        // ==================== storingMediaFileInformation ====================
        if (line.contains("storingMediaFileInformation the sqlvalue is", Qt::CaseInsensitive)) {
            int idx = line.indexOf("storingMediaFileInformation the sqlvalue is");
            if (idx != -1) {
                QString content = line.mid(idx);
                QRegExp rx("\\[(.*)\\]");
                if (rx.indexIn(content) != -1) {
                    QString bracketContent = rx.cap(1);
                    QRegExp valueRx("'([^']*)'|\\b(\\d+)\\b");
                    int pos = 0;
                    QStringList values;
                    while ((pos = valueRx.indexIn(bracketContent, pos)) != -1) {
                        if (!valueRx.cap(1).isEmpty())
                            values << valueRx.cap(1);
                        else
                            values << valueRx.cap(2);
                        pos += valueRx.matchedLength();
                    }

                    if (values.size() >= 4) {
                        QString uuid = values[0];
                        int type = values[1].toInt();
                        QString path = values[3];
                        QString typeStr = typeToString(type);
                        QString displayPath;
                        if (path.startsWith("/media/internal/")) {
                            displayPath = "Internal:" + path.mid(QString("/media/internal/").length());
                        } else if (path.startsWith("/media/external/")) {
                            displayPath = "External:" + path.mid(QString("/media/external/").length());
                        } else {
                            displayPath = path;
                        }
                        QString display = QString("%1 | %2# Media UUID:%3 Type:%4 %5")
                                              .arg(lineNumber, 6, 10, QChar(' '))
                                              .arg(flightCount)
                                              .arg(uuid)
                                              .arg(typeStr)
                                              .arg(displayPath);
                        addEventToList(triggerCount, lineNumber, display);
                    }
                }
            }
        }

        // ==================== recv exception ====================
        if (line.contains("recv exception :", Qt::CaseInsensitive)) {
            inRecvException = true;
            recvExceptionLines.clear();
            continue;
        }

        if (inRecvException) {
            recvExceptionLines << line.trimmed();
            if (line.contains('}')) {
                inRecvException = false;
                for (const QString &l : recvExceptionLines) {
                    if (l.startsWith("event:", Qt::CaseInsensitive) ||
                        l.startsWith("errors:", Qt::CaseInsensitive)) {
                        QString content = l.trimmed();
                        QString display = QString("%1 | %2# %3")
                                              .arg(lineNumber, 6, 10, QChar(' '))
                                              .arg(flightCount)
                                              .arg(content);
                        addEventToList(triggerCount, lineNumber-1, display);
                    }
                }
            }
            continue;
        }

        // ==================== camera status ====================
        parseCameraStatus(lineNumber, line);
        // ==================== rpc 调用 status ====================
        parseRpcEvent(lineNumber, line);
    }
}

// loadAndAnalyzeLog 保持之前逻辑
void PressAnalyzer::loadAndAnalyzeLog()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择日志文件", "", "日志文件 (*.txt *.log);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    statusBar->showMessage(QString("路径: %1").arg(filePath));
    allLogLines.clear();
    allEvents.clear();
    eventList->clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QStringList lines;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    analyzeFile(filePath, lineNumber, currentTakeoffTime, lines, inRecvException, recvExceptionLines);

    logView->setPlainText(lines.join("\n"));
    setWindowTitle(QString("请求起飞次数: %1 | 成功起飞次数: %2").arg(triggerCount).arg(flightCount));
}
void PressAnalyzer::loadAndAnalyzeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志文件或目录", "");
    if (path.isEmpty()) return;

    statusBar->showMessage(QString("路径: %1").arg(path));
    QStringList filesToOpen;
    QFileInfo info(path);
    if (info.isDir()) {
        // 如果是目录，查找 control_engine_log 子目录
        QDir logDir(info.filePath() + "/control_engine_log");
        if (!logDir.exists()) {
            QMessageBox::warning(this, "错误", "目录下没有 control_engine_log 子目录");
            return;
        }

        // 获取 control_engine*.log 文件
        QStringList logFiles = logDir.entryList(QStringList() << "control_engine*.log", QDir::Files);

        // 如果没有 log 文件，但存在 zip 文件，先解压
        if (logFiles.isEmpty()) {
            QStringList zipFiles = logDir.entryList(QStringList() << "control_engine*.log.zip", QDir::Files);
            if (!zipFiles.isEmpty()) {
                for (const QString &zipName : zipFiles) {
                    QString zipPath = logDir.filePath(zipName);
                    // 调用系统 unzip 命令解压到 logDir
                    QProcess unzipProcess;
                    QStringList args;
#ifdef Q_OS_WIN
                    // Windows 需要指定 unzip 工具路径，例如使用 7zip 命令行
                    args << "x" << zipPath << "-o" + logDir.absolutePath();
                    unzipProcess.start("7z.exe", args);
#else
                    args << zipPath << "-d" << logDir.absolutePath();
                    unzipProcess.start("unzip", args);
#endif
                    unzipProcess.waitForFinished(-1); // 等待解压完成
                }
                // 解压完重新获取 log 文件列表
                logFiles = logDir.entryList(QStringList() << "control_engine*.log", QDir::Files);
            }
        }

        if (logFiles.isEmpty()) {
            QMessageBox::warning(this, "错误", "control_engine_log 下没有日志文件");
            return;
        }

        // 按自然顺序排序：control_engine.1.log ... control_engine.N.log，最后 control_engine.log
        QStringList sortedFiles;
        QList<QPair<int, QString>> numberedFiles;
        QString lastFile;
        for (const QString &f : logFiles) {
            if (f == "control_engine.log") {
                lastFile = f;
            } else {
                QRegExp rx("control_engine\\.(\\d+)\\.log");
                if (rx.indexIn(f) != -1) {
                    int num = rx.cap(1).toInt();
                    numberedFiles.append(qMakePair(num, f));
                }
            }
        }

        // 按编号排序
        std::sort(numberedFiles.begin(), numberedFiles.end(),
                  [](const QPair<int, QString> &a, const QPair<int, QString> &b){
                      return a.first < b.first;
                  });

        for (const auto &p : numberedFiles)
            sortedFiles << logDir.filePath(p.second);

        if (!lastFile.isEmpty())
            sortedFiles << logDir.filePath(lastFile);

        filesToOpen = sortedFiles;

    } else if (info.isFile()) {
        filesToOpen << info.filePath();
    }

    // ---------------- 公共解析部分 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QStringList lines;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    for (const QString &filePath : filesToOpen) {
        analyzeFile(filePath, lineNumber, currentTakeoffTime, lines, inRecvException, recvExceptionLines);
    }

    logView->setPlainText(lines.join("\n"));
    setWindowTitle(QString("请求起飞次数: %1 | 成功起飞次数: %2").arg(triggerCount).arg(flightCount));
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
        color = QColor(178, 34, 34); // 橙色 - 流+预览+录像同时开启
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
    cursor.select(QTextCursor::LineUnderCursor);
    cursor.setCharFormat(fmt);
}
// 遍历所有事件，给 viewLog 对应行上色
void PressAnalyzer::highlightAllEvents()
{
    // 合并 allEvents 和 cameraEvents 并按行号排序
    QList<EventItem> mergedEvents = allEvents;
    mergedEvents += cameraEvents;  // 合并两个列表
    mergedEvents += rpcEvents;  // 合并两个列表

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
    highlightAllEvents();
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
    allEvents.clear();
    allLogLines.clear();
    eventList->clear();
    logView->clear();
    searchResultList->clear();
    searchResultList->hide();
    searchResults.clear();
    currentSearchIndex = -1;
    triggerCount = 0;
    flightCount = 0;
    cameraEventList->clear();
    cameraDock->hide();
    // 隐藏搜索结果窗口
    QDockWidget* searchDock = qobject_cast<QDockWidget*>(searchResultList->parentWidget()->parentWidget());
    if (searchDock) {
        searchDock->hide();
    }
    logView->moveCursor(QTextCursor::Start);   // 光标移到开头
    QTextCursor cursor = logView->textCursor();
    cursor.clearSelection();                    // 取消选中
    logView->setTextCursor(cursor);
    statusBar->showMessage("就绪");
    setWindowTitle("日志分析工具");
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
    searchResultList->clear();

    QString text = searchEdit->text().trimmed();
    if (text.isEmpty()) return;

    // 分割多关键字，用 | 分隔
    QStringList keys = text.split('|', Qt::SkipEmptyParts);

    // 自动转义每个关键字的正则元字符
    for (int i = 0; i < keys.size(); ++i) {
        keys[i] = escapeRegExp(keys[i]);
    }

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
    for (int i = 0; i < keys.size(); ++i) {
        QRegExp rx(keys[i], Qt::CaseInsensitive);

        QColor color;
        if (i == 0)
            color = Qt::yellow;
        else if (i == 1)
            color = Qt::green;
        else
            color = colorPool[qrand() % colorPool.size()];

        patterns.append(qMakePair(rx, color));
    }

    // 设置高亮 delegate（复用已有的）
    auto *delegate = qobject_cast<HighlightDelegate*>(searchResultList->itemDelegate());
    if (delegate) {
        delegate->setPatterns(patterns);
        searchResultList->viewport()->update();
    } else {
        delegate = new HighlightDelegate(patterns, searchResultList);
        searchResultList->setItemDelegate(delegate);
    }

    // 遍历日志行，匹配关键字
    for (int i = 0; i < allLogLines.size(); ++i) {
        bool matched = false;
        for (auto &p : patterns) {
            if (p.first.indexIn(allLogLines[i]) != -1) {
                matched = true;
                break;
            }
        }

        if (matched) {
            searchResults.push_back(i);
            QString itemText = QString("%1 | %2")
                                   .arg(i+1, 6, 10, QChar(' '))
                                   .arg(allLogLines[i]);
            searchResultList->addItem(new QListWidgetItem(itemText));
        }
    }

    if (searchResults.isEmpty()) {
        searchResultList->hide();
        return;
    }

    searchResultList->show();
    currentSearchIndex = 0;
    jumpToSearchIndex(currentSearchIndex);
}

// 高亮搜索结果
void PressAnalyzer::highlightSearchResults(int currentIndex /* = -1 */)
{
    if (searchResults.isEmpty()) return;

    QTextDocument* doc = logView->document();

    // 多关键字高亮（这里假设 keys 已经存储了搜索关键字）
    // 并且 colors 对应每个关键字的颜色
    QStringList keys = searchEdit->text().trimmed().split('|', Qt::SkipEmptyParts);

    QVector<QColor> colors;
    for (int i = 0; i < keys.size(); ++i) {
        if (i == 0)
            colors.append(Qt::yellow);
        else if (i == 1)
            colors.append(Qt::green);
        else
            colors.append(QColor(173 + qrand() % 80, 216 + qrand() % 39, 230 + qrand() % 25)); // 随机亮色
    }

    // 对每行搜索匹配词
    for (int lineNumber : searchResults) {
        QTextBlock block = doc->findBlockByNumber(lineNumber);
        if (!block.isValid()) continue;

        QString lineText = block.text();
        for (int k = 0; k < keys.size(); ++k) {
            QRegExp rx(keys[k], Qt::CaseInsensitive);
            int pos = 0;
            while ((pos = rx.indexIn(lineText, pos)) != -1) {
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + pos);
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, rx.cap(0).length());

                QTextCharFormat fmt;
                fmt.setBackground(colors[k]);
                fmt.setForeground(Qt::black);
                cursor.setCharFormat(fmt);

                pos += rx.cap(0).length(); // 移动到下一个匹配
            }
        }
    }

    // 当前选中行跳转并居中
    if (currentIndex >= 0 && currentIndex < searchResults.size()) {
        QTextBlock currentBlock = doc->findBlockByNumber(searchResults[currentIndex]);
        if (currentBlock.isValid()) {
            QTextCursor cursor(currentBlock);
            cursor.movePosition(QTextCursor::StartOfBlock);
            logView->setTextCursor(cursor);
            logView->centerCursor();
        }
    }
}


void PressAnalyzer::onSearchResultClicked(QListWidgetItem *item)
{
    int row = searchResultList->row(item);
    if (row < 0 || row >= searchResults.size()) return;
    currentSearchIndex = row;
    highlightSearchResults(currentSearchIndex);
}

void PressAnalyzer::jumpToSearchIndex(int index)
{
    if (index<0 || index>=searchResults.size()) return;
    currentSearchIndex = index;
    highlightSearchResults(currentSearchIndex);
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

        QString display = QString("%1 | status=%2, stream=%3, preview=%4, recording=%5, snapshot=%6")
                              .arg(lineNumber)
                              .arg(bitToStr(status))
                              .arg(bitToStr(stream))
                              .arg(bitToStr(preview))
                              .arg(bitToStr(recording))
                              .arg(bitToStr(snapshot));

        QListWidgetItem *item = new QListWidgetItem(display);

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

        if (!note.isEmpty()) {
            display += "   [" + note + "]";
            item->setText(display);
        }

        item->setBackground(bgColor);
        EventItem camEvent;
        camEvent.lineNumber = lineNumber;
        camEvent.display = display;
        camEvent.block = logView->document()->findBlockByNumber(lineNumber - 1);
        cameraEvents.push_back(camEvent);
        cameraEventList->addItem(item);
    }
}

void PressAnalyzer::parseRpcEvent(int lineNumber, const QString &line)
{
    QRegExp rx("\\[rpc\\] Req:\\d+ - \\{id: \\d+\\s+(\\w+)");
    if (rx.indexIn(line) == -1) return;

    QString rpcName = rx.cap(1);

    // ---------------- 心跳特殊处理 ----------------
    if (rpcName == "heartbeat_info_request") { // 心跳
        if (!heartbeatActive) {
            // 心跳开始
            QString display = QString("%1 | 心跳开始").arg(lineNumber);
            QListWidgetItem *item = new QListWidgetItem(display);
            item->setBackground(QColor(173, 216, 230)); // 浅蓝色
            rpcEventList->addItem(item);
            heartbeatActive = true;
        }
        lastHeartbeatLine = lineNumber;
        return; // 不显示每条心跳
    }

    // ---------------- 检测心跳断开 ----------------
    if (heartbeatActive && lineNumber - lastHeartbeatLine > 2) { // 超过2行没有心跳
        QString display = QString("%1 | 心跳断开").arg(lastHeartbeatLine);
        QListWidgetItem *item = new QListWidgetItem(display);
        item->setBackground(QColor(255, 182, 193)); // 浅红色
        rpcEventList->addItem(item);
        heartbeatActive = false;
    }

    // ---------------- 普通 RPC 显示 ----------------
    QString display = QString("%1 | %2").arg(lineNumber).arg(rpcName);
    QListWidgetItem *item = new QListWidgetItem(display);
    item->setBackground(Qt::white);

    EventItem rpcEvent;
    rpcEvent.lineNumber = lineNumber;
    rpcEvent.display = display;
    rpcEvent.block = logView->document()->findBlockByNumber(lineNumber - 1);
    rpcEvents.push_back(rpcEvent);

    rpcEventList->addItem(item);
}

void PressAnalyzer::onCameraEventClicked(QListWidgetItem *item)
{
    highlightAllEvents();
    int row = cameraEventList->row(item);
    if (row < 0 || row >= cameraEvents.size()) return;

    int lineNumber = cameraEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}
void PressAnalyzer::onRpcEventClicked(QListWidgetItem *item)
{
    highlightAllEvents();
    int row = rpcEventList->row(item);
    if (row < 0 || row >= rpcEvents.size()) return;

    int lineNumber =  rpcEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}
