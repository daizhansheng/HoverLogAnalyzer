#ifndef LOGPARSERWORKER_H
#define LOGPARSERWORKER_H

#include <QObject>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include "BatteryChartWidget.h"
#include "SocTempChartWidget.h"
#include "CameraTempChartWidget.h"
#include "ModuleUsageChart.h"

// ============================================================
// 解析结果聚合结构体（线程安全地从 worker 传回主线程）
// ============================================================
struct ParsedEventItem {
    int lineNumber;
    QString display;
    int triggerCount;      // 用于 eventList 着色
    QString eventCategory; // "main" | "camera" | "heartbeat"
    QDateTime timestamp;   // 事件时间戳（供 timeline 使用）
};

struct ParseResult {
    QStringList allLogLines;       // 全部日志行（原始，不含行号前缀）。GUI 用 join('\n') 一次性灌入 setPlainText，避免重复存储。
    QList<ParsedEventItem> events; // 所有类型事件
    QVector<BatteryTimeInfo> batteryinfo;
    QVector<SocTempInfo>     soctmp;
    QVector<CameraTempSample> cameraTemps;
    int triggerCount = 0;
    int flightCount  = 0;
    QString sn;
    QString version;
    QString imageVer;
    QString ipkVer;
    QString hwid;
};

Q_DECLARE_METATYPE(ParseResult)

// ============================================================
// LogParserWorker：在独立 QThread 中执行所有解析工作
// ============================================================
class LogParserWorker : public QObject
{
    Q_OBJECT
public:
    explicit LogParserWorker(QObject *parent = nullptr);

    // 调用方设置待解析文件列表，然后 emit startParsing()
    void setFiles(const QStringList &filePaths);
    // 设置版本号（影响起飞来源解析）
    void setVersion(const QString &version);

signals:
    // 解析进度（0-100）
    void progressChanged(int percent, const QString &statusText);
    // 解析完成
    void parseFinished(ParseResult result);
    // 解析出错
    void parseError(const QString &message);

public slots:
    // 启动解析（由 QThread::started 或外部 emit 触发）
    void run();

private:
    // ------- 单行解析核心（与原 analyzeLogLine 等价）-------
    void analyzeLogLine(const QString &line,
                        int &lineNumber,
                        QDateTime &currentTakeoffTime,
                        bool &inRecvException,
                        QStringList &recvExceptionLines,
                        ParseResult &result);

    void parseCameraStatus(int lineNumber, const QString &line, ParseResult &result);
    void parseStatusHeartbeat(int lineNumber, const QString &line, ParseResult &result);
    void parseStatusBattery(int lineNumber, const QString &line, ParseResult &result);
    void parseStatusSocTemp(int lineNumber, const QString &line, ParseResult &result);

    // ------- top 文件解析 -------
    void parseTopFile(const QString &filePath, QVector<AllModuleUsage> &allusage);
    QDateTime parseTopTime(const QString &line);

    // ------- 文件解析入口 -------
    bool analyzeSourceFile(const QString &filePath,
                           int &lineNumber,
                           QDateTime &currentTakeoffTime,
                           bool &inRecvException,
                           QStringList &recvExceptionLines,
                           ParseResult &result);

private:
    QStringList m_filePaths;
    QString     m_version;
    int         m_triggerCount = 0;
    QString     m_modeText;
};

#endif // LOGPARSERWORKER_H
