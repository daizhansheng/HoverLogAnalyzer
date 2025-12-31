#include "HLogBinaryParser.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QTextStream>
#include <QRegularExpression>

HLogBinaryParser::HLogBinaryParser()
    : hlog_version_(0)
    , hlog_flags_(0)
    , timezone_offset_s_(0)
    , diff_from_utc_to_monotonic_ms_(0)
    , magic_number_parsed_(false)
{
}

HLogBinaryParser::~HLogBinaryParser()
{
}

QList<HLogEntry> HLogBinaryParser::parseFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开 .hlog 文件:" << filePath;
        return QList<HLogEntry>();
    }

    return parse(&file);
}

QList<HLogEntry> HLogBinaryParser::parse(QIODevice *device)
{
    QList<HLogEntry> entries;
    
    // 重置状态
    hlog_version_ = 0;
    hlog_flags_ = 0;
    timezone_offset_s_ = 0;
    diff_from_utc_to_monotonic_ms_ = 0;
    entry_meta_map_.clear();
    magic_number_parsed_ = false;

    RawEntry entry;
    uint64_t currentWalltime = 0;

    // 解析所有条目
    while (parseNextRawEntry(device, entry)) {
        if (entry.type_id == ENTRY_TYPE_ID_MAGIC_NUMBER) {
            if (!handleMagicNumber(entry, hlog_version_, hlog_flags_)) {
                qWarning() << "解析 magic number 失败";
                continue;
            }
            magic_number_parsed_ = true;
        }
        else if (entry.type_id == ENTRY_TYPE_ID_MODULE_INIT) {
            QString moduleName;
            if (handleModuleInit(entry, moduleName)) {
                // 可以记录模块名，但不需要创建日志条目
            }
        }
        else if (entry.type_id == ENTRY_TYPE_ID_TIME_SYNCHRONIZATION) {
            int32_t timezoneOffset;
            uint64_t utcWalltime;
            if (handleTimeSync(entry, timezoneOffset, utcWalltime)) {
                timezone_offset_s_ = timezoneOffset;
                diff_from_utc_to_monotonic_ms_ = utcWalltime - entry.monotonic_raw_timestamp_ms;
            }
        }
        else if (entry.type_id == ENTRY_TYPE_ID_META) {
            EntryMeta meta;
            if (handleEntryMeta(entry, meta)) {
                entry_meta_map_[meta.type_id] = meta;
            }
        }
        else {
            // 普通日志条目
            if (entry_meta_map_.contains(entry.type_id)) {
                uint64_t walltime = entry.monotonic_raw_timestamp_ms + diff_from_utc_to_monotonic_ms_ 
                                    + timezone_offset_s_ * 1000;
                
                HLogEntry logEntry;
                if (handleLogEntry(entry, entry_meta_map_[entry.type_id], walltime, logEntry)) {
                    entries.append(logEntry);
                }
            }
        }
    }

    // 按时间戳排序
    std::sort(entries.begin(), entries.end());

    return entries;
}

bool HLogBinaryParser::parseNextRawEntry(QIODevice *device, RawEntry &entry)
{
    // 读取 type_id (4 bytes)
    QByteArray typeIdData = device->read(4);
    if (typeIdData.size() != 4) {
        return false;
    }
    entry.type_id = *reinterpret_cast<const uint32_t*>(typeIdData.constData());

    // 读取 size (4 bytes)
    QByteArray sizeData = device->read(4);
    if (sizeData.size() != 4) {
        return false;
    }
    entry.size = *reinterpret_cast<const uint32_t*>(sizeData.constData());

    // 读取 monotonic_raw_timestamp_ms (8 bytes)
    QByteArray timestampData = device->read(8);
    if (timestampData.size() != 8) {
        return false;
    }
    entry.monotonic_raw_timestamp_ms = *reinterpret_cast<const uint64_t*>(timestampData.constData());

    // 读取 data
    entry.data = device->read(entry.size);
    if (entry.data.size() != static_cast<int>(entry.size)) {
        return false;
    }

    return true;
}

bool HLogBinaryParser::handleMagicNumber(const RawEntry &entry, uint8_t &version, uint8_t &flags)
{
    if (entry.size != 2) {
        return false;
    }
    version = static_cast<uint8_t>(entry.data[0]);
    flags = static_cast<uint8_t>(entry.data[1]);
    return true;
}

