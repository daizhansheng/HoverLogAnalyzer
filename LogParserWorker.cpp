#include "LogParserWorker.h"
#include "HLogBinaryParser.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QTime>
#include <QDate>
#include <QRegularExpression>
#include <QDebug>

// ============================================================
LogParserWorker::LogParserWorker(QObject *parent)
    : QObject(parent)
{}

void LogParserWorker::setFiles(const QStringList &filePaths)
{
    m_filePaths = filePaths;
}

void LogParserWorker::setVersion(const QString &version)
{
    m_version = version;
}

// ============================================================
// 主入口：由 QThread::started() 触发
// ============================================================
void LogParserWorker::run()
{
    ParseResult result;
    m_triggerCount = 0;
    m_modeText.clear();

    const int total = m_filePaths.size();
    if (total == 0) {
        emit parseFinished(result);
        return;
    }

    // 估算总行数避免 QStringList/QList 反复扩容（按文件大小 / 80 字节每行的经验值）
    qint64 totalBytes = 0;
    for (const QString &fp : m_filePaths) totalBytes += QFileInfo(fp).size();
    const int estLines = int(qMin<qint64>(totalBytes / 80, 5'000'000)); // 上限 500 万行兜底
    if (estLines > 0) {
        result.allLogLines.reserve(estLines);
        result.events.reserve(estLines / 200 + 64);          // 事件行约占 0.5%
        result.batteryinfo.reserve(estLines / 500 + 16);
        result.soctmp.reserve(estLines / 500 + 16);
        result.cameraTemps.reserve(estLines / 200 + 16);
    }

    int lineNumber = 0;
    QDateTime currentTakeoffTime;
    bool inRecvException = false;
    QStringList recvExceptionLines;

    for (int i = 0; i < total; ++i) {
        const QString &fp = m_filePaths[i];
        const QString fname = QFileInfo(fp).fileName();
        // 每个文件起始：上报"开始读取"
        emit progressChanged(i * 100 / total,
                             QString("解析 %1/%2: %3").arg(i + 1).arg(total).arg(fname));

        const int linesBefore = lineNumber;
        if (!analyzeSourceFile(fp, lineNumber, currentTakeoffTime,
                               inRecvException, recvExceptionLines, result)) {
            emit parseError(QString("无法解析文件: %1").arg(fp));
        }
        // 单文件结束：把"该文件累计行数"补一条进度，让大文件中途也有刷新
        const int linesInFile = lineNumber - linesBefore;
        emit progressChanged((i + 1) * 100 / total,
                             QString("已完成 %1/%2: %3 (%4 行)")
                                 .arg(i + 1).arg(total).arg(fname).arg(linesInFile));
    }

    result.triggerCount = m_triggerCount;
    emit progressChanged(100, "解析完成");
    emit parseFinished(result);
}

// ============================================================
// 单文件解析入口
// ============================================================
bool LogParserWorker::analyzeSourceFile(const QString &filePath,
                                         int &lineNumber,
                                         QDateTime &currentTakeoffTime,
                                         bool &inRecvException,
                                         QStringList &recvExceptionLines,
                                         ParseResult &result)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();

    if (ext == "hlog") {
        HLogBinaryParser binaryParser;
        const QList<HLogEntry> entries = binaryParser.parseFromFile(filePath);
        if (entries.isEmpty()) {
            qWarning() << "无法解析 .hlog 文件:" << filePath;
            return false;
        }
        for (const HLogEntry &entry : entries) {
            const QStringList lines = entry.fullText.split('\n');
            for (const QString &line : lines) {
                analyzeLogLine(line, lineNumber, currentTakeoffTime,
                               inRecvException, recvExceptionLines, result);
            }
        }
        return true;
    }

    // 文本文件：流式逐行读取，无 readAll()
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开文件:" << filePath;
        return false;
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");

    // 大文件中途也定期上报进度，避免单个 10MB+ 文件期间进度条长时间不动
    const qint64 fileBytes = QFileInfo(filePath).size();
    const QString fname = QFileInfo(filePath).fileName();
    int lastReportedLines = lineNumber;
    constexpr int kReportEveryLines = 50000;

    while (!in.atEnd()) {
        const QString line = in.readLine();
        analyzeLogLine(line, lineNumber, currentTakeoffTime,
                       inRecvException, recvExceptionLines, result);
        if (lineNumber - lastReportedLines >= kReportEveryLines) {
            lastReportedLines = lineNumber;
            const qint64 pos = in.device() ? in.device()->pos() : 0;
            const int filePct = (fileBytes > 0) ? int(pos * 100 / fileBytes) : 0;
            emit progressChanged(-1,  // -1 表示"内部进度"，由 GUI 决定是否更新百分比
                                 QString("正在读取 %1 (%2行, 约 %3%)")
                                     .arg(fname).arg(lineNumber).arg(filePct));
        }
    }
    return true;
}

