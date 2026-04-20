// PressAnalyzerParser.cpp - Log parsing and analysis functions
#include "PressAnalyzer.h"

#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QDebug>
#include <QDateTime>
#include <QApplication>
#include "HLogBinaryParser.h"

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

    // 如果是第一个事件，显示侧边栏分析结果面板
    if (eventList->count() == 1) {
        m_sideBar->showPanel(1);
    }
}

bool PressAnalyzer::analyzeLogSourceFile(const QString &filePath,
                                        int &lineNumber,
                                        QDateTime &currentTakeoffTime,
                                        QString &textBuffer,
                                        bool &inRecvException,
                                        QStringList &recvExceptionLines,
                                        bool showWarning)
{
    const QString extension = QFileInfo(filePath).suffix().toLower();
    if (extension == "hlog") {
        HLogBinaryParser binaryParser;
        const QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);
        if (entries.isEmpty()) {
            if (showWarning) {
                QMessageBox::warning(this, "错误", "无法解析 .hlog 文件：" + filePath);
            } else {
                qWarning() << "无法解析 .hlog 文件:" << filePath;
            }
            return false;
        }

        for (const HLogEntry &entry : entries) {
            const QStringList lines = entry.fullText.split('\n');
            for (const QString &line : lines) {
                analyzeLogLine(line, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
            }
        }
        return true;
    }

    analyzeFile(filePath, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
    return true;
}

QStringList PressAnalyzer::collectOrderedLogFiles(const QString &baseDir,
                                                 const QString &subDir,
                                                 const QString &baseName,
                                                 const QStringList &extensions)
{
    QStringList result;
    QDir dir(baseDir + "/" + subDir);
    if (!dir.exists() || extensions.isEmpty()) {
        return result;
    }

    auto buildFilePatterns = [&](bool zipped) {
        QStringList patterns;
        for (const QString &ext : extensions) {
            patterns << QString("%1*.%2%3").arg(baseName, ext, zipped ? ".zip" : "");
        }
        return patterns;
    };

    QStringList logFiles = dir.entryList(buildFilePatterns(false), QDir::Files, QDir::Name);
    if (logFiles.isEmpty()) {
        const QStringList zipFiles = dir.entryList(buildFilePatterns(true), QDir::Files, QDir::Name);
        for (const QString &zipName : zipFiles) {
            extractZipFile(dir.filePath(zipName), dir.absolutePath());
            QApplication::processEvents();
        }
        if (!zipFiles.isEmpty()) {
            QThread::msleep(100);
            QApplication::processEvents();
            logFiles = dir.entryList(buildFilePatterns(false), QDir::Files, QDir::Name);
        }
    }

    if (logFiles.isEmpty()) {
        return result;
    }

    const QString extPattern = extensions.join('|');
    const QRegularExpression numberedRe(
        QString("^%1(?:\\.?([0-9]+))?\\.(%2)$")
            .arg(QRegularExpression::escape(baseName), extPattern),
        QRegularExpression::CaseInsensitiveOption);

    QList<QPair<int, QString>> numberedFiles;
    QStringList lastFiles;
    for (const QString &fileName : logFiles) {
        const QRegularExpressionMatch match = numberedRe.match(fileName);
        if (!match.hasMatch()) {
            continue;
        }
        if (match.captured(1).isEmpty()) {
            lastFiles << dir.filePath(fileName);
        } else {
            numberedFiles.append(qMakePair(match.captured(1).toInt(), dir.filePath(fileName)));
        }
    }

    std::sort(numberedFiles.begin(), numberedFiles.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  if (a.first != b.first) {
                      return a.first < b.first;
                  }
                  return a.second < b.second;
              });

    for (const auto &item : numberedFiles) {
        result << item.second;
    }
    std::sort(lastFiles.begin(), lastFiles.end());
    result << lastFiles;
    return result;
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

    while (!in.atEnd()) {
        const QString line = in.readLine();
        analyzeLogLine(line, lineNumber, currentTakeoffTime, textBuffer, inRecvException, recvExceptionLines);
    }
}

