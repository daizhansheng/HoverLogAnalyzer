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
#include <QShortcut>
#include <QKeyEvent>
#include <QTimer>
#include <QMenuBar>
#include <QAction>
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
    // 改浅选中颜色，避免蓝色过深
    searchResultList->setStyleSheet(
        "QListWidget{selection-background-color:#CCE8FF; selection-color:black;}\n"
        "QAbstractItemView::item:selected{background:#BBDFFF; color:black;}\n"
        "QAbstractItemView::item:selected:active{background:#BBDFFF; color:black;}\n"
        "QAbstractItemView::item:selected:!active{background:#E6F3FF; color:black;}\n"
        "QListWidget::item:hover{background:#EAF5FF;}"
    );

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
    dirloadButton = new QPushButton("选择Log目录", this);
    fileloadButton = new QPushButton("选择Log文件", this);
    saveButton = new QPushButton("保存分析结果", this);
    clearButton = new QPushButton("清除窗口", this);

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("输入搜索内容... ");
    searchEdit->setMinimumWidth(300);
    searchEdit->setStyleSheet(
        "QLineEdit { "
        "    padding: 5px; "
        "    border: 2px solid #CCCCCC; "
        "    border-radius: 5px; "
        "    background-color: white; "
        "    font-size: 12px; "
        "} "
        "QLineEdit:focus { "
        "    border-color: #4A90E2; "
        "    background-color: #F8F9FA; "
        "} "
        "QLineEdit:hover { "
        "    border-color: #999999; "
        "}"
    );
    searchAllButton = new QPushButton("搜索", this);
    searchPrevButton = new QPushButton("向前", this);
    searchNextButton = new QPushButton("向后", this);
    // -------------------- QCompleter 提示 --------------------
    fixedHints << "[rpc] Req:"
               << "[rpc] Req:254"
               << "[rpc] Req:249"
               << "capture out"
               << "MediaRequest_MediaRequestType_"
               << "GET_MEDIA_FILE media_file_transfer_request";

    QStringList completerHints = fixedHints + historyHints;

    QCompleter *completer = new QCompleter(completerHints, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    searchEdit->setCompleter(completer);

    searchEdit->installEventFilter(this);
    
    // 添加键盘快捷键支持
    QShortcut *searchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        searchEdit->setFocus();
        searchEdit->selectAll();
    });
    
    // 回车键触发搜索
    connect(searchEdit, &QLineEdit::returnPressed, this, [this]() {
        searchAll();
        if (!searchResults.isEmpty()) searchDock->show();
    });
    
    // 实时搜索提示：输入时自动更新提示
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.length() > 0) {
            // 延迟更新，避免频繁刷新
            QTimer::singleShot(200, this, &PressAnalyzer::updateCompleterWithSmartHints);
        }
    });
    
    // 统一浅色主题：集中管理色值
    struct ButtonTheme { QString bg; QString hover; QString pressed; QString fg; };
    const ButtonTheme themePrimary  {"#E8F3FF", "#D9ECFF", "#C6E2FF", "#1F2D3D"};
    const ButtonTheme themeSuccess  {"#EDF9E5", "#E0F3D3", "#CCE9BB", "#1F2D3D"};
    const ButtonTheme themeDanger   {"#FDECEA", "#F9DAD7", "#F3C5C1", "#611A15"};
    const ButtonTheme themeInfo     {"#F0EEFF", "#E6E3FF", "#D9D4FF", "#1F2D3D"};
    const ButtonTheme themeNeutral  {"#F4F4F5", "#ECECEC", "#E2E3E4", "#1F2D3D"};
    const ButtonTheme themeWarning  {"#FFF3E0", "#FFE4BA", "#FFDA9B", "#5C3B0A"};
    const ButtonTheme themeIndigo   {"#EDF2FF", "#E0E7FF", "#D0D8FF", "#1F2D3D"};

    // 按钮样式助手：扁平+自定义背景色；提升可读性（深色文本+浅色背景或较高对比度）
    auto styleButton = [](QPushButton *button,
                          const QString &bg,
                          const QString &hover,
                          const QString &pressed,
                          const QString &fg = QString("#1F2D3D")){
        if (!button) return;
        button->setFlat(true);
        button->setStyleSheet(
            QString(
                "QPushButton{"
                "  background-color:%1;"
                "  color:%4;"
                "  border:none;"
                "  border-radius:4px;"
                "  padding:6px 10px;"
                "}"
                "QPushButton:hover{background-color:%2;}"
                "QPushButton:pressed{background-color:%3;}"
            ).arg(bg, hover, pressed, fg)
        );
    };
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

    // 应用样式到已创建的按钮（统一浅色主题）
    styleButton(dirloadButton,   themePrimary.bg, themePrimary.hover, themePrimary.pressed, themePrimary.fg);   // 目录
    styleButton(fileloadButton,  themePrimary.bg, themePrimary.hover, themePrimary.pressed, themePrimary.fg);   // 文件（与目录同属主色）
    styleButton(saveButton,      themeSuccess.bg, themeSuccess.hover, themeSuccess.pressed, themeSuccess.fg);   // 保存
    styleButton(clearButton,     themeDanger.bg,  themeDanger.hover,  themeDanger.pressed,  themeDanger.fg);    // 清除
    styleButton(searchAllButton, themeInfo.bg,    themeInfo.hover,    themeInfo.pressed,    themeInfo.fg);      // 搜索
    styleButton(searchPrevButton,themeNeutral.bg, themeNeutral.hover, themeNeutral.pressed, themeNeutral.fg);   // 向前
    styleButton(searchNextButton,themeNeutral.bg, themeNeutral.hover, themeNeutral.pressed, themeNeutral.fg);   // 向后

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
    styleButton(cameraButton, themeWarning.bg, themeWarning.hover, themeWarning.pressed, themeWarning.fg);     // Camera

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
    styleButton(statusButton, themeIndigo.bg, themeIndigo.hover, themeIndigo.pressed, themeIndigo.fg);     // 状态

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

    // ==================== 菜单栏 ====================
    QMenu *fileMenu = menuBar()->addMenu("文件");
    QAction *actNewWindow = fileMenu->addAction("新建窗口");
    actNewWindow->setShortcut(QKeySequence::New);
    fileMenu->addSeparator();
    QAction *actOpenDir = fileMenu->addAction("选择Log目录");
    QAction *actOpenFile = fileMenu->addAction("选择Log文件");
    actOpenDir->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    actOpenFile->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
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
    connect(actOpenDir, &QAction::triggered, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(actOpenFile, &QAction::triggered, this, &PressAnalyzer::loadAndAnalyzeLog);
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

    connect(actUndo, &QAction::triggered, logView, &QPlainTextEdit::undo);
    connect(actRedo, &QAction::triggered, logView, &QPlainTextEdit::redo);
    connect(actCut,  &QAction::triggered, logView, &QPlainTextEdit::cut);
    connect(actCopy, &QAction::triggered, logView, &QPlainTextEdit::copy);
    connect(actPaste,&QAction::triggered, logView, &QPlainTextEdit::paste);
    connect(actSelectAll,&QAction::triggered, logView, &QPlainTextEdit::selectAll);

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
    QAction *actToggleCamera = viewMenu->addAction("切换 Camera状态 面板");
    QAction *actToggleStatus = viewMenu->addAction("切换 状态面板");
    QAction *actToggleEvent  = viewMenu->addAction("切换 分析结果 面板");
    QAction *actToggleSearchDock = viewMenu->addAction("切换 搜索结果 面板");
    viewMenu->addSeparator();
    QAction *actZoomIn = viewMenu->addAction("放大文本");
    QAction *actZoomOut = viewMenu->addAction("缩小文本");
    QAction *actZoomReset = viewMenu->addAction("重置文本大小");
    actZoomIn->setShortcut(QKeySequence::ZoomIn);
    actZoomOut->setShortcut(QKeySequence::ZoomOut);
    actZoomReset->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    connect(actToggleCamera, &QAction::triggered, this, [this](){
        cameraDock->setVisible(!cameraDock->isVisible());
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

    // 放大/缩小/重置文本大小
    QFont curFont = logView->font();
    const int basePointSize = curFont.pointSize() > 0 ? curFont.pointSize() : 12;
    logFontPointSize = basePointSize + 1; // 默认相当于放大一次
    auto applyLogFont = [this](int pt){
        QFont f = this->logView->font();
        f.setPointSize(pt);
        this->logView->setFont(f);
    };
    connect(actZoomIn, &QAction::triggered, this, [=](){ logFontPointSize += 1; applyLogFont(logFontPointSize); });
    connect(actZoomOut, &QAction::triggered, this, [=](){ logFontPointSize = std::max(8, logFontPointSize - 1); applyLogFont(logFontPointSize); });
    connect(actZoomReset, &QAction::triggered, this, [=](){ logFontPointSize = basePointSize; applyLogFont(logFontPointSize); });

    // 应用默认放大后的字号
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
                // Esc键取消提示
                if (searchEdit->completer()) {
                    searchEdit->completer()->popup()->hide();
                }
                return true;
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
    historyHints.prepend(text);
    
    // 限制历史数量，保持最近使用的50个
    if (historyHints.size() > 50) {
        historyHints.removeLast();
    }

    // 智能更新 completer：优先显示历史记录
    updateCompleterWithSmartHints();
}

// 智能更新completer提示
void PressAnalyzer::updateCompleterWithSmartHints()
{
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

// 显示搜索提示
void PressAnalyzer::showSearchHints()
{
    if (!searchEdit->completer()) return;
    
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

    // ==================== 预编译正则表达式 ====================
    static const QRegularExpression reSn(R"(\[I\|System\]: SN:\s*(\S+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rePressPower(R"(\[(\d+\.\d+)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*\])");
    static const QRegularExpression reTakeoff(R"(trigger source:\s*(\d+)\s+flight mode:\s*(\w+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTs(R"(\[\d+\.\d+\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reFcState(R"(Detected fc state changed to\s+(\d+))");
    static const QRegularExpression reInsertSql(R"(insertMediaDataIntoDb insert media sql.*\[(.*)\])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSqlValues(R"('([^']*)'|(\d+))");

    while (!in.atEnd()) {
        QString line = in.readLine();
        lineNumber++;
        allLogLines << line;

        textBuffer.reserve(textBuffer.size() + line.size() + 16);
        textBuffer.append(QString("%1 %2\n")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(line));

        // ==================== drone SN ====================
        auto matchSn = reSn.match(line);
        if (matchSn.hasMatch()) {
            sn = matchSn.captured(1).trimmed();
        }

        // ==================== press once power key ====================
        if (line.contains("press once power key", Qt::CaseInsensitive)) {
            triggerCount++;
            QString timestamp;
            auto m = rePressPower.match(line);
            if (m.hasMatch()) timestamp = m.captured(2);

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
            auto m = reTakeoff.match(line);
            if (m.hasMatch()) {
                int src = m.captured(1).toInt();
                modeText = m.captured(2);
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
            auto m = reTs.match(line);
            if (m.hasMatch())
                currentTakeoffTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");

            QString display = QString("%1 | %2# takeoff success,Flying")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(flightCount);
            addEventToList(triggerCount, lineNumber, display);
        }

        // ==================== FC STATE ====================
        auto mFc = reFcState.match(line);
        if (mFc.hasMatch()) {
            int stateValue = mFc.captured(1).toInt();
            QString stateName;
            switch (stateValue) {
            case 0: stateName = "DISARM";    break;
            case 1: stateName = "ARM";       break;
            case 2: stateName = "TAKINGOFF"; break;
            case 3: stateName = "FLYING";    break;
            case 4: stateName = "LANDING";   break;
            default: stateName = QString("UNKNOWN(%1)").arg(stateValue); break;
            }
            QString display = QString("%1 | %2# FC STATE -> %3")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(flightCount)
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
                                          .arg(flightCount)
                                          .arg(flightSeconds);
                    addEventToList(triggerCount, lineNumber, display);
                    currentTakeoffTime = QDateTime();
                }
            }
        }

        // ==================== insertMediaDataIntoDb ====================
        auto mSql = reInsertSql.match(line);
        if (mSql.hasMatch()) {
            QString bracketContent = mSql.captured(1);
            QStringList values;
            auto it = reSqlValues.globalMatch(bracketContent);
            while (it.hasNext()) {
                auto mm = it.next();
                if (mm.captured(1).size())
                    values << mm.captured(1);
                else
                    values << mm.captured(2);
            }

            if (values.size() >= 4) {
                QString uuid = values[0];
                int type = values[1].toInt();
                QString path = values[3];
                QString typeStr = typeToString(type);

                QString displayPath;
                if (path.startsWith("/media/internal/")) {
                    displayPath = "Internal:" + path.mid(16);
                } else if (path.startsWith("/media/external/")) {
                    displayPath = "External:" + path.mid(16);
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
                        QString display = QString("%1 | %2# %3")
                        .arg(lineNumber-1, 6, 10, QChar(' '))
                            .arg(flightCount)
                            .arg(l.trimmed());
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
            addEventToList(triggerCount, lineNumber, display);
            continue;
        }

        // ==================== 其他解析 ====================
        parseCameraStatus(lineNumber, line);
        parseStatusHeartbeat(lineNumber, line);
        parseStatusBattery(lineNumber, line);
        parseStatusSocTemp(lineNumber, line);
    }
}

// loadAndAnalyzeLog 保持之前逻辑
void PressAnalyzer::loadAndAnalyzeLog()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择日志文件", "", "日志文件 (*.txt *.log);;所有文件 (*)");
    if (filePath.isEmpty()) return;

    // 减少大文件解析时的界面重绘
    logView->setUpdatesEnabled(false);
    QSignalBlocker blocker1(eventList);
    QSignalBlocker blocker2(searchResultList);
    QSignalBlocker blocker3(cameraEventList);
    QSignalBlocker blocker4(statusEventList);

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
    soctmp.clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QString textBuffer;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    analyzeFile(filePath, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);

    logView->setPlainText(textBuffer);
    // 首次统一高亮一次，点击时不再重复全量高亮
    highlightAllEvents();
    titleLabel->setText(QString("心跳丢失次数:%1").arg(statusEventList->count()));
    batteryChart->setData(batteryinfo);
    socChart->clear();
    socChart->addData(soctmp);
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3").arg(sn).arg(triggerCount).arg(flightCount));

    //解析cpu/mem占用率，绘制图案
    QString topFilePath = getTopFilePath(filePath);
    parseTopFile(topFilePath);

    usageChart->setData(allusage);

    // 解析完成后恢复更新
    logView->setUpdatesEnabled(true);
}

void PressAnalyzer::loadAndAnalyzeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志文件或目录", "");
    if (path.isEmpty()) return;

    // 减少大文件解析时的界面重绘
    logView->setUpdatesEnabled(false);
    QSignalBlocker blocker1(eventList);
    QSignalBlocker blocker2(searchResultList);
    QSignalBlocker blocker3(cameraEventList);
    QSignalBlocker blocker4(statusEventList);

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
    soctmp.clear();
    triggerCount = 0;
    flightCount = 0;

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    QString textBuffer;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    // 分开解析 control_engine_log
    for (const QString &filePath : controlLogs) {
        analyzeFile(filePath, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
    }


    logView->setPlainText(textBuffer);
    // 首次统一高亮一次，点击时不再重复全量高亮
    highlightAllEvents();
    titleLabel->setText(QString("心跳丢失次数:%1").arg(statusEventList->count()));
    batteryChart->setData(batteryinfo);
    socChart->clear();
    socChart->addData(soctmp);
    setWindowTitle(QString("SN:%1 起飞次数: %2 | 成功起飞次数: %3").arg(sn).arg(triggerCount).arg(flightCount));
    // 分开解析 top_log
    for (const QString &filePath : topLogs) {
        parseTopFile(filePath);
    }
    usageChart->setData(allusage);

    // 解析完成后恢复更新
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
            color = colorPool[(i - 2) % colorPool.size()];

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
        QMessageBox::information(this, tr("搜索结果"), tr("匹配结果0,未搜索到内容。"));
        return;
    }

    searchResultList->show();
    currentSearchIndex = 0;
    jumpToSearchIndex(currentSearchIndex);
    
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
    QList<QTextEdit::ExtraSelection> selections;
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
            selections.push_back(lineSel);

            QString lineText = block.text();
            for (int k = 0; k < keys.size(); ++k) {
                QString pattern = QRegExp::escape(keys[k]);
                QRegExp rx(pattern, Qt::CaseInsensitive);
                int pos = 0;
                while ((pos = rx.indexIn(lineText, pos)) != -1) {
                    QTextEdit::ExtraSelection sel;
                    sel.cursor = QTextCursor(block);
                    sel.cursor.setPosition(block.position() + pos);
                    sel.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, rx.cap(0).length());
                    QTextCharFormat fmt;
                    fmt.setBackground(colors[k]);
                    fmt.setForeground(Qt::black);
                    sel.format = fmt;
                    selections.push_back(sel);
                    pos += rx.cap(0).length();
                }
            }
        }
    }

    logView->setExtraSelections(selections);

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
    int row = statusEventList->row(item);
    if (row < 0 || row >= statusEvents.size()) return;

    int lineNumber =  statusEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
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
