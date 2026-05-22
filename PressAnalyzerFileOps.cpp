// PressAnalyzerFileOps.cpp - File loading and operations
#include "PressAnalyzer.h"
#include "ParseProgressDialog.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QMessageBox>
#include <QTextCursor>
#include <QTextBlock>
#include <QProcess>
#include <QApplication>
#include <QThread>
#include <QEventLoop>
#include <QProgressBar>
#include <QScrollBar>
#include <QRegularExpression>
#include <QDirIterator>
#include <QDebug>
#include <QPlainTextDocumentLayout>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include "HLogBinaryParser.h"

void PressAnalyzer::loadAndAnalyzeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志文件或目录", "");
    if (path.isEmpty()) return;

    // 在"分析Control Engine日志"场景下：仅在当前目录的下一级（直接子目录）查找 system_log
    {
        auto findChildSystemLog = [](const QString &base) -> QString {
            QDir dir(base);
            if (dir.exists("system_log")) return dir.absoluteFilePath("system_log");
            QFileInfoList level1 = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d1 : level1) {
                QDir dir1(d1.absoluteFilePath());
                if (dir1.exists("system_log")) return dir1.absoluteFilePath("system_log");
            }
            return QString();
        };

        auto parseUserLogHeaderCE = [this](const QString &userLogPath){
            QFile file(userLogPath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QTextStream in(&file);
            in.setCodec("UTF-8");
            QString imageVer, ipkVer, sn, hwid;
            QString prevLine;
            int linesRead = 0;
            while (!in.atEnd() && linesRead < 400) {
                QString line = in.readLine();
                ++linesRead;
                QString l = line.trimmed();
                if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1); else imageVer = l;
                }
                if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
                }
                if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                    if (!l.isEmpty()) hwid = l;
                }
                if (imageVer.isEmpty()) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1);
                }
                if (ipkVer.isEmpty()) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1);
                }
                if (sn.isEmpty()) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                prevLine = l;
                if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
            }
            this->sn = sn;
            QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                               .arg(imageVer.isEmpty() ? "-" : imageVer)
                               .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                               .arg(sn.isEmpty() ? "-" : sn)
                               .arg(hwid.isEmpty() ? "-" : hwid);
            version = imageVer.mid(0,4);
            if (statusInfoLabel) statusInfoLabel->setText(info);
        };

        QString syslogDir = findChildSystemLog(path);
        if (!syslogDir.isEmpty()) {
            QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
            QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
            if (QFileInfo::exists(userLog)) {
                parseUserLogHeaderCE(userLog);
            } else if (QFileInfo::exists(userLogZip)) {
                if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                    parseUserLogHeaderCE(userLog);
                }
            }
        }
    }

    // 如果用户直接选中了 control_engine_log 目录本身，自动上移到父目录
    {
        QFileInfo selInfo(path);
        if (selInfo.isDir() && selInfo.fileName() == "control_engine_log") {
            path = selInfo.absolutePath();
        }
    }

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));
    QFileInfo info(path);

    QStringList controlLogs, topLogs;

    if (info.isDir()) {
        controlLogs = collectOrderedLogFiles(path, "control_engine_log", "control_engine", {"log", "hlog"});
        topLogs = collectOrderedLogFiles(path, "system_log/top_log", "top", {"log"});

        if (controlLogs.isEmpty() && topLogs.isEmpty()) {
            QMessageBox::warning(this, "错误", "日志文件不存在");
            return;
        }
    } else if (info.isFile()) {
        controlLogs << info.filePath();
    }

    // ---------------- 清空之前的数据 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
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

    // ---- 启动后台解析线程 ----
    m_pendingTopLogs = topLogs;

    m_parseWorker = new LogParserWorker();
    m_parseWorker->setFiles(controlLogs);
    m_parseWorker->setVersion(version);

    m_parseThread = new QThread();
    ++m_parseGeneration;
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].isControlEngine = true;
    startBackgroundParse(m_parseWorker, m_parseThread, m_parseGeneration);
}