bool HLogBinaryParser::handleModuleInit(const RawEntry &entry, QString &moduleName)
{
    if (entry.data.isEmpty()) {
        return false;
    }
    moduleName = QString::fromUtf8(entry.data.constData(), entry.data.size());
    return true;
}

bool HLogBinaryParser::handleTimeSync(const RawEntry &entry, int32_t &timezoneOffset, uint64_t &utcWalltime)
{
    if (entry.size < static_cast<uint32_t>(sizeof(uint32_t) + sizeof(int32_t) + sizeof(uint64_t))) {
        return false;
    }

    const char *data = entry.data.constData();
    uint32_t sourceId = *reinterpret_cast<const uint32_t*>(data);
    Q_UNUSED(sourceId);
    
    timezoneOffset = *reinterpret_cast<const int32_t*>(data + sizeof(uint32_t));
    utcWalltime = *reinterpret_cast<const uint64_t*>(data + sizeof(uint32_t) + sizeof(int32_t));
    
    return true;
}

bool HLogBinaryParser::handleEntryMeta(const RawEntry &entry, EntryMeta &meta)
{
    if (entry.size < 7) {
        return false;
    }

    const uint8_t *data = reinterpret_cast<const uint8_t*>(entry.data.constData());
    
    meta.type_id = *reinterpret_cast<const uint32_t*>(data);
    meta.level = data[4];
    meta.num_args = data[5];
    meta.num_params = data[6];

    if (entry.size < static_cast<uint32_t>(7 + meta.num_params + meta.num_args + 4)) {
        return false;
    }

    // 读取 param_types
    meta.param_types = QByteArray(reinterpret_cast<const char*>(data + 7), meta.num_params);

    // 读取 argument_infos
    meta.argument_infos.clear();
    for (int i = 0; i < meta.num_args; ++i) {
        meta.argument_infos.append(data[7 + meta.num_params + i]);
    }

    // 读取 linenum
    int offset = 7 + meta.num_params + meta.num_args;
    meta.linenum = *reinterpret_cast<const uint32_t*>(data + offset);
    offset += 4;

    // 读取 filename (以 null 结尾的字符串)
    const char *filenameStart = reinterpret_cast<const char*>(data + offset);
    int filenameLen = qstrlen(filenameStart);
    meta.filename = QString::fromUtf8(filenameStart, filenameLen);
    offset += filenameLen + 1;

    // 读取 tags (以 null 结尾的字符串)
    if (offset < entry.size) {
        const char *tagsStart = reinterpret_cast<const char*>(data + offset);
        int tagsLen = qstrlen(tagsStart);
        meta.tags = QString::fromUtf8(tagsStart, tagsLen);
        offset += tagsLen + 1;
    }

    // 读取 format (以 null 结尾的字符串)
    if (offset < entry.size) {
        const char *formatStart = reinterpret_cast<const char*>(data + offset);
        int formatLen = qstrlen(formatStart);
        meta.format = QString::fromUtf8(formatStart, formatLen).trimmed();
    }

    return true;
}

bool HLogBinaryParser::handleLogEntry(const RawEntry &entry, const EntryMeta &meta, 
                                      uint64_t walltime, HLogEntry &logEntry)
{
    logEntry.fullText = formatLogEntry(meta, entry, walltime);
    logEntry.timestampValue = walltime;
    logEntry.hasTimestamp = true;
    logEntry.timestamp = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(walltime));
    logEntry.level = levelToChar(meta.level);
    logEntry.file = meta.filename;
    logEntry.line = static_cast<int>(meta.linenum);

    return true;
}