void PressAnalyzer::analyzeLogLine(const QString &line,
                                  int &lineNumber,
                                  QDateTime &currentTakeoffTime,
                                  QString &textBuffer,
                                  bool &inRecvException,
                                  QStringList &recvExceptionLines)
{
    lineNumber++;
    allLogLines << line;

    textBuffer.reserve(textBuffer.size() + line.size() + 16);
    textBuffer.append(QString("%1 %2\n")
                          .arg(lineNumber, 6, 10, QChar(' '))
                          .arg(line));

    // ==================== 预编译正则表达式 ====================
    static const QRegularExpression rePressPower(R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*\])");
    static const QRegularExpression reTakeoff(R"(trigger source:\s*(\d+)\s+flight mode:\s*(\w+))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTs(R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reFcState(R"(Detected fc state changed to\s+(\d+))");
    static const QRegularExpression reInsertSql(R"(insertMediaDataIntoDb insert media sql.*\[(.*)\])", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSqlValues(R"('([^']*)'|(\d+))");
    static const QRegularExpression reCameraAction(R"(recv action from camera\s*:\s*(\d+))", QRegularExpression::CaseInsensitiveOption);

    // 不再从日志正文解析 SN，改为从 system_log/user.log 中获取

    // ==================== press once power key ====================
    if (line.contains("press once power key", Qt::CaseInsensitive)) {
        QString timestamp;
        auto m = rePressPower.match(line);
        if (m.hasMatch()) timestamp = m.captured(2);
        triggerCount++;
        QString display = QString("%1 | %2# press power key : [%3]")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(timestamp);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== 起飞事件 ====================
    if (line.contains("start takeoff. powerkey trigger source", Qt::CaseInsensitive)) {
        QString triggerText = "未知";
        auto m = reTakeoff.match(line);
        if (m.hasMatch()) {
            int src = m.captured(1).toInt();
            modeText = m.captured(2);
            if (version == "H151") {
                switch (src) {
                case 0: triggerText = "NONE"; break;
                case 1: triggerText = "APP"; break;
                case 2: triggerText = "VOICE"; break;
                case 3: triggerText = "THROW"; break;
                case 4: triggerText = "RC102"; break;
                case 5: triggerText = "RC100"; break;
                case 6: triggerText = "ROPE"; break;
                case 10: triggerText = "BOARD"; break;
                case 11: triggerText = "MCU"; break;
                default: triggerText = "UNKNOWN"; break;
                }
            } else {
                switch (src) {
                case 0: triggerText = "BOARD"; break;
                case 1: triggerText = "MCU"; break;
                case 2: triggerText = "APP"; break;
                case 3: triggerText = "RC"; break;
                case 4: triggerText = "VOICE"; break;
                case 5: triggerText = "THROW"; break;
                default: triggerText = "UNKNOWN"; break;
                }
            }
        }
        QString display = QString("%1 | %2# starting takeoff : 方式:%3 模式:%4")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(triggerText)
                              .arg(modeText);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== fly_power: takeoff success ====================
    if (line.contains("fly_power: takeoff success", Qt::CaseInsensitive)) {
        auto m = reTs.match(line);
        if (m.hasMatch()) {
            currentTakeoffTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");
        }

        QString display = QString("%1 | %2# takeoff success,Flying")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount);
        addEventToList(triggerCount, lineNumber, display);
    }

    // ==================== FC STATE ====================
    auto mFc = reFcState.match(line);
    if (mFc.hasMatch()) {
        int stateValue = mFc.captured(1).toInt();
        QString stateName;
        switch (stateValue) {
        case 0: stateName = "DISARM"; break;
        case 1: stateName = "ARM"; break;
        case 2: stateName = "TAKINGOFF"; break;
        case 3: stateName = "FLYING"; break;
        case 4: stateName = "LANDING"; break;
        case 5: stateName = "TURTLE_ROLLING"; break;
        default: stateName = QString("UNKNOWN(%1)").arg(stateValue); break;
        }
        QString display = QString("%1 | %2# FC STATE -> %3")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
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
                                      .arg(triggerCount)
                                      .arg(flightSeconds);
                addEventToList(triggerCount, lineNumber, display);
                currentTakeoffTime = QDateTime();
            }
        }
    }

    // ==================== insertMediaDataIntoDb ====================
    auto mSql = reInsertSql.match(line);
    if (mSql.hasMatch()) {
        QString sql = mSql.captured(1);

        QRegularExpression reCols(R"(INSERT\s+INTO\s+MEDIADATA\s*\(([^)]*)\)\s*VALUES)",
                                  QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch mc = reCols.match(sql);
        QStringList colNames;
        if (mc.hasMatch()) {
            QString colsStr = mc.captured(1);
            for (QString c : colsStr.split(',', Qt::SkipEmptyParts)) {
                colNames << c.trimmed().toLower();
            }
        }

        QStringList values;
        auto it = reSqlValues.globalMatch(sql);
        while (it.hasNext()) {
            auto mm = it.next();
            values << (mm.captured(1).isEmpty() ? mm.captured(2) : mm.captured(1));
        }

        auto indexOfCol = [&](const QString &name, int fallback) -> int {
            int idx = colNames.indexOf(name);
            return idx >= 0 ? idx : fallback;
        };

        int idxUuid = indexOfCol("uuid", 0);
        int idxType = indexOfCol("type", 1);
        int idxPath = indexOfCol("path", 3);

        if (!colNames.isEmpty() && colNames.contains("flightid")) {
            idxType = indexOfCol("type", 2);
            idxPath = indexOfCol("path", 4);
        }

        if (values.size() > qMax(idxPath, qMax(idxType, idxUuid))) {
            QString uuid = values.value(idxUuid);
            int type = values.value(idxType).toInt();
            QString path = values.value(idxPath);

            auto typeToString = [](int type) -> QString {
                switch (type) {
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
            };

            QString typeStr = typeToString(type);

            QString storagePrefix;
            QString displayPath;
            if (path.startsWith("/media/internal/")) {
                storagePrefix = "Internal:";
                displayPath = path.mid(16);
            } else if (path.startsWith("/media/external/")) {
                storagePrefix = "External:";
                displayPath = path.mid(16);
            } else {
                storagePrefix = "Unknown:";
                displayPath = path;
            }

            QString display = QString("%1 | %2# Media UUID:%3 Type:%4 %5%6")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(triggerCount)
                                  .arg(uuid)
                                  .arg(typeStr)
                                  .arg(storagePrefix)
                                  .arg(displayPath);
            addEventToList(triggerCount, lineNumber, display);
        }
    }

    // ==================== recv exception ====================
    if (line.contains("recv exception :", Qt::CaseInsensitive)) {
        inRecvException = true;
        recvExceptionLines.clear();
        return;
    }
    if (inRecvException) {
        recvExceptionLines << line.trimmed();
        if (line.contains('}')) {
            inRecvException = false;
            for (const QString &l : recvExceptionLines) {
                if (l.startsWith("event:", Qt::CaseInsensitive) ||
                    l.startsWith("errors:", Qt::CaseInsensitive)) {
                    if (l.contains("NOTIFY_TURTLE_FLIP", Qt::CaseInsensitive)) {
                        triggerCount++;
                        QString display = QString("%1 | %2# %3")
                                             .arg(lineNumber - 1, 6, 10, QChar(' '))
                                             .arg(triggerCount)
                                             .arg(l.trimmed());
                        addEventToList(triggerCount, lineNumber - 1, display);
                    } else {
                        QString display = QString("%1 | %2# %3")
                                             .arg(lineNumber - 1, 6, 10, QChar(' '))
                                             .arg(triggerCount)
                                             .arg(l.trimmed());
                        addEventToList(triggerCount, lineNumber - 1, display);
                    }
                }
            }
        }
        return;
    }

    // ==================== manual_control_takeover_request ====================
    if (line.contains("manual_control_takeover_request", Qt::CaseInsensitive)) {
        QString display = QString("%1 | %2# 模式:%3 -> MANUAL")
                              .arg(lineNumber, 6, 10, QChar(' '))
                              .arg(triggerCount)
                              .arg(modeText);
        addEventToList(triggerCount, lineNumber, display);
        return;
    }

    // ==================== Camera recv action from camera ====================
    if (line.contains("recv action from camera", Qt::CaseInsensitive)) {
        auto mCamAct = reCameraAction.match(line);
        if (mCamAct.hasMatch()) {
            int actionValue = mCamAct.captured(1).toInt();
            QString actionName;
            switch (actionValue) {
            case 0: actionName = "INIT"; break;
            case 1: actionName = "START_VIDEO"; break;
            case 2: actionName = "FINISH_VIDEO"; break;
            case 3: actionName = "SNAP_DONE"; break;
            case 4: actionName = "START_PREVIEW"; break;
            case 5: actionName = "STOP_PREVIEW"; break;
            case 6: actionName = "START_CONTINOUS_PICTURE"; break;
            case 7: actionName = "STOP_CONTINOUS_PICTURE"; break;
            case 8: actionName = "IN_PREVIEWING"; break;
            case 9: actionName = "IN_VIDEO_RECORDING"; break;
            case 10: actionName = "SNAP_FILE_SAVE_DONE"; break;
            default: actionName = QString("UNKNOWN(%1)").arg(actionValue); break;
            }

            QString display = QString("%1 | %2# CS ACTION -> %3")
                                  .arg(lineNumber, 6, 10, QChar(' '))
                                  .arg(triggerCount)
                                  .arg(actionName);
            addEventToList(triggerCount, lineNumber, display);
            return;
        }
    }

    // ==================== Camera 温度 ====================
    if (line.contains("camera temp", Qt::CaseInsensitive)) {
        QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
        QDateTime ts;
        if (rxTimestamp.indexIn(line) != -1) {
            QString dtStr = rxTimestamp.cap(2);
            QString tsStr = rxTimestamp.cap(1);
            
        // 解析日期和时间
        QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
        QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
        
        // 创建本地时间（日志中的时间是北京时间）
        ts = QDateTime(date, time, Qt::LocalTime);
            
            // 提取毫秒部分
            int dotPos = tsStr.indexOf('.');
            int msecs = 0;
            if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
                QString msecStr = tsStr.mid(dotPos + 1);
                // 确保毫秒部分有3位数字
                while (msecStr.length() < 3) {
                    msecStr += '0';
                }
                msecStr = msecStr.left(3); // 只取前3位
                msecs = msecStr.toInt();
            }
            ts = ts.addMSecs(msecs);
        }

        int tempVal = 0;
        bool found = false;
        QRegExp rxTempBoth("soc\\s+temp\\s+and\\s+camera\\s+temp\\s+(\\-?\\d+)\\s*:\\s*(\\-?\\d+)", Qt::CaseInsensitive);
        if (rxTempBoth.indexIn(line) != -1) {
            tempVal = rxTempBoth.cap(2).toInt();
            found = true;
        } else {
            QRegExp rxTemp("camera\\s+temp:?\\s*(\\-?\\d+)", Qt::CaseInsensitive);
            if (rxTemp.indexIn(line) != -1) {
                tempVal = rxTemp.cap(1).toInt();
                found = true;
            }
        }

        if (found && tempVal != -128) {
            cameraTemps.push_back({ts, tempVal});
        }
    }

    parseCameraStatus(lineNumber, line);
    parseStatusHeartbeat(lineNumber, line);
    parseStatusBattery(lineNumber, line);
    parseStatusSocTemp(lineNumber, line);
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
        heartbeatLostEventList->addItem(item);
        return;
    }

}
void PressAnalyzer::parseStatusBattery(int lineNumber, const QString &line)
{
    // ---------------- 解析电池信息 ----------------
    if (line.contains("battery info", Qt::CaseInsensitive)) {
        BatteryTimeInfo timeinfo;
        // 提取时间戳和日期时间
        QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\].*");
        if (rxTimestamp.indexIn(line) != -1) {
            QString tsStr = rxTimestamp.cap(1);   // 9733.000
            QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

            // 解析日期和时间
            QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
            QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
            
            // 创建本地时间（日志中的时间是北京时间）
            timeinfo.timestamp = QDateTime(date, time, Qt::LocalTime);

            // 提取毫秒部分
            int dotPos = tsStr.indexOf('.');
            int msecs = 0;
            if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
                QString msecStr = tsStr.mid(dotPos + 1);
                // 确保毫秒部分有3位数字
                while (msecStr.length() < 3) {
                    msecStr += '0';
                }
                msecStr = msecStr.left(3); // 只取前3位
                msecs = msecStr.toInt();
            }
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
    int row = heartbeatLostEventList->row(item);
    if (row < 0 || row >= statusEvents.size()) return;

    int lineNumber =  statusEvents[row].lineNumber;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;

    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();  // 居中显示
}