void PressAnalyzer::loadAndAnalyzeLogsFromPath(const QString &path)
{
    // 切换到日志视图页
    centralStack->setCurrentIndex(0);

    // 更新 sourcePath 供 onParseFinished 命名标签（CE 路径将以 CE:<SN> 命名）
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].sourcePath = path;

    // 在"分析Control Engine日志"场景下：仅在当前目录的下一级（直接子目录）查找 system_log
    {
        auto findChildSystemLog = [](const QString &base) -> QString {
            QDir dir(base);
            // 当前目录本身
            if (dir.exists("system_log")) return dir.absoluteFilePath("system_log");
            // 直接子目录
            QFileInfoList level1 = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &d1 : level1) {
                QDir dir1(d1.absoluteFilePath());
                if (dir1.exists("system_log")) return dir1.absoluteFilePath("system_log");
            }
            return QString();
        };

        auto parseUserLogHeaderCE = [this](const QString &userLogPath){
            QFile file(userLogPath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QTextStream in(&file);
            in.setCodec("UTF-8");
            QString imageVer, ipkVer, sn, hwid;
            QString prevLine;
            int linesRead = 0;
            while (!in.atEnd() && linesRead < 400) {
                QString line = in.readLine();
                ++linesRead;
                QString l = line.trimmed();
                if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1); else imageVer = l;
                }
                if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
                }
                if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                    if (!l.isEmpty()) hwid = l;
                }
                if (imageVer.isEmpty()) {
                    QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reZZ.match(l);
                    if (m.hasMatch()) imageVer = m.captured(1);
                }
                if (ipkVer.isEmpty()) {
                    QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                             QRegularExpression::CaseInsensitiveOption);
                    auto m = reVer.match(l);
                    if (m.hasMatch()) ipkVer = m.captured(1);
                }
                if (sn.isEmpty()) {
                    QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                            QRegularExpression::CaseInsensitiveOption);
                    auto m = reSn.match(l);
                    if (m.hasMatch()) sn = m.captured(1);
                }
                prevLine = l;
                if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
            }
            // 将解析到的 SN 写回成员变量，供窗口标题等使用
            this->sn = sn;

            QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                               .arg(imageVer.isEmpty() ? "-" : imageVer)
                               .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                               .arg(sn.isEmpty() ? "-" : sn)
                               .arg(hwid.isEmpty() ? "-" : hwid);
            version = imageVer.mid(0,4);
            if (statusInfoLabel) statusInfoLabel->setText(info);
        };

        QString syslogDir = findChildSystemLog(path);
        if (!syslogDir.isEmpty()) {
            QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
            QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
            if (QFileInfo::exists(userLog)) {
                parseUserLogHeaderCE(userLog);
            } else if (QFileInfo::exists(userLogZip)) {
                if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                    parseUserLogHeaderCE(userLog);
                }
            }
        }
    }

    // 减少大文件解析时的界面重绘
    // (blockers removed: parsing is now done in background thread)

    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));
    QFileInfo info(path);

    QStringList controlLogs, topLogs;

    if (info.isDir()) {
        controlLogs = collectOrderedLogFiles(path, "control_engine_log", "control_engine", {"log", "hlog"});
        topLogs = collectOrderedLogFiles(path, "system_log/top_log", "top", {"log"});

        if (controlLogs.isEmpty() && topLogs.isEmpty()) {
            QMessageBox::warning(this, "错误", "日志文件不存在");
            return;
        }
    } else if (info.isFile()) {
        controlLogs << info.filePath();
    }

    // ---------------- 清空之前的数据 ----------------
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
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

    // ---- 启动后台解析线程 ----
    m_pendingTopLogs = topLogs;

    m_parseWorker = new LogParserWorker();
    m_parseWorker->setFiles(controlLogs);
    m_parseWorker->setVersion(version);

    m_parseThread = new QThread();
    ++m_parseGeneration;
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].isControlEngine = true;
    startBackgroundParse(m_parseWorker, m_parseThread, m_parseGeneration);
}

QString PressAnalyzer::findControlEngineAnalysisRoot(const QString &basePath) const
{
    QFileInfo baseInfo(basePath);
    if (!baseInfo.exists()) {
        return QString();
    }

    // 单个文件不做 control_engine_log 搜索，直接返回空
    if (baseInfo.isFile()) {
        return QString();
    }

    if (baseInfo.isDir() && baseInfo.fileName() == "control_engine_log") {
        return baseInfo.absolutePath();
    }

    const QDir rootDir(baseInfo.absoluteFilePath());
    QDirIterator it(rootDir.absolutePath(),
                    QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dirPath = it.next();
        if (QFileInfo(dirPath).fileName() == "control_engine_log") {
            return QFileInfo(dirPath).absolutePath();
        }
    }

    return QString();
}

void PressAnalyzer::loadAndMergeLogs()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择日志目录");
    if (path.isEmpty()) {
        return;
    }
    loadMergeLogsFromPath(path);
}

