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
#include <QRandomGenerator>
#include <QCheckBox>
#include <QCompleter>
#include <QStringListModel>
PressAnalyzer::PressAnalyzer(QWidget *parent)
    : QMainWindow(parent), currentSearchIndex(-1), toggleBtn(nullptr)
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
    eventDock = new QDockWidget("分析结果", this);
    eventDock->setWidget(eventList);
    eventDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    eventDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::LeftDockWidgetArea, eventDock);

    toggleBtn = new EventToggleButton(eventDock, this);
    toggleBtn->move(0, (height() - toggleBtn->height()) / 2);
    toggleBtn->show();

    // ==================== 搜索结果 Dock ====================
    searchResultList = new QListWidget(this);
    searchResultList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    searchDock = new QDockWidget(this);
    searchDock->setWidget(searchResultList);
    searchDock->setMinimumHeight(150);
    addDockWidget(Qt::BottomDockWidgetArea, searchDock);
    searchDock->hide();

    searchDock->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(searchDock, &QDockWidget::customContextMenuRequested, this, [=](const QPoint &pos){
        QMenu menu;
        QAction *clearAction = menu.addAction("Clear");
        QAction *closeAction = menu.addAction("Close");

        QAction *selected = menu.exec(searchDock->mapToGlobal(pos));
        if (selected == clearAction) {
            searchResults.clear();
            currentSearchIndex = 0;
            searchResultList->clear();
            highlightSearchResults(currentSearchIndex);
        } else if (selected == closeAction) {
            searchDock->hide();
        }
    });

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
    // -------------------- QCompleter 提示 --------------------
    fixedHints << "[rpc] Req:"
               << "[rpc] Req:254"
               << "[rpc] Req:249"
               << "capture out"
               << "MediaRequest_MediaRequestType_";

    QStringList completerHints = fixedHints + historyHints;

    QCompleter *completer = new QCompleter(completerHints, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    searchEdit->setCompleter(completer);

    searchEdit->installEventFilter(this);
    // -------------------- 添加到工具栏 --------------------
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
    statusBar->showMessage("就绪");

    // ==================== 信号连接 ====================
    connect(dirloadButton, &QPushButton::clicked, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(fileloadButton, &QPushButton::clicked, this, &PressAnalyzer::loadAndAnalyzeLog);
    connect(saveButton, &QPushButton::clicked, this, &PressAnalyzer::saveEventListToFile);
    connect(clearButton, &QPushButton::clicked, this, &PressAnalyzer::clearWindow);

    connect(eventList, &QListWidget::itemClicked, this, &PressAnalyzer::onEventClicked);
    connect(eventList, &QListWidget::itemDoubleClicked, this, &PressAnalyzer::onEventClicked);

    connect(searchAllButton, &QPushButton::clicked, this, [this](){
        searchAll();
        if (!searchResults.isEmpty()) searchDock->show();
    });
    connect(searchPrevButton, &QPushButton::clicked, this, &PressAnalyzer::goToPrevSearch);
    connect(searchNextButton, &QPushButton::clicked, this, &PressAnalyzer::goToNextSearch);
    connect(searchResultList, &QListWidget::itemClicked, this, &PressAnalyzer::onSearchResultClicked);

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

    // ==================== 状态面板 ====================
    statusButton = new QPushButton("状态面板", this);
    toolBar->addWidget(statusButton);

    statusContainer = new QWidget(this);
    QVBoxLayout *vLayout = new QVBoxLayout(statusContainer);
    vLayout->setContentsMargins(5, 5, 5, 5);
    vLayout->setSpacing(0);

    titleLabel = new QLabel(statusContainer);
    QFont font = titleLabel->font();
    font.setBold(true);
    font.setPointSize(12);
    titleLabel->setFont(font);
    titleLabel->setAlignment(Qt::AlignLeft);
    titleLabel->setStyleSheet("color: red;");
    vLayout->addWidget(titleLabel);

    statusEventList = new QListWidget(statusContainer);
    statusEventList->setMaximumHeight(50);
    vLayout->addWidget(statusEventList);

    batteryChart = new BatteryWidget(statusContainer);
    vLayout->addWidget(batteryChart);

    socChart = new SocTempChart(statusContainer);
    socChart->setMinimumSize(600, 300);
    vLayout->addWidget(socChart);

    vLayout->addStretch(1);
    statusContainer->setLayout(vLayout);

    // ==================== usageContainer（复选框 + 曲线图） ====================
    usageContainer = new QWidget(this);
    QVBoxLayout *usageLayout = new QVBoxLayout(usageContainer);
    usageLayout->setContentsMargins(0, 0, 0, 0);
    usageLayout->setSpacing(0);

    // ---------------- 曲线图 ----------------
    usageChart = new ModuleUsageChart(usageContainer);
    usageChart->setMinimumSize(700, 300);

    // ---------------- 复选框容器 ----------------
    checkBoxContainer = new QWidget(this);
    QGridLayout *gridLayout = new QGridLayout(checkBoxContainer);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setHorizontalSpacing(2);
    gridLayout->setVerticalSpacing(5);

    const auto &moduleKeys = usageChart->getModuleVisibility().keys();
    int total = moduleKeys.size();
    int rows = 5;                              // 固定5行
    int cols = (total + rows - 1) / rows;      // 每行列数自动计算
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

    // ---------------- 添加到布局 ----------------
    // 如果复选框要在上方，先添加复选框，再添加图表
    usageLayout->addWidget(checkBoxContainer);
    usageLayout->addWidget(usageChart);
    usageLayout->addStretch(1);
    usageContainer->setLayout(usageLayout);
    // ==================== 与状态面板组合 ====================
    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(statusContainer);
    mainSplitter->addWidget(usageContainer);
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 2);

    statusDock = new QDockWidget("状态面板", this);
    statusDock->setWidget(mainSplitter);
    statusDock->setAllowedAreas(Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, statusDock);
    statusDock->setMinimumWidth(800);
    statusDock->hide();

    connect(statusButton, &QPushButton::clicked, this, [this](){
        if (!statusDock->isVisible()) {
            statusDock->show();
            eventDock->hide();
        } else {
            statusDock->hide();
            eventDock->show();
        }
    });

    connect(statusEventList, &QListWidget::itemClicked, this, &PressAnalyzer::onStatusEventClicked);
}

