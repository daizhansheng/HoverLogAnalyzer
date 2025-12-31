#include "HLogParser.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

HLogParser::HLogParser() {
    // 匹配格式：[时间戳毫秒 日期时间]
    // 例如：[7513 2025-12-30 01:56:03]
    timestampPattern = QRegularExpression(R"(\[(\d+)\s+(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})\])");
    
    // 匹配格式：[级别|文件名:行号]
    // 例如：[I|Config.cpp:46]
    levelFilePattern = QRegularExpression(R"(\[([IDWEF])\|([^:]+):(\d+)\])");
}

HLogParser::~HLogParser() {
}

QList<HLogEntry> HLogParser::parse(const QByteArray &data) {
    QList<HLogEntry> entries;
    
    // 将二进制数据转换为文本 - 使用简单直接的方法
    QString text;
    text.reserve(data.size());
    
    // 直接遍历字节，保留所有可能的文本字符
    for (int i = 0; i < data.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(data[i]);
        
        // 保留换行、制表符、回车符
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            text.append(QChar(ch));
        }
        // 保留所有 >= 32 的字符（包括 ASCII 和扩展字符）
        else if (ch >= 32) {
            text.append(QChar(ch));
        }
        // null 字符：如果前后都是可打印字符，转换为空格
        else if (ch == 0) {
            if (i > 0 && i < data.size() - 1) {
                unsigned char prev = static_cast<unsigned char>(data[i-1]);
                unsigned char next = static_cast<unsigned char>(data[i+1]);
                if (prev >= 32 && next >= 32) {
                    text.append(' ');
                }
            }
        }
        // 其他控制字符忽略
    }
    
    // 按行分割，保留空行用于多行条目的识别
    QStringList lines = text.split('\n');
    
    QString currentEntry;
    bool inMultiLineEntry = false;
    
    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        
        // 检查是否是新的日志条目开始（包含时间戳和级别信息）
        // 完整格式：[时间戳 日期时间] [级别|文件名:行号]: 内容
        QRegularExpressionMatch timestampMatch = timestampPattern.match(trimmedLine);
        bool hasLevelInfo = levelFilePattern.match(trimmedLine).hasMatch();
        
        if (timestampMatch.hasMatch() || (hasLevelInfo && trimmedLine.contains(':'))) {
            // 如果之前有未保存的条目，先保存
            if (!currentEntry.isEmpty()) {
                HLogEntry entry;
                if (parseLogLine(currentEntry, entry)) {
                    entries.append(entry);
                }
            }
            // 开始新的条目
            currentEntry = trimmedLine;
            inMultiLineEntry = true;
        } else if (inMultiLineEntry && !trimmedLine.isEmpty()) {
            // 继续多行条目（例如 relations { ... } 块）
            // 检查是否是新的条目开始（以 [ 开头且包含时间戳）
            if (trimmedLine.startsWith('[') && timestampPattern.match(trimmedLine).hasMatch()) {
                // 这是新条目，先保存之前的
                if (!currentEntry.isEmpty()) {
                    HLogEntry entry;
                    if (parseLogLine(currentEntry, entry)) {
                        entries.append(entry);
                    }
                }
                currentEntry = trimmedLine;
            } else {
                // 继续当前条目
                currentEntry += "\n" + trimmedLine;
            }
        } else if (!trimmedLine.isEmpty()) {
            // 没有时间戳的行，作为独立条目
            if (!currentEntry.isEmpty()) {
                HLogEntry entry;
                if (parseLogLine(currentEntry, entry)) {
                    entries.append(entry);
                }
            }
            currentEntry = trimmedLine;
            inMultiLineEntry = false;
        }
    }
    
    // 保存最后一个条目
    if (!currentEntry.isEmpty()) {
        HLogEntry entry;
        if (parseLogLine(currentEntry, entry)) {
            entries.append(entry);
        }
    }
    
    // 按时间戳排序
    std::sort(entries.begin(), entries.end());
    
    return entries;
}

bool HLogParser::parseLogLine(const QString &line, HLogEntry &entry) {
    // 直接保存原始行，不做任何修改
    entry.fullText = line;
    
    // 提取时间戳（用于排序）
    // 匹配第一个时间戳（通常是日志条目的时间戳）
    QRegularExpressionMatchIterator timestampIterator = timestampPattern.globalMatch(line);
    if (timestampIterator.hasNext()) {
        QRegularExpressionMatch timestampMatch = timestampIterator.next();
        bool ok;
        int64_t timestampMs = timestampMatch.captured(1).toLongLong(&ok);
        if (ok) {
            // 只使用小于 10^10 的时间戳（避免误匹配 Unix 时间戳）
            // 正常的时间戳应该是毫秒数，通常小于 10^10
            if (timestampMs < 10000000000LL) {
                entry.timestampValue = timestampMs;
                entry.hasTimestamp = true;
                
                // 解析日期时间
                QString dateTimeStr = timestampMatch.captured(2);
                entry.timestamp = QDateTime::fromString(dateTimeStr, "yyyy-MM-dd HH:mm:ss");
                if (!entry.timestamp.isValid()) {
                    // 如果解析失败，尝试其他格式
                    entry.timestamp = QDateTime::fromString(dateTimeStr, "yyyy-MM-dd hh:mm:ss");
                }
            }
        }
    }
    
    // 提取级别、文件名和行号
    QRegularExpressionMatch levelMatch = levelFilePattern.match(line);
    if (levelMatch.hasMatch()) {
        entry.level = levelMatch.captured(1);
        entry.file = levelMatch.captured(2);
        bool ok;
        entry.line = levelMatch.captured(3).toInt(&ok);
        if (!ok) {
            entry.line = 0;
        }
    }
    
    return true;
}

QList<HLogEntry> HLogParser::parseFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开文件:" << filePath;
        return QList<HLogEntry>();
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    return parse(data);
}

QString HLogParser::entriesToText(const QList<HLogEntry> &entries) {
    QString result;
    result.reserve(entries.size() * 100); // 预分配空间
    
    for (const HLogEntry &entry : entries) {
        result += entry.fullText + "\n";
    }
    
    return result;
}