void PressAnalyzer::loadMergeLogsFromPath(const QString &path, bool navigate)
{
    // 执行智能分析前先清除"当前 tab"内容，避免上次分析结果的干扰
    // 注意：必须只清当前 tab，而非 clearWindow() 关全部，否则会误关其它 tab
    clearCurrentTabContent();

    // 显示用户选择的通用目录路径（后续过程保持静默，不覆盖）
    if (statusPathLabel) statusPathLabel->setText(QString("%1").arg(path));

    // 更新 sourcePath 供 onParseFinished 命名标签
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        m_tabStates[m_currentTabIndex].sourcePath = path;

    // 根据参数决定是否在文件浏览器中打开此目录
    if (navigate) {
        openDirectoryInBrowser(path);
    }

    // 先在所选目录向下（最多两级）寻找 system_log 目录，并尝试解析 user.log 头部信息
    auto findSystemLogDir = [](const QString &base) -> QString {
        QDir cur(base);
        // 情况A：当前目录本身或其上级链中存在 system_log，则返回该 system_log
        QDir climb = cur;
        while (true) {
            if (climb.dirName() == QStringLiteral("system_log")) {
                return climb.absolutePath();
            }
            QDir up(climb);
            if (!up.cdUp()) break;
            climb = up;
        }

        // 情况B：在当前目录的同级目录中查找 system_log
        QDir parent(cur.absolutePath());
        if (parent.cdUp()) {
            if (parent.exists("system_log")) {
                return parent.absoluteFilePath("system_log");
            }
        }

        return QString();
    };

    auto parseUserLogHeader = [this](const QString &userLogPath){
        QFile file(userLogPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
        QTextStream in(&file);
        in.setCodec("UTF-8");

        QString imageVer, ipkVer, sn, hwid;
        QString prevLine;
        int linesRead = 0;
        while (!in.atEnd() && linesRead < 400) {
            QString line = in.readLine();
            ++linesRead;
            QString l = line.trimmed();
            // 通过标记行提取下一行内容
            if (prevLine.contains("image verison", Qt::CaseInsensitive)) {
                // 目标：提取 H141B_V8.0.17
                QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reZZ.match(l);
                if (m.hasMatch()) imageVer = m.captured(1);
                if (imageVer.isEmpty()) imageVer = l;
            }
            if (prevLine.contains("ipk version", Qt::CaseInsensitive)) {
                QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                         QRegularExpression::CaseInsensitiveOption);
                auto m = reVer.match(l);
                if (m.hasMatch()) ipkVer = m.captured(1); else ipkVer = l;
            }
            if (prevLine.contains("hover.sn", Qt::CaseInsensitive)) {
                QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reSn.match(l);
                if (m.hasMatch()) sn = m.captured(1);
            }
            if (prevLine.contains("hardware id", Qt::CaseInsensitive)) {
                if (!l.isEmpty()) hwid = l;
            }

            // 也支持不依赖标记的直匹配
            if (imageVer.isEmpty()) {
                QRegularExpression reZZ(R"(zz\.product\.version=\s*ZZ_IMG_([A-Za-z0-9_\.]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reZZ.match(l);
                if (m.hasMatch()) imageVer = m.captured(1);
            }
            if (ipkVer.isEmpty()) {
                QRegularExpression reVer(R"(Version:\s*([0-9][0-9\.]*))",
                                         QRegularExpression::CaseInsensitiveOption);
                auto m = reVer.match(l);
                if (m.hasMatch()) ipkVer = m.captured(1);
            }
            if (sn.isEmpty()) {
                QRegularExpression reSn(R"(hover\s*=\s*([A-Za-z0-9]+))",
                                        QRegularExpression::CaseInsensitiveOption);
                auto m = reSn.match(l);
                if (m.hasMatch()) sn = m.captured(1);
            }

            prevLine = l;
            if (!imageVer.isEmpty() && !ipkVer.isEmpty() && !sn.isEmpty() && !hwid.isEmpty()) break;
        }

        QString info = QString("Image:%1 | IPK:%2 | SN:%3 | HW:%4")
                           .arg(imageVer.isEmpty() ? "-" : imageVer)
                           .arg(ipkVer.isEmpty() ? "-" : ipkVer)
                           .arg(sn.isEmpty() ? "-" : sn)
                           .arg(hwid.isEmpty() ? "-" : hwid);
        version = imageVer.mid(0,4);
        // 将持久信息放到右侧永久区域；左侧保持路径不变
        if (statusInfoLabel) statusInfoLabel->setText(info);
    };

    QString syslogDir = findSystemLogDir(path);
    if (!syslogDir.isEmpty()) {
        QString userLogZip = QDir(syslogDir).absoluteFilePath("user.log.zip");
        QString userLog = QDir(syslogDir).absoluteFilePath("user.log");
        if (QFileInfo::exists(userLog)) {
            // 已解压，直接解析
            parseUserLogHeader(userLog);
        } else if (QFileInfo::exists(userLogZip)) {
            // 解压 user.log.zip 到同目录（若已解压则 unzip 会覆盖或直接成功返回）
            if (extractZipFile(userLogZip, syslogDir) && waitForFile(userLog)) {
                parseUserLogHeader(userLog);
            }
        }
    }

    // 如果当前目录本身是 control_engine_log，或其下包含 control_engine_log 子目录，
    // 都走专用的 control_engine 分析逻辑。
    QFileInfo pathInfo(path);
    if (pathInfo.fileName() == "control_engine_log") {
        QString parentPath = pathInfo.absolutePath();
        loadAndAnalyzeLogsFromPath(parentPath);
        return;
    }
    if (pathInfo.isDir() && QDir(path).exists("control_engine_log")) {
        loadAndAnalyzeLogsFromPath(path);
        return;
    }

    // 如果不是control_engine_log目录，隐藏分析结果面板
    if (m_sideBar->currentPanelIndex() == 1) m_sideBar->hidePanel();

    // 禁用界面更新，提高性能
    logView->setUpdatesEnabled(false);
    QSignalBlocker blocker1(eventList);
    QSignalBlocker blocker2(searchResultView);
    QSignalBlocker blocker3(cameraEventList);
    QSignalBlocker blocker4(heartbeatLostEventList);

    // 清空之前的数据
    allLogLines.clear();
    allEvents.clear();
    cameraEvents.clear();
    eventList->clear();
    if (m_sideBar->currentPanelIndex() == 1) m_sideBar->hidePanel();  // 清空后隐藏分析结果面板
    heartbeatLostEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    searchResults.clear();
    searchResultView->clearResults();
    batteryinfo.clear();
    allusage.clear();
    soctmp.clear();
    triggerCount = 0;
    flightCount = 0;

    // 第一步：快速解压所有 zip 文件（递归扫描子目录）
    std::function<void(const QString&)> extractAllZips;
    extractAllZips = [&extractAllZips, this](const QString &dirPath) {
        QDir dir(dirPath);
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                   QDir::DirsFirst | QDir::Name);

        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                extractAllZips(entry.filePath());
                continue;
            }

            if (entry.isFile() && entry.fileName().endsWith(".zip", Qt::CaseInsensitive)) {
                QString zipPath = entry.filePath();
                QString extractDir = entry.absolutePath();

                // 更新状态栏显示当前处理的zip文件
                // 静默解压
                QApplication::processEvents(); // 保持界面响应

                // 使用统一的解压函数（Windows使用PowerShell，Linux/macOS使用unzip）
                extractZipFile(zipPath, extractDir);

                // 处理Qt事件，保持界面响应
                QApplication::processEvents();
            }
        }
    };

    // 第二步：递归收集所有相关文件
    std::function<QStringList(const QString&)> collectAllFiles;
    collectAllFiles = [&collectAllFiles, this](const QString &dirPath) -> QStringList {
        QStringList allFiles;
        QDir dir(dirPath);
        QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                   QDir::DirsFirst | QDir::Name);

        for (const QFileInfo &entry : entries) {
            if (entry.isDir()) {
                allFiles += collectAllFiles(entry.filePath());
                continue;
            }

            if (entry.isFile()) {
                QString fileName = entry.fileName();
                if (fileName.endsWith(".log", Qt::CaseInsensitive) ||
                    fileName.endsWith(".ulg", Qt::CaseInsensitive) ||
                    fileName.endsWith(".csv", Qt::CaseInsensitive) ||
                    fileName.endsWith(".txt", Qt::CaseInsensitive) ||
                    fileName.endsWith(".hlog", Qt::CaseInsensitive)) {
                    allFiles.append(entry.filePath());
                }
            }
        }

        return allFiles;
    };

    // 先解压所有zip文件
    // 静默
    extractAllZips(path);

    // 然后收集所有相关文件
    // 静默
    QStringList allFiles = collectAllFiles(path);

        if (allFiles.isEmpty()) {
        QMessageBox::information(this, "提示", "未找到相关文件");
        logView->setUpdatesEnabled(true);
        return;
    }

    // 如果只有一个文件，直接打开
    if (allFiles.size() == 1) {
        loadSelectedFilesInOrder(allFiles);
        // 标签名更新已移入 loadSelectedFilesInOrder 的异步完成回调中
        return;
    }

    // 创建文件选择对话框
    QDialog dialog(this);
    dialog.setWindowTitle("选择要显示的文件");
    dialog.resize(600, 400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // 添加说明标签
    QLabel *label = new QLabel("请选择要显示的文件（支持多选，按点击顺序显示，再次点击取消选择）:");
    layout->addWidget(label);

    // 创建文件列表，禁用系统多选，由点击事件手动管理选择顺序
    QListWidget *fileList = new QListWidget();
    fileList->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(fileList);

    // 用于记录按点击顺序排列的文件路径
    QStringList *clickOrderFiles = new QStringList();

    // 添加文件到列表，显示文件名和大小
    for (const QString &filePath : allFiles) {
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();
        qint64 fileSize = fileInfo.size();
        QString sizeStr = (fileSize > 1024 * 1024) ?
            QString("%1 MB").arg(fileSize / (1024.0 * 1024.0), 0, 'f', 1) :
            QString("%1 KB").arg(fileSize / 1024.0, 0, 'f', 1);

        QString displayText = QString("%1 (%2)").arg(fileName).arg(sizeStr);
        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, filePath); // 存储完整路径
        // 用 Qt::UserRole+1 存储选择序号（0 = 未选）
        item->setData(Qt::UserRole + 1, 0);
        fileList->addItem(item);
    }

    // 点击时按序号管理选择顺序
    connect(fileList, &QListWidget::itemClicked, [fileList, clickOrderFiles](QListWidgetItem *item){
        QString filePath = item->data(Qt::UserRole).toString();
        int currentOrder = item->data(Qt::UserRole + 1).toInt();

        if (currentOrder > 0) {
            // 已选中 -> 取消选择，移出列表，并更新其他项的序号
            clickOrderFiles->removeAll(filePath);
            item->setData(Qt::UserRole + 1, 0);
            item->setBackground(QBrush());
            item->setText(item->text().left(item->text().lastIndexOf(" [")));
            // 重新编号剩余选中项
            for (int i = 0; i < fileList->count(); ++i) {
                QListWidgetItem *it = fileList->item(i);
                int ord = it->data(Qt::UserRole + 1).toInt();
                if (ord > 0) {
                    int newOrd = clickOrderFiles->indexOf(it->data(Qt::UserRole).toString()) + 1;
                    it->setData(Qt::UserRole + 1, newOrd);
                    // 更新显示文本中的序号标记
                    QString baseText = it->text();
                    int bracketPos = baseText.lastIndexOf(" [");
                    if (bracketPos >= 0) baseText = baseText.left(bracketPos);
                    it->setText(QString("%1 [%2]").arg(baseText).arg(newOrd));
                }
            }
        } else {
            // 未选中 -> 追加到选择列表
            clickOrderFiles->append(filePath);
            int order = clickOrderFiles->size();
            item->setData(Qt::UserRole + 1, order);
            item->setBackground(QBrush(QColor(173, 216, 230))); // 浅蓝色背景
            item->setText(QString("%1 [%2]").arg(item->text()).arg(order));
        }
    });

    // 添加按钮
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton("确定");
    QPushButton *cancelButton = new QPushButton("取消");
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);

    // 连接信号
    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    // 显示对话框
    bool loadStarted = false;
    if (dialog.exec() == QDialog::Accepted) {
        // 按点击顺序加载文件
        if (!clickOrderFiles->isEmpty()) {
            loadSelectedFilesInOrder(*clickOrderFiles);
            loadStarted = true;
            // 标签名更新已移入 loadSelectedFilesInOrder 的异步完成回调中
        }
    }

    delete clickOrderFiles;

    // 仅在未启动异步加载时恢复界面更新（异步加载在完成回调中恢复）
    if (!loadStarted) {
        logView->setUpdatesEnabled(true);
    }
}

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
void PressAnalyzer::saveCurrentFile()
{
    // 取当前标签的源文件路径
    QString filePath;
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
        filePath = m_tabStates[m_currentTabIndex].sourcePath;

    if (filePath.isEmpty()) {
        // 没有关联文件，弹出另存为对话框
        filePath = QFileDialog::getSaveFileName(this, "保存文件", "", "所有文件 (*)");
        if (filePath.isEmpty()) return;
        if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
            m_tabStates[m_currentTabIndex].sourcePath = filePath;
        if (m_tabBar && m_currentTabIndex >= 0)
            m_tabBar->setTabText(m_currentTabIndex, QFileInfo(filePath).fileName());
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "保存失败", QString("无法写入文件：%1").arg(filePath));
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");

    // 如果内容含7字符行号前缀（由解析器加载），保存时去掉前缀还原原始内容
    bool stripPrefix = (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
                       && m_tabStates[m_currentTabIndex].hasLinePrefix;
    if (stripPrefix) {
        const QString text = logView->toPlainText();
        for (const QStringRef &line : text.splitRef('\n')) {
            // 每行前7个字符是行号前缀（6位数字+1空格），去掉后写入
            if (line.length() >= 7)
                out << line.mid(7) << '\n';
            else
                out << line << '\n';
        }
    } else {
        out << logView->toPlainText();
    }
    file.close();

    if (statusPathLabel) statusPathLabel->setText(QString("已保存：%1").arg(filePath));
    logView->document()->setModified(false);
    markTabModified(false);
}

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

// 仅清空当前 tab 的 UI 与数据状态（不动其它 tab）。供解析前预清理使用。
void PressAnalyzer::clearCurrentTabContent()
{
    // 切换回日志视图页（关闭可能正在显示的 DB 视图）
    centralStack->setCurrentIndex(0);

    allEvents.clear();
    allLogLines.clear();
    eventList->clear();
    logView->clear();
    // clear() 内部调用 document()->clear()，会重置 defaultFont，需要重新应用
    logView->setFont(currentLogFont);
    logView->document()->setDefaultFont(currentLogFont);  // 显式同步文档默认字体
    // 重置 cursor currentCharFormat：避免残留的红底格式污染下一次解析
    {
        QTextCursor cur = logView->textCursor();
        cur.setCharFormat(QTextCharFormat());
        cur.setBlockCharFormat(QTextCharFormat());
        logView->setTextCursor(cur);
        logView->setCurrentCharFormat(QTextCharFormat());
    }
    searchResultView->clearResults();
    // clearResults() 内部调用 clear()，同样需要重新应用字体
    searchResultView->setFont(currentLogFont);
    searchResultView->hide();
    searchResults.clear();
    searchDock->hide();
    currentSearchIndex = -1;
    triggerCount = 0;
    flightCount = 0;
    cameraEventList->clear();
    if (eventTimeline) eventTimeline->clear();
    heartbeatLostEventList->clear();
    statusEvents.clear();
    batteryChart->clear();
    if (socChart) socChart->clear();
    statusDock->hide();
    batteryinfo.clear();
    soctmp.clear();
    logView->moveCursor(QTextCursor::Start);
    QTextCursor cursor = logView->textCursor();
    cursor.clearSelection();
    logView->setTextCursor(cursor);
    allusage.clear();
    if (usageChart) usageChart->setData(allusage);
    if (statusPathLabel) statusPathLabel->setText("就绪");
    if (statusInfoLabel) statusInfoLabel->setText("");
    setWindowTitle("日志分析工具");

    // 重置当前标签的 CE/DB 标志与 sourcePath
    if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size()) {
        m_tabStates[m_currentTabIndex].isControlEngine = false;
        m_tabStates[m_currentTabIndex].isDbTab         = false;
        m_tabStates[m_currentTabIndex].dbPath.clear();
        m_tabStates[m_currentTabIndex].sourcePath.clear();
    }
    if (m_tabBar && m_currentTabIndex >= 0)
        m_tabBar->setTabText(m_currentTabIndex, "新标签");
}