void PressAnalyzer::onTimelineJumpToLine(int lineNumber)
{
    if (lineNumber <= 0) return;
    QTextBlock block = logView->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    logView->setTextCursor(cursor);
    logView->centerCursor();
}

void PressAnalyzer::parseStatusSocTemp(int lineNumber, const QString &line)
{
    // 如果行中不包含关键字，直接跳过
    if (!line.contains("get soc max temp")) {
        return;
    }

    SocTempInfo info;

    // 提取时间戳和日期时间
    QRegExp rxTimestamp("\\[(\\d+(?:\\.\\d+)?)\\s+(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2})\\]");
    if (rxTimestamp.indexIn(line) != -1) {
        QString tsStr = rxTimestamp.cap(1);   // 9733 或 9733.000
        QString dtStr = rxTimestamp.cap(2);   // 2025-08-08 13:22:57

        // 解析日期和时间
        QDate date = QDate::fromString(dtStr.left(10), "yyyy-MM-dd");
        QTime time = QTime::fromString(dtStr.mid(11), "HH:mm:ss");
        
        // 创建本地时间（日志中的时间是北京时间）
        info.timestamp = QDateTime(date, time, Qt::LocalTime);

        // 提取毫秒部分
        int dotPos = tsStr.indexOf('.');
        int msecs = 0;
        if (dotPos != -1 && dotPos + 1 < tsStr.length()) {
            QString msecStr = tsStr.mid(dotPos + 1);
            // 确保毫秒部分有3位数字
            while (msecStr.length() < 3) {
                msecStr += '0';
            }
            msecStr = msecStr.left(3); // 只取前3位
            msecs = msecStr.toInt();
        }
        info.timestamp = info.timestamp.addMSecs(msecs);
    }

    // 提取 max temp
    QRegExp rxMax("get soc max temp\\s*[:=]\\s*(\\d+)");
    if (rxMax.indexIn(line) != -1) {
        info.maxTemp = rxMax.cap(1).toInt();
    }

    // 提取 core temp
    QRegExp rxCore("core temp\\s*[:=]\\s*([0-9:]+)");
    if (rxCore.indexIn(line) != -1) {
        QStringList temps = rxCore.cap(1).split(":");
        for (const QString &t : temps) {
            info.coreTemps.append(t.toInt());
        }
    }

    // 只保留有效数据点：timestamp 必须 valid，maxTemp 必须 > 0
    if (!info.timestamp.isValid() || info.maxTemp <= 0) return;
    soctmp.append(info);
}


// 将 "top - 14:03:27 ..." 这一行提取时间
QDateTime PressAnalyzer::parseTopTime(const QString &line) {
    QRegExp rx("top - (\\d{2}:\\d{2}:\\d{2})");
    if (rx.indexIn(line) != -1) {
        QString timeStr = rx.cap(1);
        QTime t = QTime::fromString(timeStr, "HH:mm:ss");
        // 使用当天日期，设置为本地时间（北京时间）
        return QDateTime(QDate::currentDate(), t, Qt::LocalTime);
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
        else if (moduleName ==  "fpv_service") usage.fpv_service = {cpu, mem};
    }

    // 最后一组数据也要存进去
    if (hasData) {
        allusage.append(usage);
    }
}
