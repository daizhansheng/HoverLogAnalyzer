#ifndef HLOGPARSER_H
#define HLOGPARSER_H

#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QRegularExpression>

// 解析后的日志条目
struct HLogEntry {
    QString fullText;        // 完整的日志文本（包含原始格式）
    QDateTime timestamp;     // 时间戳
    bool hasTimestamp;       // 是否有有效时间戳
    int64_t timestampValue;  // 时间戳数值（毫秒，用于排序）
    QString level;           // 日志级别：I, D, W, E, F
    QString module;          // 模块名
    QString file;            // 文件名
    int line;                // 行号
    
    HLogEntry() : hasTimestamp(false), timestampValue(0), line(0) {}
    
    bool operator<(const HLogEntry &other) const {
        // 优先按时间戳排序
        if (hasTimestamp && other.hasTimestamp) {
            return timestampValue < other.timestampValue;
        }
        if (hasTimestamp) return true;
        if (other.hasTimestamp) return false;
        return fullText < other.fullText;
    }
};

// HLog 解析器
class HLogParser {
public:
    HLogParser();
    ~HLogParser();

    // 从二进制数据解析日志
    QList<HLogEntry> parse(const QByteArray &data);

    // 从文件路径解析日志
    QList<HLogEntry> parseFromFile(const QString &filePath);

    // 将解析结果转换为文本格式（用于显示）
    QString entriesToText(const QList<HLogEntry> &entries);

private:
    // 从文本行中提取日志信息
    bool parseLogLine(const QString &line, HLogEntry &entry);
    
    // 正则表达式模式
    QRegularExpression timestampPattern;  // 匹配 [时间戳 日期时间]
    QRegularExpression levelFilePattern; // 匹配 [级别|文件名:行号]
};

#endif // HLOGPARSER_H