// 清空：关闭所有标签页，恢复到最初始的单个空白标签状态
void PressAnalyzer::clearWindow()
{
    // 关闭数据库连接
    if (currentDb.isOpen()) {
        currentDb.close();
    }
    QString dbConn = "dbviewer_conn";
    if (QSqlDatabase::contains(dbConn)) {
        QSqlDatabase::removeDatabase(dbConn);
    }
    currentDbPath.clear();
    if (dbTableModel) {
        if (dbTableView) dbTableView->setModel(nullptr);
        delete dbTableModel;
        dbTableModel = nullptr;
    }

    // 释放非激活标签持有的文档（激活标签的文档由 logView 管理，下面会 clear）
    if (m_tabBar) {
        m_tabBar->blockSignals(true);
        for (int i = m_tabStates.size() - 1; i >= 0; --i) {
            if (i != m_currentTabIndex && m_tabStates[i].document) {
                delete m_tabStates[i].document;
                m_tabStates[i].document = nullptr;
            }
        }
        // 移除除第 0 个之外的所有 tab（同步移除颜色映射）
        while (m_tabBar->count() > 1) {
            int last = m_tabBar->count() - 1;
            m_tabBar->removeTab(last);
            m_tabBar->shiftColorsAfterRemove(last);
        }
        m_tabBar->setTabColor(0, tabColorForIndex(0));
        m_tabBar->setTabText(0, "新标签");
        m_tabBar->setCurrentIndex(0);
        m_tabBar->blockSignals(false);
    }

    // 重置 m_tabStates 到单个初始 TabState
    m_tabStates.clear();
    {
        TabState initState;
        initState.logFont       = currentLogFont;
        initState.logFontPtSize = logFontPointSize;
        m_tabStates.append(initState);
    }
    m_currentTabIndex = 0;

    // 清空当前（唯一）tab 的内容
    clearCurrentTabContent();

    // 重置全局按钮点击状态
    anyFileButtonClicked = false;
}

