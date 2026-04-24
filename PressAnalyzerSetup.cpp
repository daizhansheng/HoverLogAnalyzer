// PressAnalyzerSetup.cpp - UI initialization and setup functions
#include "PressAnalyzer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenuBar>
#include <QAction>
#include <QMenu>
#include <QShortcut>
#include <QKeyEvent>
#include <QTimer>
#include <QCheckBox>
#include <QCompleter>
#include <QStringListModel>
#include <QInputDialog>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QListView>
#include <QFontMetrics>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QClipboard>
#include <QHeaderView>
#include <QScrollArea>
#include <QScrollBar>
#include <QProgressBar>
#include <QFontDialog>
#include <QWindow>
#include "LogNumberHighlighter.h"

void PressAnalyzer::setupMainWindow()
{
    setWindowTitle("Hover日志分析助手");
    setWindowIcon(QIcon(":/new/image/logo.icns"));
    resize(1200, 700);

    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::South);

    // 支持拖放文件直接打开
    setAcceptDrops(true);

    // 撤销自定义居中标题栏
}
// 撤销自定义更新接口，保持系统默认标题行为

void PressAnalyzer::setupCentralWidget()
{
    // ---- VS Code 风格侧边栏 ----
    m_sideBar = new VSCodeSideBar(this);

    // ---- 外层容器：TabBar（上）+ centralStack（下）----
    m_tabContainer = new QWidget(this);
    QVBoxLayout *tabContainerLayout = new QVBoxLayout(m_tabContainer);
    tabContainerLayout->setContentsMargins(0, 0, 0, 0);
    tabContainerLayout->setSpacing(0);

    m_tabBar = new ColoredTabBar(m_tabContainer);
    // 强制 Fusion 风格，绕过 macOS 原生样式对 tab 宽度的限制
    m_tabBar->setStyle(QStyleFactory::create("Fusion"));
    m_tabBar->setTabsClosable(false);
    m_tabBar->setMovable(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    // 不在 stylesheet 中设置 font-size / font-weight，
    // 否则 QStyleSheetStyle 会接管 CE_TabBarTabLabel 绘制并忽略 elideMode()。
    // 字体通过 setFont() 设置，bold 选中效果由 paintEvent 处理。
    m_tabBar->setStyleSheet(
        "QTabBar::tab {"
        "  padding: 4px 20px 4px 10px;"
        "}"
    );
    // 在 setStyleSheet 之后锁定 ElideNone，防止被 style change 重置
    QFont tabFont = m_tabBar->font();
    // Tab 字体大小平台适配：macOS=12pt，Windows=10pt
    tabFont.setPointSize(platformFontSize(12, 10));
    m_tabBar->setFont(tabFont);
    m_tabBar->setElideMode(Qt::ElideNone);

    // 左对齐：tabBar 放入 HBoxLayout，右侧加 stretch
    QHBoxLayout *tabBarRow = new QHBoxLayout();
    tabBarRow->setContentsMargins(0, 0, 0, 0);
    tabBarRow->setSpacing(0);
    tabBarRow->addWidget(m_tabBar);

    // "+" 新建标签按钮
    m_newTabButton = new QPushButton("+", m_tabContainer);
    m_newTabButton->setFixedSize(28, 28);
    m_newTabButton->setFlat(true);
    m_newTabButton->setCursor(Qt::ArrowCursor);
    m_newTabButton->setFocusPolicy(Qt::NoFocus);
    m_newTabButton->setToolTip("新建标签");
    m_newTabButton->setStyleSheet(
        "QPushButton {"
        "  border-radius: 5px;"
        "  background-color: transparent;"
        "  border: none;"
        "  color: #666666;"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  padding: 0px;"
        "  margin-left: 4px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(0,0,0,0.10);"
        "  color: #222222;"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(0,0,0,0.18);"
        "}"
    );
    connect(m_newTabButton, &QPushButton::clicked, this, &PressAnalyzer::openNewEmptyTab);
    tabBarRow->addWidget(m_newTabButton);

    tabBarRow->addStretch(1);

    centralStack = new QStackedWidget(m_tabContainer);
    tabContainerLayout->addLayout(tabBarRow);
    tabContainerLayout->addWidget(centralStack);

    // ---- 将侧边栏 + 主内容区包装到 QHBoxLayout ----
    QWidget *centralContainer = new QWidget(this);
    QHBoxLayout *centralHLayout = new QHBoxLayout(centralContainer);
    centralHLayout->setContentsMargins(0, 0, 0, 0);
    centralHLayout->setSpacing(0);
    centralHLayout->addWidget(m_sideBar);
    centralHLayout->addWidget(m_tabContainer, 1);
    setCentralWidget(centralContainer);

    // 初始化第一个标签（带字体状态初始化）
    m_tabBar->addTab("新标签");
    {
        TabState initState;
        initState.logFont       = currentLogFont;
        initState.logFontPtSize = logFontPointSize;
        m_tabStates.append(initState);
    }
    m_currentTabIndex = 0;
    m_tabBar->setTabButton(0, QTabBar::RightSide, makeTabCloseButton(0));
    m_tabBar->setTabColor(0, tabColorForIndex(0));

    // Tab 切换信号
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (index == m_currentTabIndex) return;
        if (index < 0 || index >= m_tabStates.size()) return;
        saveCurrentTabState();
        restoreTabState(index);
        m_currentTabIndex = index;
    });
    // Tab 关闭信号
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
        closeTab(index);
    });
    // Tab 右键菜单
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        int idx = m_tabBar->tabAt(pos);
        if (idx < 0) return;
        QMenu menu;
        QAction *actDetach = menu.addAction("移到新窗口");
        if (menu.exec(m_tabBar->mapToGlobal(pos)) == actDetach)
            detachTabToNewWindow(idx);
    });

    // Page 0: 日志视图
    logView = new LogView(this);
    centralStack->addWidget(logView);  // index 0

    // 禁用 logView 自带的拖放（URL 会被直接插入文本），由主窗口统一处理
    logView->setAcceptDrops(false);
    logView->installEventFilter(this);

    // 行号已由 LogView 的独立行号栏绘制；正文中不再嵌入数字前缀，
    // 这里 skip=0 让高亮器照常给日志正文中的数字着色。
    new LogNumberHighlighter(logView->document(), 0);

    // 使用 currentLogFont（在构造函数中已初始化），避免与硬编码字体不一致
    logView->setFont(currentLogFont);
    logView->document()->setDefaultFont(currentLogFont);
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
    eventList->setStyleSheet(
        "QListWidget {"
        "  border: none;"
        "  background-color: #FAFAFA;"
        "}"
        "QListWidget::item {"
        "  padding: 2px 4px;"
        "}"
        "QListWidget::item:hover {"
        "  background-color: #E8F0FE;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #D2E3FC;"
        "  color: #1A73E8;"
        "}"
    );
    // 添加到侧边栏 panel 1 = 分析结果
    m_sideBar->addPanel(SideBarIcons::analysisList(), "分析结果", eventList, "分析结果");
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
    // 文件浏览器字体平台适配：macOS=12px/11px，Windows 用 pt 单位以适应系统 DPI
    fileBrowserTree->setStyleSheet(
        QString(
        "QTreeView {"
        "  border: none;"
        "  background-color: #FAFAFA;"
#if defined(Q_OS_WIN)
        "  font-size: 9pt;"
#else
        "  font-size: 12px;"
#endif
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
#if defined(Q_OS_WIN)
        "  font-size: 8pt;"
#else
        "  font-size: 11px;"
#endif
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
        )
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
    goUpButton->setFixedSize(platformPx(28), platformPx(28));
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
#if defined(Q_OS_WIN)
        "  font-size: 8pt;"
#else
        "  font-size: 11px;"
#endif
        "  padding: 2px 4px;"
        "  background-color: #F8F8F8;"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 3px;"
        "}"
    );

    QPushButton *chooseDirButton = new QPushButton(this);
    chooseDirButton->setIcon(QIcon(":/icons/icons/analytics_ce.png"));
    chooseDirButton->setToolTip("智能解析Control Engine日志");
    chooseDirButton->setFixedSize(platformPx(28), platformPx(28));
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

    // 添加到侧边栏 panel 0 = 文件浏览器
    m_fileBrowserContainer = browserContainer;
    m_sideBar->addPanel(SideBarIcons::fileBrowser(), "文件浏览器", browserContainer, "文件浏览器");

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
            QAction *actDelete        = menu.addAction("删除目录");
            QAction *actRename        = menu.addAction("重命名目录");
            QAction *actOpen          = menu.addAction("打开目录");
            QAction *actLoadDir       = menu.addAction("智能解析日志");
            QAction *actLoadDirNewTab = menu.addAction("在新标签页中智能解析");
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
            } else if (chosen == actLoadDirNewTab) {
                openInNewTab(filePath);
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

void PressAnalyzer::setupToolBar()
{
    QToolBar *toolBar = addToolBar("主工具栏");

    // 创建图标按钮
    analyzeControlButton = new QPushButton(this);
    analyzeControlButton->setIcon(QIcon(":/icons/icons/analysis.png"));
    analyzeControlButton->setToolTip("分析Control Engine日志(专用)");
    analyzeControlButton->setIconSize(platformSize(20, 20));

    clearButton = new QPushButton(this);
    clearButton->setIcon(QIcon(":/icons/icons/clear.png"));
    clearButton->setToolTip("清除窗口");
    clearButton->setIconSize(platformSize(20, 20));

    newWindowButton = new QPushButton(this);
    QIcon windowIcon(":/icons/icons/window-new.png");
    if (windowIcon.isNull() || windowIcon.pixmap(20, 20).isNull()) {
        newWindowButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    } else {
        newWindowButton->setIcon(windowIcon);
    }
    newWindowButton->setToolTip("新建窗口");
    newWindowButton->setIconSize(platformSize(20, 20));

    searchAllButton = new QPushButton(this);
    searchAllButton->setIcon(QIcon(":/icons/icons/search.png"));
    searchAllButton->setToolTip("搜索");
    searchAllButton->setIconSize(platformSize(20, 20));

    searchPrevButton = new QPushButton(this);
    searchPrevButton->setIcon(QIcon(":/icons/icons/arrow-up.png"));
    searchPrevButton->setToolTip("向前搜索");
    searchPrevButton->setIconSize(platformSize(20, 20));

    searchNextButton = new QPushButton(this);
    searchNextButton->setIcon(QIcon(":/icons/icons/arrow-down.png"));
    searchNextButton->setToolTip("向后搜索");
    searchNextButton->setIconSize(platformSize(20, 20));

    // 创建搜索下拉（可编辑），不点下拉也可直接输入
    searchCombo = new SearchComboBox(this);
    searchCombo->setEditable(true);
    searchCombo->setInsertPolicy(QComboBox::NoInsert);
    searchCombo->setMaxVisibleItems(15);  // 限制下拉最多显示15条，避免列表过长
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
        // macOS: Menlo, Windows: Consolas, Linux: DejaVu Sans Mono
        QFont seFont = platformMonoFont(11);
        searchEdit->setFont(seFont);
        // 同步设置给 QComboBox 本体，确保高度与布局计算一致
        searchCombo->setFont(seFont);
    }

    // 文件浏览器按钮
    fileBrowserButton = new QPushButton(this);
    fileBrowserButton->setIcon(QIcon(":/icons/icons/folder.png"));
    fileBrowserButton->setToolTip("文件浏览器");
    fileBrowserButton->setIconSize(platformSize(20, 20));

    // 动态图表搜索按钮
    chartSearchButton = new QPushButton(this);
    chartSearchButton->setIcon(QIcon(":/icons/icons/motion-graphics.png"));
    chartSearchButton->setToolTip("动态图表搜索");
    chartSearchButton->setIconSize(platformSize(20, 20));

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
    statusButton->setIconSize(platformSize(20, 20));
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
        const int btnMin = platformPx(28);
        button->setFlat(false);
        button->setStyleSheet(
            QString(
                "QPushButton{"
                "  background-color:%1;"
                "  border:1px solid #CCCCCC;"
                "  border-radius:4px;"
                "  padding:4px;"
                "  min-width:%4px;"
                "  min-height:%4px;"
                "}"
                "QPushButton:hover{"
                "  background-color:%2;"
                "}"
                "QPushButton:pressed{"
                "  background-color:%3;"
                "}"
            ).arg(bg, hover, pressed, QString::number(btnMin))
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
    QAction *actSave = fileMenu->addAction("保存文件");
    actSave->setShortcut(QKeySequence::Save);
    QAction *actClear = fileMenu->addAction("清除窗口");
    QAction *actClose = fileMenu->addAction("关闭窗口");
    actClose->setShortcut(QKeySequence::Close);

    connect(actNewWindow, &QAction::triggered, this, [=](){
        auto *w = new PressAnalyzer(nullptr);
        w->setAttribute(Qt::WA_DeleteOnClose, true);
        w->show();
        // 字体在 setupMenuBar 末尾的 s_sharedLogFont 检查中自动应用
    });

    connect(actOpenDir, &QAction::triggered, this, &PressAnalyzer::loadAndMergeLogs);
    connect(actAnalyzeControlLog, &QAction::triggered, this, &PressAnalyzer::loadAndAnalyzeLogs);
    connect(actSave, &QAction::triggered, this, &PressAnalyzer::saveCurrentFile);
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
        if (m_sideBar->isPanelVisible()) {
            m_sideBar->hidePanel();
        } else {
            if (fileBrowserRootPath.isEmpty()) {
                QString dir = QFileDialog::getExistingDirectory(this, "选择浏览目录", QDir::homePath());
                if (!dir.isEmpty()) {
                    openDirectoryInBrowser(dir);
                }
            } else {
                m_sideBar->showPanel(0); // 默认显示文件浏览器
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
        m_sideBar->togglePanel(1);
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
    // 如果已有跨窗口共享字体，优先使用，确保所有窗口字体一致
    if (s_sharedLogFontSet) {
        currentLogFont = s_sharedLogFont;
    }
    // basePointSize 从 currentLogFont 获取（已含 settings 或共享字体的字号），避免硬编码
    const int basePointSize = (currentLogFont.pointSize() > 0) ? currentLogFont.pointSize() : 11;
    logFontPointSize = basePointSize;
    auto applyLogFont = [this](int pt){
        QFont f = this->currentLogFont;   // 以当前字体（族、样式）为基础
        f.setPointSize(pt);
        this->currentLogFont = f;         // 保持 currentLogFont 与实际显示同步
        this->logView->setFont(f);
        this->logView->document()->setDefaultFont(f);  // 显式同步文档默认字体
        if (this->searchResultView) {
            this->searchResultView->setFont(f);
        }
        if (this->searchCombo) {
            this->searchCombo->setFont(f);
        }
        // 同步共享字体
        s_sharedLogFont = f;
        s_sharedLogFontSet = true;
    };
    connect(actZoomIn, &QAction::triggered, this, [=](){ logFontPointSize += 1; applyLogFont(logFontPointSize); });
    connect(actZoomOut, &QAction::triggered, this, [=](){ logFontPointSize = std::max(8, logFontPointSize - 1); applyLogFont(logFontPointSize); });
    connect(actZoomReset, &QAction::triggered, this, [=](){ logFontPointSize = basePointSize; applyLogFont(logFontPointSize); });

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

    // ---- 独立浮动窗口（替代 QDockWidget）----
    // 以主窗口 this 作为逻辑父窗口（配合 Qt::Window 仍是独立顶层窗口），
    // 这样 Qt 能把浮动窗口的屏幕归属绑定到主窗口所在的屏幕，
    // 避免首次 show 时被 OS 默认放到主屏。
    statusFloatWin = new QWidget(this,
        Qt::Window | Qt::Tool | Qt::WindowTitleHint |
        Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    statusFloatWin->setWindowTitle("状态面板");
    statusFloatWin->setAttribute(Qt::WA_DeleteOnClose, false);  // 关闭时隐藏，不销毁
    statusFloatWin->resize(900, 700);

    // 用 QScrollArea 包裹 mainSplitter，使内容可滚动
    QScrollArea *statusScroll = new QScrollArea(statusFloatWin);
    statusScroll->setWidgetResizable(true);
    statusScroll->setWidget(mainSplitter);
    statusScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    statusScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QVBoxLayout *floatLayout = new QVBoxLayout(statusFloatWin);
    floatLayout->setContentsMargins(0, 0, 0, 0);
    floatLayout->addWidget(statusScroll);

    // 让关闭按钮只隐藏窗口
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), statusFloatWin),
            &QShortcut::activated, statusFloatWin, &QWidget::hide);

    statusDock = statusFloatWin;  // 保持 statusDock 指针别名兼容旧代码
}

void PressAnalyzer::setupConnections()
{
    // 编辑内容变化时标记当前标签为已修改
    // 使用 QTextDocument::modificationChanged(bool) 而非 textChanged：
    // 该信号只在“脏/干净”状态真正翻转时发出一次，不会被加载期间的
    // 格式高亮（highlightAllEvents 等）误触发。加载流程末尾调用
    // logView->document()->setModified(false) 即可把状态归零。
    connect(logView, &QPlainTextEdit::modificationChanged, this, [this](bool mod){
        if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
            markTabModified(mod);
    });

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
            m_sideBar->showPanel(0);
            return;
        }

        if (!fileBrowserRootPath.isEmpty()) {
            m_sideBar->showPanel(0);
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

    // 状态面板连接：切换浮动窗口显隐
    // 辅助 lambda：将浮动窗口定位到主窗口所在屏幕，并确保不超出边界
    auto showFloatWin = [this](QWidget *win, bool preferRight) {
        QRect ref = geometry();
        QScreen *scr = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
        QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);

        int wx, wy;
        if (preferRight) {
            wx = ref.right() + 10;
            wy = ref.top();
            if (wx + win->width() > avail.right())
                wx = ref.center().x() - win->width() / 2;
        } else {
            wx = ref.left() + 60;
            wy = ref.top() + 40;
        }
        wx = qBound(avail.left(), wx, avail.right()  - win->width());
        wy = qBound(avail.top(),  wy, avail.bottom() - win->height());

        // 关键：先创建 native handle 并把窗口显式绑定到目标屏幕，
        // 否则首次 show 时 OS 可能把窗口放到主屏，move() 之后才纠正，
        // 视觉上就成了“总在主屏弹出再跳过来”。
        win->createWinId();
        if (scr && win->windowHandle())
            win->windowHandle()->setScreen(scr);
        // 用 setGeometry 一次性给定位置+尺寸，比 move() 更可靠
        win->setGeometry(wx, wy, win->width(), win->height());
        win->show();
        win->raise();
        win->activateWindow();
    };

    connect(statusButton, &QPushButton::clicked, this, [this, showFloatWin](){
        if (statusFloatWin->isVisible())
            statusFloatWin->hide();
        else
            showFloatWin(statusFloatWin, true);
    });

    // 动态图表搜索按钮 — 切换动态图表浮动窗口
    connect(chartSearchButton, &QPushButton::clicked, this, [this, showFloatWin](){
        if (m_chartFloatWin->isVisible())
            m_chartFloatWin->hide();
        else
            showFloatWin(m_chartFloatWin, false);
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

    // SideBar 外部切换按钮：先添加状态面板（上），再添加动态图表（下）
    m_statusToggleIndex = m_sideBar->addExternalToggle(SideBarIcons::statusChart(), "状态面板");
    m_chartPanelIndex   = m_sideBar->addExternalToggle(SideBarIcons::dynamicChart(), "动态图表");
    connect(m_sideBar, &VSCodeSideBar::externalToggleClicked, this, [this, showFloatWin](int idx) {
        if (idx == m_statusToggleIndex) {
            if (statusFloatWin->isVisible())
                statusFloatWin->hide();
            else
                showFloatWin(statusFloatWin, true);
        } else if (idx == m_chartPanelIndex) {
            if (m_chartFloatWin->isVisible())
                m_chartFloatWin->hide();
            else
                showFloatWin(m_chartFloatWin, false);
        }
    });
    // 同步浮动窗口可见性到 SideBar 按钮激活状态
    statusFloatWin->installEventFilter(this);
    m_chartFloatWin->installEventFilter(this);
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
        // 按钮最小尺寸平台适配：Windows 放大以匹配视觉效果
        const int btnMin = platformPx(28);
        button->setFlat(false);
        button->setStyleSheet(
            QString(
                "QPushButton{"
                "  background-color:%1;"
                "  border:1px solid #CCCCCC;"
                "  border-radius:4px;"
                "  padding:4px;"
                "  min-width:%4px;"
                "  min-height:%4px;"
                "}"
                "QPushButton:hover{"
                "  background-color:%2;"
                "}"
                "QPushButton:pressed{"
                "  background-color:%3;"
                "}"
            ).arg(bg, hover, pressed, QString::number(btnMin))
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