QString HLogBinaryParser::formatLogEntry(const EntryMeta &meta, const RawEntry &entry, uint64_t walltime)
{
    QString result;
    QTextStream stream(&result);

    // 格式化时间戳: [时间戳毫秒 日期时间]
    QString timeStr = formatTimestamp(walltime);
    stream << "[" << walltime << " " << timeStr << "] ";

    // 格式化级别和文件信息: [级别|文件名:行号]
    // 从完整路径中提取文件名
    QString fileName = meta.filename;
    int lastSlash = fileName.lastIndexOf('/');
    if (lastSlash >= 0) {
        fileName = fileName.mid(lastSlash + 1);
    }
    int lastBackslash = fileName.lastIndexOf('\\');
    if (lastBackslash >= 0) {
        fileName = fileName.mid(lastBackslash + 1);
    }
    stream << "[" << levelToChar(meta.level) << "|" << fileName << ":" << meta.linenum << "]: ";

    // 格式化日志内容
    QString content;
    QTextStream contentStream(&content);
    
    if (meta.format.isEmpty()) {
        // 如果没有格式字符串，直接显示原始数据（十六进制）
        for (int i = 0; i < entry.data.size(); ++i) {
            contentStream << QString::asprintf("%02X ", static_cast<unsigned char>(entry.data[i]));
        }
    } else {
        // 解析格式字符串并格式化参数
        const char *dataPtr = entry.data.constData();
        int argIndex = 0;
        QString format = meta.format;

        // 简单的格式字符串解析（支持基本的 %s, %d, %f 等）
        int pos = 0;
        while (pos < format.length()) {
            if (format[pos] == '%' && pos + 1 < format.length()) {
                if (format[pos + 1] == '%') {
                    // 转义的 %
                    contentStream << '%';
                    pos += 2;
                } else {
                    // 格式说明符
                    QString formatSpec;
                    int specStart = pos;
                    pos++;
                    while (pos < format.length() && 
                           (format[pos].isLetterOrNumber() || format[pos] == '.' || 
                            format[pos] == '-' || format[pos] == '+' || format[pos] == ' ' ||
                            format[pos] == '#' || format[pos] == '0' || format[pos] == '*' ||
                            format[pos] == 'h' || format[pos] == 'l' || format[pos] == 'L')) {
                        pos++;
                    }
                    if (pos < format.length()) {
                        formatSpec = format.mid(specStart, pos - specStart + 1);
                        pos++;
                    } else {
                        formatSpec = format.mid(specStart);
                    }

                    QString value = formatValue(meta, entry, argIndex, dataPtr, formatSpec);
                    contentStream << value;
                }
            } else {
                contentStream << format[pos];
                pos++;
            }
        }
    }

    // 处理多行内容：如果内容包含换行，在后续行前添加8个空格
    QStringList lines = content.split('\n');
    if (lines.size() > 1) {
        // 多行内容
        for (int i = 0; i < lines.size(); ++i) {
            if (i == 0) {
                // 第一行直接添加
                stream << lines[i];
            } else {
                // 后续行添加换行符和8个空格缩进
                stream << "\n        " << lines[i];  // 8个空格
            }
        }
    } else {
        // 单行内容，直接添加
        stream << content;
    }

    return result;
}