void PressAnalyzer::loadSelectedFiles(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        logView->setUpdatesEnabled(true);
        return;
    }

    // 对文件进行排序（按数字顺序）
    QStringList sortedFiles = filePaths;
    // 两个正则只差扩展名顺序，合并为一个即可（字母序不变）。静态构造避免每次比较都编译。
    static const QRegularExpression kFileNameRx(
        QStringLiteral("(.+?)(?:\\.(\\d+))?\\.(log|ulg|csv)$"));
    std::sort(sortedFiles.begin(), sortedFiles.end(), [](const QString &a, const QString &b) {
        const QString fileNameA = QFileInfo(a).fileName();
        const QString fileNameB = QFileInfo(b).fileName();

        QString baseNameA, baseNameB;
        int numA = -1, numB = -1;

        const auto mA = kFileNameRx.match(fileNameA);
        if (mA.hasMatch()) {
            baseNameA = mA.captured(1);
            const QString numStr = mA.captured(2);
            if (!numStr.isEmpty()) numA = numStr.toInt();
        }

        const auto mB = kFileNameRx.match(fileNameB);
        if (mB.hasMatch()) {
            baseNameB = mB.captured(1);
            const QString numStr = mB.captured(2);
            if (!numStr.isEmpty()) numB = numStr.toInt();
        }

        // 如果基础名称相同，按数字排序
        if (baseNameA == baseNameB) {
            if (numA == -1 && numB == -1) return false;
            if (numA == -1) return true;
            if (numB == -1) return false;
            return numA < numB;
        }

        // 基础名称不同，按字母排序
        return baseNameA < baseNameB;
    });

    // 检查文件大小，如果总大小超过100MB，显示警告
    qint64 totalSize = 0;
    for (const QString &filePath : sortedFiles) {
        QFileInfo fileInfo(filePath);
        totalSize += fileInfo.size();
    }

    if (totalSize > 100 * 1024 * 1024) { // 100MB
        QMessageBox::StandardButton reply = QMessageBox::question(this, "文件过大警告",
            QString("选中的文件总大小约为 %1 MB，加载可能需要较长时间。是否继续？")
            .arg(totalSize / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            logView->setUpdatesEnabled(true);
            return;
        }
    }

        // 清空之前的内容
    allLogLines.clear();
    logView->clear();
    // clear() 内部调用 document()->clear()，会重置 defaultFont，需要重新应用
    logView->setFont(currentLogFont);
    // 关键：clear() 不会重置 textCursor 的 currentCharFormat。
    // 若用户上一次点击时光标停在被 highlightLine 染红的行，
    // 后续 appendPlainText 会用这个 cursor 的 charFormat 作为新 block 的格式，
    // 导致整段新日志全部变红。这里显式把当前输入格式重置为默认。
    {
        QTextCursor cur = logView->textCursor();
        cur.setCharFormat(QTextCharFormat());
        cur.setBlockCharFormat(QTextCharFormat());
        logView->setTextCursor(cur);
        logView->setCurrentCharFormat(QTextCharFormat());
    }

    // 使用QPlainTextEdit的append方法，避免内存问题
    int totalLineNumber = 0;
    int processedFiles = 0;

    for (const QString &filePath : sortedFiles) {
        processedFiles++;
        // 静默：原有进度提示已移除

        QFileInfo fileInfo(filePath);
        QString extension = fileInfo.suffix().toLower();

        // 添加文件分隔符
        if (totalLineNumber > 0) {
            logView->appendPlainText(QString("\n\n=== 文件: %1 ===\n\n").arg(fileInfo.fileName()));
        }

        // 检查是否是 .hlog 二进制文件
        if (extension == "hlog") {
            HLogBinaryParser binaryParser;
            QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);

            if (entries.isEmpty()) {
                qWarning() << "无法解析 .hlog 文件:" << filePath;
                continue;
            }

            // 将解析的条目添加到视图（保证多行内容逐行编号）
            for (const HLogEntry &entry : entries) {
                const QStringList lines = entry.fullText.split('\n');
                for (const QString &l : lines) {
                    totalLineNumber++;
                    allLogLines << l;
                    logView->appendPlainText(l);
                }
            }
        } else {
            // 处理文本文件
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            QTextStream in(&file);
            in.setCodec("UTF-8");

            // 正则表达式用于检测日志条目开始
            static const QRegularExpression timestampPattern(R"(\[\d+\.?\d*\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\])");
            static const QRegularExpression levelFilePattern(R"(\[[IDWEF]\|[^:]+\:\d+\])");

            // 分块读取文件，避免一次性加载大文件到内存
            const int chunkSize = 1000; // 每次处理1000行
            QStringList lines;
            int lineCount = 0;
            bool inMultiLineEntry = false;

            while (!in.atEnd()) {
                lines.clear();

                // 读取一个块的行
                for (int i = 0; i < chunkSize && !in.atEnd(); ++i) {
                    QString line = in.readLine();
                    lines.append(line);
                    totalLineNumber++;
                    allLogLines << line;
                }

                // 处理这个块的行
                for (const QString &line : lines) {
                    QString trimmedLine = line.trimmed();
                    bool isLogEntryStart = false;

                    // 检查是否是日志条目开始（包含时间戳或级别信息）
                    if (!trimmedLine.isEmpty()) {
                        QRegularExpressionMatch timestampMatch = timestampPattern.match(trimmedLine);
                        QRegularExpressionMatch levelMatch = levelFilePattern.match(trimmedLine);
                        isLogEntryStart = timestampMatch.hasMatch() || levelMatch.hasMatch();
                    }

                    if (isLogEntryStart) {
                        // 新的日志条目开始
                        inMultiLineEntry = true;
                        logView->appendPlainText(line);
                    } else if (inMultiLineEntry && !trimmedLine.isEmpty()) {
                        // 多行日志的后续行
                        logView->appendPlainText(line);
                    } else {
                        // 空行或独立行，正常处理
                        if (trimmedLine.isEmpty()) {
                            inMultiLineEntry = false;  // 空行结束多行条目
                        }
                        logView->appendPlainText(line);
                    }
                    lineCount++;
                }

                // 处理Qt事件，保持界面响应
                QApplication::processEvents();
            }

            file.close();
        }
    }

    // 更新状态栏
    // 静默

    // 设置窗口标题
    setWindowTitle(QString("日志查看器 - %1 个文件").arg(sortedFiles.size()));

    // 解析完成后恢复更新
    logView->setUpdatesEnabled(true);
}

