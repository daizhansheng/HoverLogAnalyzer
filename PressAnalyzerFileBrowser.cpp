// PressAnalyzerFileBrowser.cpp - File browser functions
#include "PressAnalyzer.h"
#include "MultiLanguageSyntaxHighlighter.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QMessageBox>
#include <QTextStream>
#include <QTextCursor>
#include <QApplication>
#include <QThread>
#include <QEventLoop>
#include <QDebug>
#include <QTimer>
#include <QPlainTextDocumentLayout>

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

void PressAnalyzer::openDirectoryInBrowser(const QString &dirPath)
{
    fileBrowserRootPath = dirPath;
    fileSystemModel->setRootPath(dirPath);
    fileBrowserTree->setRootIndex(fileSystemModel->index(dirPath));

    // 更新路径标签：传入完整路径，由 ElidedPathLabel 根据宽度自动省略
    ElidedPathLabel *pathLabel = m_fileBrowserContainer->findChild<ElidedPathLabel*>("fileBrowserPathLabel");
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

    // 如果侧边栏文件浏览器面板没有显示则显示
    if (!(m_sideBar->isPanelVisible() && m_sideBar->currentPanelIndex() == 0)) {
        m_sideBar->showPanel(0);
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

    // 按扩展名切换语法高亮模式（.json/.xml/.cpp/.py/.sh/.yaml/.ini ... 自适应配色）
    if (m_codeHighlighter) {
        m_codeHighlighter->setMode(MultiLanguageSyntaxHighlighter::modeForFile(filePath));
    }

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(filePath));

    // 记录 sourcePath 并立即更新标签名（loadFileToLogView 不走 onParseFinished）
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size()) {
        m_tabStates[m_currentTabIndex].sourcePath = filePath;
        if (m_tabBar)
            m_tabBar->setTabText(m_currentTabIndex, QFileInfo(filePath).fileName());
    }

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    if (m_sideBar->currentPanelIndex() == 1) m_sideBar->hidePanel();  // 隐藏分析结果面板
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
    m_colorMarkHitData.clear();

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
    ++m_parseGeneration;
    startBackgroundParse(m_parseWorker, m_parseThread, m_parseGeneration);
}