QString HLogBinaryParser::formatValue(const EntryMeta &meta, const RawEntry &entry, 
                                     int &argIndex, const char *&dataPtr, const QString &formatSpec)
{
    Q_UNUSED(formatSpec);
    
    if (argIndex >= meta.num_args) {
        return QString("<?>");
    }

    // 边界检查
    const char *dataStart = entry.data.constData();
    const char *dataEnd = dataStart + entry.data.size();
    if (dataPtr >= dataEnd) {
        return QString("<?>");
    }

    quint8 argInfo = meta.argument_infos[argIndex];
    argIndex++;

    uint8_t argType = (argInfo >> 4) & 0x0F;
    uint8_t argSizeBytes = argInfo & 0x0F;

    QString result;

    switch (argType) {
    case ARGUMENT_TYPE_UNSIGN_INT: {
        uint64_t value = 0;
        if (argSizeBytes == 1 && dataPtr + 1 <= dataEnd) {
            value = *reinterpret_cast<const uint8_t*>(dataPtr);
            dataPtr += 1;
        } else if (argSizeBytes == 2 && dataPtr + 2 <= dataEnd) {
            value = *reinterpret_cast<const uint16_t*>(dataPtr);
            dataPtr += 2;
        } else if (argSizeBytes == 4 && dataPtr + 4 <= dataEnd) {
            value = *reinterpret_cast<const uint32_t*>(dataPtr);
            dataPtr += 4;
        } else if (argSizeBytes == 8 && dataPtr + 8 <= dataEnd) {
            value = *reinterpret_cast<const uint64_t*>(dataPtr);
            dataPtr += 8;
        } else {
            return QString("<?>");
        }
        result = QString::number(value);
        break;
    }
    case ARGUMENT_TYPE_SIGN_INT: {
        int64_t value = 0;
        if (argSizeBytes == 1 && dataPtr + 1 <= dataEnd) {
            value = *reinterpret_cast<const int8_t*>(dataPtr);
            dataPtr += 1;
        } else if (argSizeBytes == 2 && dataPtr + 2 <= dataEnd) {
            value = *reinterpret_cast<const int16_t*>(dataPtr);
            dataPtr += 2;
        } else if (argSizeBytes == 4 && dataPtr + 4 <= dataEnd) {
            value = *reinterpret_cast<const int32_t*>(dataPtr);
            dataPtr += 4;
        } else if (argSizeBytes == 8 && dataPtr + 8 <= dataEnd) {
            value = *reinterpret_cast<const int64_t*>(dataPtr);
            dataPtr += 8;
        } else {
            return QString("<?>");
        }
        result = QString::number(value);
        break;
    }
    case ARGUMENT_TYPE_FLOAT: {
        double value = 0.0;
        if (argSizeBytes == 4 && dataPtr + 4 <= dataEnd) {
            value = *reinterpret_cast<const float*>(dataPtr);
            dataPtr += 4;
        } else if (argSizeBytes == 8 && dataPtr + 8 <= dataEnd) {
            value = *reinterpret_cast<const double*>(dataPtr);
            dataPtr += 8;
        } else {
            return QString("<?>");
        }
        result = QString::number(value, 'f', 6);
        break;
    }
    case ARGUMENT_TYPE_STRING: {
        // 字符串格式：先读取长度（uint32_t），然后是字符串数据
        if (dataPtr + sizeof(uint32_t) > dataEnd) {
            return QString("<?>");
        }
        uint32_t strLen = *reinterpret_cast<const uint32_t*>(dataPtr);
        dataPtr += sizeof(uint32_t);
        if (dataPtr + strLen > dataEnd) {
            return QString("<?>");
        }
        result = QString::fromUtf8(dataPtr, strLen);
        dataPtr += strLen;
        break;
    }
    case ARGUMENT_TYPE_POINTER: {
        void *ptr = nullptr;
        if (argSizeBytes == 4 && dataPtr + 4 <= dataEnd) {
            ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(dataPtr)));
            dataPtr += 4;
        } else if (argSizeBytes == 8 && dataPtr + 8 <= dataEnd) {
            ptr = reinterpret_cast<void*>(static_cast<uintptr_t>(*reinterpret_cast<const uint64_t*>(dataPtr)));
            dataPtr += 8;
        } else {
            return QString("<?>");
        }
        result = QString::asprintf("%p", ptr);
        break;
    }
    default:
        result = QString("<?>");
        break;
    }

    return result;
}

int HLogBinaryParser::getArgumentSize(quint8 argInfo, const char *&dataPtr)
{
    Q_UNUSED(dataPtr);
    uint8_t argType = (argInfo >> 4) & 0x0F;
    uint8_t argSizeBytes = argInfo & 0x0F;

    if (argType == ARGUMENT_TYPE_STRING) {
        // 字符串：长度字段（4字节）+ 字符串数据（长度在 formatValue 中读取）
        return sizeof(uint32_t);  // 最小大小，实际大小在 formatValue 中计算
    }

    // 其他类型直接返回大小
    if (argSizeBytes == 0) return 1;
    return argSizeBytes;
}

QString HLogBinaryParser::formatTimestamp(uint64_t walltimeMs)
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(walltimeMs));
    return dt.toString("yyyy-MM-dd HH:mm:ss");
}

QChar HLogBinaryParser::levelToChar(uint8_t level)
{
    switch (level) {
    case LOG_LEVEL_FATAL: return 'F';
    case LOG_LEVEL_ERROR: return 'E';
    case LOG_LEVEL_WARN: return 'W';
    case LOG_LEVEL_DEBUG: return 'D';
    case LOG_LEVEL_INFO: return 'I';
    default: return 'U';
    }
}