void PressAnalyzer::loadSelectedFilesInOrder(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        logView->setUpdatesEnabled(true);
        return;
    }

    // 检查文件大小，如果总大小超过100MB，显示警告
    qint64 totalSize = 0;
    for (const QString &filePath : filePaths) {
        QFileInfo fileInfo(filePath);
        totalSize += fileInfo.size();
    }

    if (totalSize > 100 * 1024 * 1024) { // 100MB
        QMessageBox::StandardButton reply = QMessageBox::question(this, "文件过大警告",
            QString("选中的文件总大小约为 %1 MB，加载可能需要较长时间。是否继续？")
            .arg(totalSize / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            logView->setUpdatesEnabled(true);
            return;
        }
    }

    // 清空之前的内容
    allLogLines.clear();
    logView->clear();
    logView->setFont(currentLogFont);
    // 同 loadSelectedFiles：重置 cursor currentCharFormat，避免上一次
    // 染红的光标位置污染新 append 的内容
    {
        QTextCursor cur = logView->textCursor();
        cur.setCharFormat(QTextCharFormat());
        cur.setBlockCharFormat(QTextCharFormat());
        logView->setTextCursor(cur);
        logView->setCurrentCharFormat(QTextCharFormat());
    }
    logView->setUpdatesEnabled(false);

    // 显示独立进度窗口
    if (!m_parseProgressDialog) m_parseProgressDialog = new ParseProgressDialog(this);
    m_parseProgressDialog->setProgress(0, tr("开始合并加载日志..."));
    m_parseProgressDialog->show();
    m_parseProgressDialog->raise();

    // ---- 后台线程执行文件 I/O 和缓冲区构建 ----
    struct GenericLoadResult {
        QStringList logLines;
        QString     textBuffer;
        int         fileCount;
    };

    const qint64 reserveSize = totalSize;
    QStringList filesCopy = filePaths;   // 拷贝一份，lambda 捕获值

    QFuture<GenericLoadResult> future = QtConcurrent::run(
        [filesCopy, reserveSize]() -> GenericLoadResult {
            GenericLoadResult r;
            r.fileCount = filesCopy.size();
            r.textBuffer.reserve(reserveSize + reserveSize / 10);
            r.logLines.reserve(reserveSize / 60);

            int totalLineNumber = 0;

            for (const QString &filePath : filesCopy) {
                QFileInfo fileInfo(filePath);
                QString extension = fileInfo.suffix().toLower();

                // 添加文件分隔符
                if (totalLineNumber > 0) {
                    r.textBuffer += QString("\n\n=== 文件: %1 ===\n\n").arg(fileInfo.fileName());
                }

                // 检查是否是 .hlog 二进制文件
                if (extension == "hlog") {
                    HLogBinaryParser binaryParser;
                    QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);

                    if (entries.isEmpty()) {
                        qWarning() << "无法解析 .hlog 文件:" << filePath;
                        continue;
                    }

                    // 将解析的条目添加到缓冲区
                    for (const HLogEntry &entry : entries) {
                        const QStringList lines = entry.fullText.split('\n');
                        for (const QString &l : lines) {
                            totalLineNumber++;
                            r.logLines << l;
                            r.textBuffer += l;
                            r.textBuffer += '\n';
                        }
                    }
                } else {
                    // 处理文本文件
                    QFile file(filePath);
                    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        continue;
                    }

                    QTextStream in(&file);
                    in.setCodec("UTF-8");

                    // 分块读取文件
                    const int chunkSize = 4000;  // 加大块大小，减少循环开销
                    QStringList lines;
                    int lineCount = 0;

                    while (!in.atEnd()) {
                        lines.clear();

                        // 读取一个块的行
                        for (int i = 0; i < chunkSize && !in.atEnd(); ++i) {
                            QString line = in.readLine();
                            lines.append(line);
                            totalLineNumber++;
                            r.logLines << line;
                        }

                        // 处理这个块的行
                        for (const QString &line : lines) {
                            r.textBuffer += line;
                            r.textBuffer += '\n';
                            lineCount++;
                        }
                    }

                    file.close();
                }
            }
            return r;
        }
    );

    auto *watcher = new QFutureWatcher<GenericLoadResult>(this);
    connect(watcher, &QFutureWatcher<GenericLoadResult>::finished, this,
        [this, watcher]() {
            GenericLoadResult result = watcher->result();
            watcher->deleteLater();

            allLogLines = std::move(result.logLines);
            // Qt 的 setPlainText 会保留 cursor 的 charFormat（见 onParseFinished 同类注释）。
            // 必须在 setPlainText 之前重置，否则上一次停在红色行上的光标会让
            // 整个新文档继承红底格式。
            {
                QTextCursor cur = logView->textCursor();
                cur.setCharFormat(QTextCharFormat());
                cur.setBlockCharFormat(QTextCharFormat());
                logView->setTextCursor(cur);
                logView->setCurrentCharFormat(QTextCharFormat());
            }
            m_loadingFile = true;
            logView->setPlainText(result.textBuffer);
            m_loadingFile = false;
            // 加载完成后立即把脏位归零；后续用户真正编辑时
            // modificationChanged(true) 才会触发 markTabModified
            logView->document()->setModified(false);
            markTabModified(false);
            if (m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size())
                m_tabStates[m_currentTabIndex].hasLinePrefix = false;
            logView->setFont(currentLogFont);

            setWindowTitle(QString("日志查看器 - %1 个文件").arg(result.fileCount));

            // 更新标签名
            if (m_tabBar && m_currentTabIndex >= 0 && m_currentTabIndex < m_tabStates.size()) {
                const QString &sp = m_tabStates[m_currentTabIndex].sourcePath;
                if (!sp.isEmpty())
                    m_tabBar->setTabText(m_currentTabIndex, QFileInfo(sp).fileName());
            }

            if (m_parseProgressDialog) {
                m_parseProgressDialog->markFinished(tr("加载完成"));
                m_parseProgressDialog->close();
            }
            logView->setUpdatesEnabled(true);
        }
    );
    watcher->setFuture(future);
}