// ============================================================
// 核心逐行解析（与原 PressAnalyzer::analyzeLogLine 等价，全部用 QRegularExpression）
// ============================================================
void LogParserWorker::analyzeLogLine(const QString &line,
                                      int &lineNumber,
                                      QDateTime &currentTakeoffTime,
                                      bool &inRecvException,
                                      QStringList &recvExceptionLines,
                                      ParseResult &result)
{
    lineNumber++;
    result.allLogLines << line;

    // 不再单独维护 textBuffer：GUI 端用 allLogLines.join('\n') 一次性构造，避免双倍内存与拷贝

    // ==================== 预编译正则（static，仅初始化一次）====================
    static const QRegularExpression rePressPower(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*\])");
    static const QRegularExpression reTakeoff(
        R"(trigger source:\s*(\d+)\s+flight mode:\s*(\w+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reTs(
        R"(\[\d+(?:\.\d+)?\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reFcState(
        R"(Detected fc state changed to\s+(\d+))");
    static const QRegularExpression reInsertSql(
        R"(insertMediaDataIntoDb insert media sql.*\[(.*)\])",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reSqlValues(
        R"('([^']*)'|(\d+))");
    static const QRegularExpression reCameraAction(
        R"(recv action from camera\s*:\s*(\d+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reCameraTs(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reCameraTempBoth(
        R"(soc\s+temp\s+and\s+camera\s+temp\s+(-?\d+)\s*:\s*(-?\d+))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reCameraTemp(
        R"(camera\s+temp:?\s*(-?\d+))",
        QRegularExpression::CaseInsensitiveOption);

    // ==================== press once power key ====================
    if (line.contains("press once power key", Qt::CaseInsensitive)) {
        QString timestamp;
        auto m = rePressPower.match(line);
        if (m.hasMatch()) timestamp = m.captured(2);
        m_triggerCount++;
        ParsedEventItem ev;
        ev.lineNumber = lineNumber;
        ev.triggerCount = m_triggerCount;
        ev.eventCategory = "main";
        ev.display = QString("%1 | %2# press power key : [%3]")
                         .arg(lineNumber, 6, 10, QChar(' '))
                         .arg(m_triggerCount)
                         .arg(timestamp);
        result.events << ev;
    }

    // ==================== 起飞事件 ====================
    if (line.contains("start takeoff. powerkey trigger source", Qt::CaseInsensitive)) {
        QString triggerText = "未知";
        auto m = reTakeoff.match(line);
        if (m.hasMatch()) {
            int src = m.captured(1).toInt();
            m_modeText = m.captured(2);
            if (m_version == "H151") {
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
        ParsedEventItem ev;
        ev.lineNumber = lineNumber;
        ev.triggerCount = m_triggerCount;
        ev.eventCategory = "main";
        ev.display = QString("%1 | %2# starting takeoff : 方式:%3 模式:%4")
                         .arg(lineNumber, 6, 10, QChar(' '))
                         .arg(m_triggerCount)
                         .arg(triggerText)
                         .arg(m_modeText);
        result.events << ev;
    }

    // ==================== fly_power: takeoff success ====================
    if (line.contains("fly_power: takeoff success", Qt::CaseInsensitive)) {
        auto m = reTs.match(line);
        if (m.hasMatch())
            currentTakeoffTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");

        ParsedEventItem ev;
        ev.lineNumber = lineNumber;
        ev.triggerCount = m_triggerCount;
        ev.eventCategory = "main";
        ev.display = QString("%1 | %2# takeoff success,Flying")
                         .arg(lineNumber, 6, 10, QChar(' '))
                         .arg(m_triggerCount);
        result.events << ev;
    }

    // ==================== FC STATE ====================
    if (line.contains("Detected fc state changed to")) {
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
            ParsedEventItem ev;
            ev.lineNumber = lineNumber;
            ev.triggerCount = m_triggerCount;
            ev.eventCategory = "main";
            ev.display = QString("%1 | %2# FC STATE -> %3")
                             .arg(lineNumber, 6, 10, QChar(' '))
                             .arg(m_triggerCount)
                             .arg(stateName, -12);
            result.events << ev;
        }
    }

    // ==================== fly_power: will landing ====================
    if (line.contains("fly_power: will landing", Qt::CaseInsensitive)) {
        auto m = reTs.match(line);
        if (m.hasMatch()) {
            QDateTime landingTime = QDateTime::fromString(m.captured(1), "yyyy-MM-dd HH:mm:ss");
            if (currentTakeoffTime.isValid() && landingTime.isValid()) {
                qint64 flightSeconds = currentTakeoffTime.secsTo(landingTime);
                ParsedEventItem ev;
                ev.lineNumber = lineNumber;
                ev.triggerCount = m_triggerCount;
                ev.eventCategory = "main";
                ev.display = QString("%1 | %2# will Landing, Flight Duration: %3 seconds")
                                 .arg(lineNumber, 6, 10, QChar(' '))
                                 .arg(m_triggerCount)
                                 .arg(flightSeconds);
                result.events << ev;
                currentTakeoffTime = QDateTime();
            }
        }
    }

    // ==================== insertMediaDataIntoDb ====================
    if (line.contains("insertMediaDataIntoDb", Qt::CaseInsensitive)) {
        auto mSql = reInsertSql.match(line);
        if (mSql.hasMatch()) {
            QString sql = mSql.captured(1);

            static const QRegularExpression reCols(
                R"(INSERT\s+INTO\s+MEDIADATA\s*\(([^)]*)\)\s*VALUES)",
                QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch mc = reCols.match(sql);
            QStringList colNames;
            if (mc.hasMatch()) {
                for (QString c : mc.captured(1).split(',', Qt::SkipEmptyParts))
                    colNames << c.trimmed().toLower();
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

                auto typeToString = [](int t) -> QString {
                    switch (t) {
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

                QString storagePrefix, displayPath;
                if (path.startsWith("/media/internal/")) {
                    storagePrefix = "Internal:"; displayPath = path.mid(16);
                } else if (path.startsWith("/media/external/")) {
                    storagePrefix = "External:"; displayPath = path.mid(16);
                } else {
                    storagePrefix = "Unknown:"; displayPath = path;
                }

                ParsedEventItem ev;
                ev.lineNumber = lineNumber;
                ev.triggerCount = m_triggerCount;
                ev.eventCategory = "main";
                ev.display = QString("%1 | %2# Media UUID:%3 Type:%4 %5%6")
                                 .arg(lineNumber, 6, 10, QChar(' '))
                                 .arg(m_triggerCount)
                                 .arg(uuid)
                                 .arg(typeToString(type))
                                 .arg(storagePrefix)
                                 .arg(displayPath);
                result.events << ev;
            }
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
                    if (l.contains("NOTIFY_TURTLE_FLIP", Qt::CaseInsensitive))
                        m_triggerCount++;
                    ParsedEventItem ev;
                    ev.lineNumber = lineNumber - 1;
                    ev.triggerCount = m_triggerCount;
                    ev.eventCategory = "main";
                    ev.display = QString("%1 | %2# %3")
                                     .arg(lineNumber - 1, 6, 10, QChar(' '))
                                     .arg(m_triggerCount)
                                     .arg(l.trimmed());
                    result.events << ev;
                }
            }
        }
        return;
    }

    // ==================== manual_control_takeover_request ====================
    if (line.contains("manual_control_takeover_request", Qt::CaseInsensitive)) {
        ParsedEventItem ev;
        ev.lineNumber = lineNumber;
        ev.triggerCount = m_triggerCount;
        ev.eventCategory = "main";
        ev.display = QString("%1 | %2# 模式:%3 -> MANUAL")
                         .arg(lineNumber, 6, 10, QChar(' '))
                         .arg(m_triggerCount)
                         .arg(m_modeText);
        result.events << ev;
        return;
    }

    // ==================== recv action from camera ====================
    if (line.contains("recv action from camera", Qt::CaseInsensitive)) {
        auto mCam = reCameraAction.match(line);
        if (mCam.hasMatch()) {
            int actionValue = mCam.captured(1).toInt();
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
            ParsedEventItem ev;
            ev.lineNumber = lineNumber;
            ev.triggerCount = m_triggerCount;
            ev.eventCategory = "main";
            ev.display = QString("%1 | %2# CS ACTION -> %3")
                             .arg(lineNumber, 6, 10, QChar(' '))
                             .arg(m_triggerCount)
                             .arg(actionName);
            result.events << ev;
            return;
        }
    }

    // ==================== Camera 温度 ====================
    if (line.contains("camera temp", Qt::CaseInsensitive)) {
        QDateTime ts;
        auto mTs = reCameraTs.match(line);
        if (mTs.hasMatch()) {
            ts = QDateTime::fromString(mTs.captured(2), "yyyy-MM-dd HH:mm:ss");
            double tsDouble = mTs.captured(1).toDouble();
            int msecs = static_cast<int>((tsDouble - static_cast<int>(tsDouble)) * 1000);
            ts = ts.addMSecs(msecs);
        }

        int tempVal = 0;
        bool found = false;
        auto mBoth = reCameraTempBoth.match(line);
        if (mBoth.hasMatch()) {
            tempVal = mBoth.captured(2).toInt();
            found = true;
        } else {
            auto mSingle = reCameraTemp.match(line);
            if (mSingle.hasMatch()) {
                tempVal = mSingle.captured(1).toInt();
                found = true;
            }
        }

        if (found && tempVal != -128)
            result.cameraTemps.push_back({ts, tempVal});
    }

    // ==================== 子解析函数 ====================
    parseCameraStatus(lineNumber, line, result);
    parseStatusHeartbeat(lineNumber, line, result);
    parseStatusBattery(lineNumber, line, result);
    parseStatusSocTemp(lineNumber, line, result);
}

// ============================================================
// parseCameraStatus — 使用 QRegularExpression 替代 QRegExp
// ============================================================
void LogParserWorker::parseCameraStatus(int lineNumber, const QString &line, ParseResult &result)
{
    // 预检：只有包含该关键字才跑正则
    if (!line.contains("The last five bits of"))
        return;

    static const QRegularExpression rx(R"(The last five bits of\s*([01]{5}))");
    auto m = rx.match(line);
    if (!m.hasMatch()) return;

    bool ok = false;
    int last_five_bits = m.captured(1).toInt(&ok, 2);
    if (!ok) return;

    bool status    = (last_five_bits >> 0) & 1;
    bool stream    = (last_five_bits >> 1) & 1;
    bool preview   = (last_five_bits >> 2) & 1;
    bool recording = (last_five_bits >> 3) & 1;
    bool snapshot  = (last_five_bits >> 4) & 1;

    auto bitToStr = [](bool b) { return b ? "ON" : "OFF"; };

    QString note;
    if (!status) {
        note = "close";
    } else if (!stream && !preview && !recording && !snapshot) {
        note = "init";
    } else {
        QStringList activeStates;
        if (stream)    activeStates << "stream";
        if (preview)   activeStates << "preview";
        if (recording) activeStates << "recording";
        if (snapshot)  activeStates << "snapshot";
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

    // 提取时间戳
    static const QRegularExpression rxTs(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    QDateTime evTs;
    auto mTs = rxTs.match(line);
    if (mTs.hasMatch()) {
        evTs = QDateTime::fromString(mTs.captured(2), "yyyy-MM-dd HH:mm:ss");
        double tsDouble = mTs.captured(1).toDouble();
        int msecs = static_cast<int>((tsDouble - static_cast<int>(tsDouble)) * 1000);
        evTs = evTs.addMSecs(msecs);
    }

    ParsedEventItem ev;
    ev.lineNumber = lineNumber;
    ev.triggerCount = m_triggerCount;
    ev.eventCategory = "camera";
    ev.display = display;
    ev.timestamp = evTs;
    result.events << ev;
}

// ============================================================
// parseStatusHeartbeat
// ============================================================
void LogParserWorker::parseStatusHeartbeat(int lineNumber, const QString &line, ParseResult &result)
{
    if (!line.contains("APP heart timeout delay", Qt::CaseInsensitive))
        return;

    // 提取时间戳
    static const QRegularExpression rxTs(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    QDateTime evTs;
    auto mTs = rxTs.match(line);
    if (mTs.hasMatch()) {
        evTs = QDateTime::fromString(mTs.captured(2), "yyyy-MM-dd HH:mm:ss");
        double tsDouble = mTs.captured(1).toDouble();
        int msecs = static_cast<int>((tsDouble - static_cast<int>(tsDouble)) * 1000);
        evTs = evTs.addMSecs(msecs);
    }

    ParsedEventItem ev;
    ev.lineNumber = lineNumber;
    ev.triggerCount = m_triggerCount;
    ev.eventCategory = "heartbeat";
    ev.display = QString("%1 | App/RC 连接超时10s").arg(lineNumber);
    ev.timestamp = evTs;
    result.events << ev;
}

// ============================================================
// parseStatusBattery — 使用 QRegularExpression 替代 QRegExp
// ============================================================
void LogParserWorker::parseStatusBattery(int lineNumber, const QString &line, ParseResult &result)
{
    Q_UNUSED(lineNumber)
    if (!line.contains("battery info", Qt::CaseInsensitive))
        return;

    static const QRegularExpression rxTimestamp(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression reKV(R"(\b(\w+)\s*[:=]\s*([^\s,]+))");

    BatteryTimeInfo timeinfo;
    auto mTs = rxTimestamp.match(line);
    if (mTs.hasMatch()) {
        QString dtStr = mTs.captured(2); // "2026-04-03 17:42:38"
        QString tsStr = mTs.captured(1); // "7635.014"
        
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

    // 解析 battery info: 字段
    int idx = line.indexOf("battery info:");
    if (idx != -1) {
        QString data = line.mid(idx + 13).trimmed(); // len("battery info:") == 13
        auto it = reKV.globalMatch(data);
        while (it.hasNext()) {
            auto mm = it.next();
            QString key = mm.captured(1).trimmed();
            QString val = mm.captured(2).trimmed();
            if      (key == "soc")                  timeinfo.info.soc = val.toInt();
            else if (key == "current")              timeinfo.info.current = val.toInt();
            else if (key == "voltage")              timeinfo.info.voltage = val.toInt();
            else if (key == "temp")                 timeinfo.info.temp = val.toDouble();
            else if (key == "is_abnormal")          timeinfo.info.is_abnormal = val.toInt();
            else if (key == "is_charging")          timeinfo.info.is_charging = val.toInt();
            else if (key == "heating")              timeinfo.info.heating = val.toInt();
            else if (key == "can_heat")             timeinfo.info.can_heat = val.toInt();
            else if (key == "battery_sn")           timeinfo.info.battery_sn = val;
            else if (key == "battery_cycles_count") timeinfo.info.battery_cycles_count = val.toInt();
            else if (key == "battery_health")       timeinfo.info.battery_health = val.toInt();
        }
    }

    result.batteryinfo.push_back(timeinfo);
}

// ============================================================
// parseStatusSocTemp — 使用 QRegularExpression 替代 QRegExp
// ============================================================
void LogParserWorker::parseStatusSocTemp(int lineNumber, const QString &line, ParseResult &result)
{
    Q_UNUSED(lineNumber)
    if (!line.contains("get soc max temp"))
        return;

    static const QRegularExpression rxTimestamp(
        R"(\[(\d+(?:\.\d+)?)\s+(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\])");
    static const QRegularExpression rxMax(R"(get soc max temp\s*[:=]\s*(\d+))");
    static const QRegularExpression rxCore(R"(core temp\s*[:=]\s*([0-9:]+))");

    SocTempInfo info;

    auto mTs = rxTimestamp.match(line);
    if (mTs.hasMatch()) {
        QString dtStr = mTs.captured(2); // "2026-04-03 17:42:38"
        QString tsStr = mTs.captured(1); // "7635.014"
        
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

    auto mMax = rxMax.match(line);
    if (mMax.hasMatch())
        info.maxTemp = mMax.captured(1).toInt();

    auto mCore = rxCore.match(line);
    if (mCore.hasMatch()) {
        for (const QString &t : mCore.captured(1).split(':'))
            info.coreTemps.append(t.toInt());
    }

    // 只保留有效数据点：timestamp 必须 valid，maxTemp 必须 > 0
    if (!info.timestamp.isValid() || info.maxTemp <= 0) return;
    result.soctmp.append(info);
}

// ============================================================
// parseTopTime — 使用 QRegularExpression
// ============================================================
QDateTime LogParserWorker::parseTopTime(const QString &line)
{
    static const QRegularExpression rx(R"(top - (\d{2}:\d{2}:\d{2}))");
    auto m = rx.match(line);
    if (m.hasMatch()) {
        QTime t = QTime::fromString(m.captured(1), "HH:mm:ss");
        return QDateTime(QDate::currentDate(), t);
    }
    return QDateTime();
}

// ============================================================
// parseTopFile — 使用 QRegularExpression 替代 QRegExp
// ============================================================
void LogParserWorker::parseTopFile(const QString &filePath, QVector<AllModuleUsage> &allusage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    static const QRegularExpression reSplit(R"(\s+)");

    QTextStream in(&file);
    AllModuleUsage usage;
    bool hasData = false;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("top -")) {
            if (hasData) {
                allusage.append(usage);
                usage = AllModuleUsage();
            }
            usage.timestamp = parseTopTime(line);
            hasData = true;
            continue;
        }

        QStringList parts = line.split(reSplit, Qt::SkipEmptyParts);
        if (parts.size() < 12) continue;

        QString moduleName = parts.last();
        double cpu = parts[8].toDouble();
        double mem = parts[9].toDouble();

        if      (moduleName == "camera_service")   usage.camera_service = {cpu, mem};
        else if (moduleName == "captain")          usage.captain = {cpu, mem};
        else if (moduleName == "fcs")              usage.fcs = {cpu, mem};
        else if (moduleName == "control_engine")   usage.control_engine = {cpu, mem};
        else if (moduleName == "drvf_msg_monito")  usage.drvf_msg_monito = {cpu, mem};
        else if (moduleName == "top")              usage.top = {cpu, mem};
        else if (moduleName == "vio_hover")        usage.vio_hover = {cpu, mem};
        else if (moduleName == "logd")             usage.logd = {cpu, mem};
        else if (moduleName == "exception_manag")  usage.exception_manag = {cpu, mem};
        else if (moduleName == "bt_service")       usage.bt_service = {cpu, mem};
        else if (moduleName == "battery_service")  usage.battery_service = {cpu, mem};
        else if (moduleName == "gimbal_service")   usage.gimbal_service = {cpu, mem};
        else if (moduleName == "kworker/u18:1-crm_workq-icp_message_q")
            usage.kworker_u18_icp_message_q = {cpu, mem};
        else if (moduleName == "logcat")           usage.logcat = {cpu, mem};
        else if (moduleName == "kworker/u19:1-kgsl-events")
            usage.kworker_u19_kgsl_events = {cpu, mem};
        else if (moduleName == "systemd")          usage.systemd = {cpu, mem};
        else if (moduleName == "kthreadd")         usage.kthreadd = {cpu, mem};
        else if (moduleName == "rcu_gp")           usage.rcu_gp = {cpu, mem};
        else if (moduleName == "rcu_par_gp")       usage.rcu_par_gp = {cpu, mem};
        else if (moduleName == "kworker/0:0-events") usage.kworker_0_events = {cpu, mem};
        else if (moduleName == "fpv_service")      usage.fpv_service = {cpu, mem};
    }

    if (hasData)
        allusage.append(usage);
}