// ---------------- eventFilter ----------------
bool PressAnalyzer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == searchEdit && event->type() == QEvent::FocusIn) {
        if (searchEdit->completer()) {
            searchEdit->completer()->setCompletionPrefix(""); // 显示完整列表
            searchEdit->completer()->complete();
        }
    }
    return QObject::eventFilter(obj, event);
}

// 5. 在搜索或确认输入时，记录历史
void PressAnalyzer::addSearchHistory(const QString &text)
{
    if (text.isEmpty()) return;
    if (!historyHints.contains(text)) {
        historyHints.prepend(text);        // 添加到历史开头
        if (historyHints.size() > 50)      // 限制历史数量
            historyHints.removeLast();

        // 更新 completer 数据源
        QStringList hints = fixedHints + historyHints;
        QCompleter *c = searchEdit->completer();

        // 获取原 model 并转换
        QStringListModel *model = qobject_cast<QStringListModel*>(c->model());
        if (!model) {
            model = new QStringListModel(hints, c);
            c->setModel(model);
        } else {
            model->setStringList(hints);
        }
    }
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
        // ==================== drone SN ====================
        if (line.contains("[I|System]: SN:", Qt::CaseInsensitive)) {
            int idx = line.indexOf("[I|System]: SN:");
            if (idx != -1) {
                sn = line.mid(idx + QString("[I|System]: SN:").length()).trimmed();

            }
        }
        // ==================== press once power key ====================
        if (line.contains("press once power key", Qt::CaseInsensitive)) {
            triggerCount++;
            QString timestamp;
            QRegularExpression tsRx(R"(\[\d+\.\d+\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*\])");
            QRegularExpressionMatch match = tsRx.match(line);
            if (match.hasMatch()) {
                timestamp = match.captured(1);
            }

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
                QString display = QString("%1 | %2# FC STATE -> %3")
                                      .arg(lineNumber, 6, 10, QChar(' '))
                                      .arg(flightCount)
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

        // ==================== insertMediaDataIntoDb ====================
        if (line.contains("insertMediaDataIntoDb insert media sql", Qt::CaseInsensitive)) {
            int idx = line.indexOf("insertMediaDataIntoDb insert media sql");
            if (idx != -1) {
                QString content = line.mid(idx);

                // 提取方括号内的 SQL
                QRegExp rx("\\[(.*)\\]");
                if (rx.indexIn(content) != -1) {
                    QString bracketContent = rx.cap(1);

                    // 匹配 VALUES(...) 中的单引号内容或数字
                    QRegExp valueRx("'([^']*)'|\\b(\\d+)\\b");
                    int pos = 0;
                    QStringList values;
                    while ((pos = valueRx.indexIn(bracketContent, pos)) != -1) {
                        if (!valueRx.cap(1).isEmpty())
                            values << valueRx.cap(1);  // 引号中的字符串
                        else
                            values << valueRx.cap(2);  // 数字
                        pos += valueRx.matchedLength();
                    }

                    if (values.size() >= 4) {
                        QString uuid = values[0];          // uuid
                        int type = values[1].toInt();      // type
                        QString path = values[3];          // path
                        QString typeStr = typeToString(type);

                        // 美化 path 显示
                        QString displayPath;
                        if (path.startsWith("/media/internal/")) {
                            displayPath = "Internal:" + path.mid(QString("/media/internal/").length());
                        } else if (path.startsWith("/media/external/")) {
                            displayPath = "External:" + path.mid(QString("/media/external/").length());
                        } else {
                            displayPath = path;
                        }

                        // 构造展示字符串
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
        // ==================== manual_control_takeover_request ====================
        if (line.contains("manual_control_takeover_request", Qt::CaseInsensitive)) {
            QString display = QString("%1 | %2# 模式:%3 -> MANUAL")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(flightCount)
                                  .arg(modeText);
            addEventToList(triggerCount, lineNumber-1, display);
            continue;
        }
        // ==================== camera status ====================
        parseCameraStatus(lineNumber, line);
        // ==================== status 面板 ====================
        parseStatusHeartbeat(lineNumber, line);
        parseStatusBattery(lineNumber, line);
        parseStatusSocTemp(lineNumber, line);
    }
}
QString getTopFilePath(const QString &selectedFilePath)
{
    // 假设 top 日志都在 ../system_log/top_log/ 下
    // 获取文件名
    QFileInfo fi(selectedFilePath);

    // 构造 top 文件路径
    QString topPath = fi.absolutePath() + "/../system_log/top_log/top.1.log";
    topPath = QFileInfo(topPath).canonicalFilePath();
    return topPath;
}

// loadAndAnalyzeLog 保持之前逻辑
void PressAnalyzer::loadAndAnalyzeLog()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择日志文件", "", "日志文件 (*.txt *.log);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    statusBar->showMessage(QString("路径: %1").arg(filePath));
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    statusEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    searchResults.clear();
    searchResultList->clear();
    batteryinfo.clear();
    allusage.clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QStringList lines;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    analyzeFile(filePath, lineNumber, currentTakeoffTime, lines, inRecvException, recvExceptionLines);

    logView->setPlainText(lines.join("\n"));
    titleLabel->setText(QString("心跳丢失次数:%1").arg(statusEventList->count()));
    batteryChart->setData(batteryinfo);
    socChart->addData(soctmp);
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3").arg(sn).arg(triggerCount).arg(flightCount));

    //解析cpu/mem占用率，绘制图案
    QString topFilePath = getTopFilePath(filePath);
    parseTopFile(topFilePath);

    usageChart->setData(allusage);
}

void PressAnalyzer::loadAndAnalyzeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志文件或目录", "");
    if (path.isEmpty()) return;

    statusBar->showMessage(QString("路径: %1").arg(path));
    QFileInfo info(path);

    auto collectLogs = [](const QString &baseDir, const QString &subDir, const QString &logPattern, const QString &zipPattern) -> QStringList {
        QStringList result;
        QDir dir(baseDir + "/" + subDir);
        if (!dir.exists()) return result;

        // 获取日志文件
        QStringList logFiles = dir.entryList(QStringList() << logPattern, QDir::Files);

        // 如果没有日志，但存在 zip 文件，解压
        if (logFiles.isEmpty()) {
            QStringList zipFiles = dir.entryList(QStringList() << zipPattern, QDir::Files);
            for (const QString &zipName : zipFiles) {
                QString zipPath = dir.filePath(zipName);
                QProcess unzipProcess;
                QStringList args;
#ifdef Q_OS_WIN
                args << "x" << zipPath << "-o" + dir.absolutePath();
                unzipProcess.start("7z.exe", args);
#else
                args << zipPath << "-d" << dir.absolutePath();
                unzipProcess.start("unzip", args);
#endif
                unzipProcess.waitForFinished(-1);
            }
            // 解压完重新获取日志文件列表
            logFiles = dir.entryList(QStringList() << logPattern, QDir::Files);
        }

        if (logFiles.isEmpty()) return result;

        // 按自然顺序排序
        QStringList sortedFiles;
        QList<QPair<int, QString>> numberedFiles;
        QString lastFile;
        for (const QString &f : logFiles) {
            // 生成临时变量
            QString baseName = logPattern.left(logPattern.indexOf('*')); // control_engine
            if (f == baseName + ".log") {
                lastFile = f;
            } else {
                // 构造正则表达式匹配 control_engine.1.log、control_engine.2.log ...
                QRegExp rx(baseName + "\\.(\\d+)\\.log");
                if (rx.indexIn(f) != -1) {
                    int num = rx.cap(1).toInt();
                    numberedFiles.append(qMakePair(num, f));
                }
            }
        }

        std::sort(numberedFiles.begin(), numberedFiles.end(),
                  [](const QPair<int, QString> &a, const QPair<int, QString> &b){ return a.first < b.first; });

        for (const auto &p : numberedFiles)
            sortedFiles << dir.filePath(p.second);

        if (!lastFile.isEmpty())
            sortedFiles << dir.filePath(lastFile);

        return sortedFiles;
    };

    QStringList controlLogs, topLogs;

    if (info.isDir()) {
        controlLogs = collectLogs(path, "control_engine_log", "control_engine*.log", "control_engine*.log.zip");
        topLogs     = collectLogs(path, "system_log/top_log", "top*.log", "top*.log.zip");

        if (controlLogs.isEmpty() && topLogs.isEmpty()) {
            QMessageBox::warning(this, "错误", "日志文件不存在");
            return;
        }
    } else if (info.isFile()) {
        controlLogs << info.filePath();
    }

    // ---------------- 公共解析部分 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    statusEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    searchResults.clear();
    searchResultList->clear();
    batteryinfo.clear();
    allusage.clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QStringList lines;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    // 分开解析 control_engine_log
    for (const QString &filePath : controlLogs) {
        analyzeFile(filePath, lineNumber, currentTakeoffTime, lines, inRecvException, recvExceptionLines);
    }


    logView->setPlainText(lines.join("\n"));
    titleLabel->setText(QString("心跳丢失次数:%1").arg(statusEventList->count()));
    batteryChart->setData(batteryinfo);
    socChart->addData(soctmp);
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3").arg(sn).arg(triggerCount).arg(flightCount));
    // 分开解析 top_log
    for (const QString &filePath : topLogs) {
        parseTopFile(filePath);
    }
    usageChart->setData(allusage);
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
    searchDock->hide();
    currentSearchIndex = -1;
    triggerCount = 0;
    flightCount = 0;
    cameraEventList->clear();
    cameraDock->hide();
    statusEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    statusDock->hide();
    batteryinfo.clear();
    logView->moveCursor(QTextCursor::Start);   // 光标移到开头
    QTextCursor cursor = logView->textCursor();
    cursor.clearSelection();                    // 取消选中
    logView->setTextCursor(cursor);
    allusage.clear();
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
            color = colorPool[QRandomGenerator::global()->bounded(colorPool.size())];

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
    QString combined = keys.join(", ");
    combined.remove(QRegularExpression("[\\x00-\\x1F]"));                 // 控制字符
    combined.replace(QRegularExpression(R"(\\(?=[\[\]\(\)\{\}<>\/*"']))"), "");
    addSearchHistory(combined);
}

// 高亮搜索结果
void PressAnalyzer::highlightSearchResults(int currentIndex /* = -1 */)
{
    if (searchResults.isEmpty()) return;

    QTextDocument* doc = logView->document();

    // ---------------- 1. 清除旧的高亮 ----------------
    QTextCursor clearCursor(doc);
    clearCursor.select(QTextCursor::Document);
    QTextCharFormat clearFormat;
    clearFormat.setBackground(Qt::transparent);  // 背景透明
    clearFormat.setForeground(Qt::black);        // 恢复默认前景色（根据需要调整）
    clearCursor.setCharFormat(clearFormat);
    highlightAllEvents();
    // ---------------- 2. 生成关键字颜色 ----------------
    QStringList keys = searchEdit->text().trimmed().split('|', Qt::SkipEmptyParts);

    QVector<QColor> colors;
    for (int i = 0; i < keys.size(); ++i) {
        if (i == 0)
            colors.append(Qt::yellow);
        else if (i == 1)
            colors.append(Qt::green);
        else
            colors.append(QColor(
                173 + QRandomGenerator::global()->bounded(80),
                216 + QRandomGenerator::global()->bounded(39),
                230 + QRandomGenerator::global()->bounded(25)
                ));
    }

    // ---------------- 3. 遍历匹配并高亮 ----------------
    for (int lineNumber : searchResults) {
        QTextBlock block = doc->findBlockByNumber(lineNumber);
        if (!block.isValid()) continue;

        QString lineText = block.text();
        for (int k = 0; k < keys.size(); ++k) {
            // ✅ 转义关键词，避免正则元字符导致误匹配
            QString pattern = QRegExp::escape(keys[k]);
            QRegExp rx(pattern, Qt::CaseInsensitive);

            int pos = 0;
            while ((pos = rx.indexIn(lineText, pos)) != -1) {
                QTextCursor cursor(block);
                cursor.setPosition(block.position() + pos);
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, rx.cap(0).length());

                QTextCharFormat fmt;
                fmt.setBackground(colors[k]);
                fmt.setForeground(Qt::black);
                cursor.setCharFormat(fmt);

                pos += rx.cap(0).length();
            }
        }
    }

    // ---------------- 4. 跳转到当前选中行 ----------------
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
        statusEventList->addItem(item);
        return;
    }

}
void PressAnalyzer::parseStatusBattery(int lineNumber, const QString &line)
{
    // ---------------- 解析电池信息 ----------------
    if (line.contains("battery info", Qt::CaseInsensitive)) {
        BatteryTimeInfo timeinfo;
        // 提取时间戳和日期时间
        QRegExp rxTimestamp("\\[(\\d+\\.\\d+)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
        if (rxTimestamp.indexIn(line) != -1) {
            QString tsStr = rxTimestamp.cap(1);   // 9733.000
            QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

            timeinfo.timestamp = QDateTime::fromString(dtStr, "yyyy-MM-dd HH:mm:ss");

            // 补上毫秒部分
            double tsDouble = tsStr.toDouble();
            int msecs = static_cast<int>((tsDouble - static_cast<int>(tsDouble)) * 1000);
            timeinfo.timestamp = timeinfo.timestamp.addMSecs(msecs);
        }
        timeinfo.info = parseBatteryInfo(line);
        batteryinfo.push_back(timeinfo);
    }
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
void PressAnalyzer::onStatusEventClicked(QListWidgetItem *item)
{
    highlightAllEvents();
    int row = statusEventList->row(item);
    if (row < 0 || row >= statusEvents.size()) return;

    int lineNumber =  statusEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}

void PressAnalyzer::parseStatusSocTemp(int lineNumber, const QString &line)
{
    // 如果行中不包含关键字，直接跳过
    if (!line.contains("get soc max temp")) {
        return;
    }

    SocTempInfo info;

    // 提取时间戳和日期时间
    QRegExp rxTimestamp("\\[(\\d+\\.\\d+)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
    if (rxTimestamp.indexIn(line) != -1) {
        QString tsStr = rxTimestamp.cap(1);   // 9733.000
        QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

        info.timestamp = QDateTime::fromString(dtStr, "yyyy-MM-dd HH:mm:ss");

        // 补上毫秒部分
        double tsDouble = tsStr.toDouble();
        int msecs = static_cast<int>((tsDouble - static_cast<int>(tsDouble)) * 1000);
        info.timestamp = info.timestamp.addMSecs(msecs);
    }

    // 提取 max temp
    QRegExp rxMax("get soc max temp\\s*:\\s*(\\d+)");
    if (rxMax.indexIn(line) != -1) {
        info.maxTemp = rxMax.cap(1).toInt();
    }

    // 提取 core temp
    QRegExp rxCore("core temp\\s*:\\s*([0-9:]+)");
    if (rxCore.indexIn(line) != -1) {
        QStringList temps = rxCore.cap(1).split(":");
        for (const QString &t : temps) {
            info.coreTemps.append(t.toInt());
        }
    }

    // 保存到成员 QVector
    soctmp.append(info);
}


// 将 "top - 14:03:27 ..." 这一行提取时间
QDateTime PressAnalyzer::parseTopTime(const QString &line) {
    QRegExp rx("top - (\\d{2}:\\d{2}:\\d{2})");
    if (rx.indexIn(line) != -1) {
        QString timeStr = rx.cap(1);
        QTime t = QTime::fromString(timeStr, "HH:mm:ss");
        return QDateTime(QDate::currentDate(), t); // 使用当天日期
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
    }

    // 最后一组数据也要存进去
    if (hasData) {
        allusage.append(usage);
    }
}