// ---- 拖放文件直接打开 ----

void PressAnalyzer::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void PressAnalyzer::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // 支持的文件扩展名（小写）
    static const QSet<QString> kTextSuffixes = {
        "log", "txt", "hlog", "csv", "ulg",
        "c", "cpp", "cc", "cxx",
        "h", "hpp", "hxx",
        "py", "js", "ts", "java", "kt",
        "json", "xml", "yaml", "yml", "toml", "ini", "conf",
        "md", "sh", "bash", "zsh", "bat", "cmake"
    };

    // 每个拖入的文件/目录：当前标签为空则复用，否则新建标签
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) continue;
        QString path = url.toLocalFile();
        QFileInfo fi(path);
        const QString suffix = fi.suffix().toLower();

        // DB 文件：始终在新标签页中打开（保留任意当前标签内容）
        if (fi.isFile() && (suffix == "db" || suffix == "sqlite" || suffix == "sqlite3")) {
            openDatabaseInNewTab(path);
            continue;
        }

        // 目录或已知文本后缀或无后缀 → 打开
        if (fi.isDir() || kTextSuffixes.contains(suffix) || suffix.isEmpty()) {
            bool currentTabEmpty = (m_currentTabIndex >= 0 &&
                                    m_currentTabIndex < m_tabStates.size() &&
                                    m_tabStates[m_currentTabIndex].sourcePath.isEmpty());
            if (currentTabEmpty) {
                loadPathSmart(path);
            } else {
                openInNewTab(path);
            }
        }
    }
    event->acceptProposedAction();
}
