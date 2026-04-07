#include "HLogBinaryParser.h"
#include <QFile>
#include <QDebug>
#include <QTextStream>

HLogBinaryParser::HLogBinaryParser()
    : hlog_version_(0)
    , hlog_flags_(0)
    , timezone_offset_s_(0)
    , diff_from_utc_to_monotonic_ms_(0)
    , magic_number_parsed_(false)
    , entry_layout_(EntryLayout::Unknown)
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
    struct PendingLogEntry {
        RawEntry entry;
        EntryMeta meta;
        uint64_t diffFromUtcToMonotonicMs = 0;
    };
    QList<PendingLogEntry> pendingEntries;

    // 重置状态
    hlog_version_ = 0;
    hlog_flags_ = 0;
    timezone_offset_s_ = 0;
    diff_from_utc_to_monotonic_ms_ = 0;
    entry_meta_map_.clear();
    magic_number_parsed_ = false;
    entry_layout_ = EntryLayout::Unknown;

    RawEntry entry;

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
                if (utcWalltime < 946684800000ULL && entry.has_utc_walltime_timestamp &&
                    entry.utc_walltime_timestamp_ms >= 946684800000ULL) {
                    diff_from_utc_to_monotonic_ms_ = entry.utc_walltime_timestamp_ms - entry.monotonic_raw_timestamp_ms;
                }
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
                pendingEntries.append({entry, entry_meta_map_[entry.type_id], diff_from_utc_to_monotonic_ms_});
            }
        }
    }

    static constexpr uint64_t kMinReasonableWalltimeMs = 946684800000ULL;
    for (const PendingLogEntry &pending : pendingEntries) {
        uint64_t walltime = 0;
        if (pending.entry.has_utc_walltime_timestamp &&
            pending.entry.utc_walltime_timestamp_ms >= kMinReasonableWalltimeMs) {
            walltime = pending.entry.utc_walltime_timestamp_ms;
        } else if (pending.diffFromUtcToMonotonicMs != 0) {
            walltime = pending.entry.monotonic_raw_timestamp_ms + pending.diffFromUtcToMonotonicMs;
        } else if (diff_from_utc_to_monotonic_ms_ != 0) {
            walltime = pending.entry.monotonic_raw_timestamp_ms + diff_from_utc_to_monotonic_ms_;
        } else {
            walltime = pending.entry.monotonic_raw_timestamp_ms;
        }

        HLogEntry logEntry;
        if (handleLogEntry(pending.entry, pending.meta,
                           pending.entry.monotonic_raw_timestamp_ms, walltime, logEntry)) {
            entries.append(logEntry);
        }
    }

    // 按时间戳排序
    std::sort(entries.begin(), entries.end());

    return entries;
}

bool HLogBinaryParser::parseNextRawEntry(QIODevice *device, RawEntry &entry)
{
    entry = RawEntry{};

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

    auto looksLikeMagicPayload = [](const QByteArray &bytes, int offset) -> bool {
        if (bytes.size() < offset + 2) {
            return false;
        }
        const uint8_t version = static_cast<uint8_t>(bytes[offset]);
        const uint8_t flags = static_cast<uint8_t>(bytes[offset + 1]);
        return version > 0 && version <= 8 && flags <= 0x3F;
    };

    if (entry_layout_ == EntryLayout::Unknown &&
        entry.type_id == ENTRY_TYPE_ID_MAGIC_NUMBER &&
        entry.size == 2) {
        const QByteArray sniff = device->peek(10);
        const bool legacyPayloadValid = looksLikeMagicPayload(sniff, 0);
        const bool walltimePayloadValid = looksLikeMagicPayload(sniff, 8);
        if (!legacyPayloadValid && walltimePayloadValid) {
            entry_layout_ = EntryLayout::WithWalltime;
        } else if (legacyPayloadValid && !walltimePayloadValid) {
            entry_layout_ = EntryLayout::Legacy;
        }
    }

    if (entry_layout_ != EntryLayout::Legacy && device->bytesAvailable() >= static_cast<qint64>(sizeof(uint64_t))) {
        const qint64 payloadPos = device->pos();
        QByteArray walltimeData = device->read(sizeof(uint64_t));
        if (walltimeData.size() == static_cast<int>(sizeof(uint64_t))) {
            const uint64_t candidateWalltime = *reinterpret_cast<const uint64_t*>(walltimeData.constData());
            const qint64 remainingAfterWalltime = device->bytesAvailable();
            const bool looksLikeWalltime = candidateWalltime > 1000000000000ULL;
            const bool hasEnoughPayload = remainingAfterWalltime >= static_cast<qint64>(entry.size);

            if ((entry_layout_ == EntryLayout::WithWalltime || looksLikeWalltime) && hasEnoughPayload) {
                entry_layout_ = EntryLayout::WithWalltime;
                entry.utc_walltime_timestamp_ms = candidateWalltime;
                entry.has_utc_walltime_timestamp = true;
            } else {
                if (entry_layout_ == EntryLayout::Unknown) {
                    entry_layout_ = EntryLayout::Legacy;
                }
                if (!device->seek(payloadPos)) {
                    return false;
                }
            }
        } else {
            if (!device->seek(payloadPos)) {
                return false;
            }
        }
    }

    if (entry_layout_ == EntryLayout::WithWalltime &&
        !entry.has_utc_walltime_timestamp &&
        diff_from_utc_to_monotonic_ms_ != 0) {
        entry.utc_walltime_timestamp_ms = entry.monotonic_raw_timestamp_ms + diff_from_utc_to_monotonic_ms_;
        entry.has_utc_walltime_timestamp = true;
    }

    // 读取 data
    entry.data = device->read(entry.size);
    if (entry.data.size() != static_cast<int>(entry.size)) {
        return false;
    }

    if (entry.has_utc_walltime_timestamp && entry.utc_walltime_timestamp_ms < 946684800000ULL) {
        const QByteArray futureBytes = device->peek(sizeof(uint64_t) + static_cast<int>(entry.size));
        for (int i = 0; i + static_cast<int>(sizeof(uint64_t)) <= futureBytes.size(); ++i) {
            const uint64_t candidate = *reinterpret_cast<const uint64_t*>(futureBytes.constData() + i);
            if (candidate >= 946684800000ULL) {
                entry.utc_walltime_timestamp_ms = candidate;
                break;
            }
        }
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
                                      uint64_t monotonicTimestampMs, uint64_t walltime, HLogEntry &logEntry)
{
    logEntry.fullText = formatLogEntry(meta, entry, monotonicTimestampMs, walltime);
    logEntry.timestampValue = walltime;
    logEntry.hasTimestamp = true;
    logEntry.timestamp = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(walltime));
    logEntry.level = levelToChar(meta.level);
    logEntry.file = meta.filename;
    logEntry.line = static_cast<int>(meta.linenum);

    return true;
}

QString HLogBinaryParser::formatLogEntry(const EntryMeta &meta, const RawEntry &entry,
                                         uint64_t monotonicTimestampMs, uint64_t walltime)
{
    QString result;
    QTextStream stream(&result);

    // 格式化时间戳: [时间戳毫秒 日期时间]
    QString timeStr = formatTimestamp(walltime);
    stream << "[" << monotonicTimestampMs << " " << timeStr << "] ";

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

    const QString content = formatContent(meta, entry);

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

QString HLogBinaryParser::formatContent(const EntryMeta &meta, const RawEntry &entry)
{
    if (meta.format.isEmpty()) {
        QString hex;
        QTextStream stream(&hex);
        for (int i = 0; i < entry.data.size(); ++i) {
            stream << QString::asprintf("%02X ", static_cast<unsigned char>(entry.data[i]));
        }
        return hex;
    }

    QString content;
    QTextStream contentStream(&content);
    const QString &format = meta.format;
    const char *dataPtr = entry.data.constData();
    const char *dataEnd = dataPtr + entry.data.size();
    int argIndex = 0;
    int pos = 0;

    while (pos < format.length()) {
        if (format[pos] != '%') {
            contentStream << format[pos++];
            continue;
        }
        if (pos + 1 < format.length() && format[pos + 1] == '%') {
            contentStream << '%';
            pos += 2;
            continue;
        }

        FormatSpec spec;
        const int specStart = pos;
        if (!parseNextFormatSpec(format, pos, spec)) {
            contentStream << '%';
            pos = specStart + 1;
            continue;
        }
        contentStream << formatValue(meta, entry, argIndex, dataPtr, spec.raw);
    }

    if (dataPtr < dataEnd) {
        qWarning() << "HLog entry still has unread payload bytes:" << (dataEnd - dataPtr) << meta.format;
    }

    return content;
}

bool HLogBinaryParser::parseNextFormatSpec(const QString &format, int &pos, FormatSpec &spec)
{
    if (pos >= format.length() || format[pos] != '%') {
        return false;
    }

    int scan = pos + 1;
    while (scan < format.length() && QStringLiteral("-+ #0'").contains(format[scan])) {
        ++scan;
    }

    if (scan < format.length() && format[scan] == '*') {
        spec.dynamicWidth = true;
        ++scan;
    } else {
        while (scan < format.length() && format[scan].isDigit()) {
            ++scan;
        }
    }

    if (scan < format.length() && format[scan] == '.') {
        ++scan;
        if (scan < format.length() && format[scan] == '*') {
            spec.dynamicPrecision = true;
            ++scan;
        } else {
            while (scan < format.length() && format[scan].isDigit()) {
                ++scan;
            }
        }
    }

    if (scan + 1 < format.length() &&
        ((format[scan] == 'h' && format[scan + 1] == 'h') ||
         (format[scan] == 'l' && format[scan + 1] == 'l'))) {
        scan += 2;
    } else if (scan < format.length() && QStringLiteral("hljztL").contains(format[scan])) {
        ++scan;
    }

    const QString conversions = QStringLiteral("cspdiuoxXfFeEgGaAn");
    if (scan >= format.length() || !conversions.contains(format[scan])) {
        return false;
    }

    spec.raw = format.mid(pos, scan - pos + 1);
    spec.conversion = format[scan];
    pos = scan + 1;
    return true;
}

int HLogBinaryParser::consumeDynamicInt(const EntryMeta &meta, const RawEntry &entry, int &argIndex,
                                        const char *&dataPtr, bool *ok)
{
    const QString value = formatValue(meta, entry, argIndex, dataPtr, "%d");
    bool localOk = false;
    const int intValue = value.toInt(&localOk);
    if (ok) {
        *ok = localOk;
    }
    return intValue;
}

void HLogBinaryParser::resolveArgumentInfo(quint8 argInfo,
                                          const QString &resolvedFormat,
                                          uint8_t &argType,
                                          uint8_t &argSizeBytes) const
{
    argType = (argInfo >> 4) & 0x0F;
    argSizeBytes = argInfo & 0x0F;
    const QChar conversion = resolvedFormat.isEmpty() ? QChar() : resolvedFormat.back();

    switch (conversion.toLatin1()) {
    case 'd':
    case 'i':
    case 'c':
        argType = ARGUMENT_TYPE_SIGN_INT;
        break;
    case 'u':
    case 'o':
    case 'x':
    case 'X':
        argType = ARGUMENT_TYPE_UNSIGN_INT;
        break;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
        argType = ARGUMENT_TYPE_FLOAT;
        break;
    case 's':
        argType = ARGUMENT_TYPE_STRING;
        break;
    case 'p':
        argType = ARGUMENT_TYPE_POINTER;
        break;
    default:
        break;
    }

    if (argSizeBytes == 0) {
        switch (conversion.toLatin1()) {
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            argType = ARGUMENT_TYPE_FLOAT;
            argSizeBytes = resolvedFormat.contains('L') ? 16 : 8;
            break;
        case 's':
            argType = ARGUMENT_TYPE_STRING;
            argSizeBytes = 0;
            break;
        case 'p':
            argType = ARGUMENT_TYPE_POINTER;
            argSizeBytes = sizeof(void*);
            break;
        case 'd':
        case 'i':
        case 'c':
            argType = ARGUMENT_TYPE_SIGN_INT;
            argSizeBytes = 1;
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            argType = ARGUMENT_TYPE_UNSIGN_INT;
            argSizeBytes = 1;
            break;
        default:
            argSizeBytes = 1;
            break;
        }
    }
}

QString HLogBinaryParser::applyResolvedFormat(const QString &resolvedFormat,
                                             uint8_t argType,
                                             uint8_t argSizeBytes,
                                             const char *&dataPtr,
                                             const char *dataEnd,
                                             bool *ok)
{
    if (ok) {
        *ok = true;
    }

    const QByteArray fmt = resolvedFormat.toUtf8();
    const QChar conversion = resolvedFormat.isEmpty() ? QChar() : resolvedFormat.back();
    auto fallbackIntegralString = [&](qulonglong value, bool signedValue) -> QString {
        switch (conversion.toLatin1()) {
        case 'd':
        case 'i':
        case 'c':
            return signedValue ? QString::number(static_cast<qlonglong>(value))
                               : QString::number(static_cast<qulonglong>(value));
        case 'u':
            return QString::number(static_cast<qulonglong>(value));
        case 'o':
            return QString::number(static_cast<qulonglong>(value), 8);
        case 'x':
            return QString::number(static_cast<qulonglong>(value), 16);
        case 'X':
            return QString::number(static_cast<qulonglong>(value), 16).toUpper();
        default:
            return signedValue ? QString::number(static_cast<qlonglong>(value))
                               : QString::number(static_cast<qulonglong>(value));
        }
    };
    switch (argType) {
    case ARGUMENT_TYPE_UNSIGN_INT: {
        qulonglong value = 0;
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
            if (ok) *ok = false;
            return QString("<?>");
        }
        if (argSizeBytes == 1 && !resolvedFormat.contains('h') && !resolvedFormat.contains('l') &&
            !resolvedFormat.contains('j') && !resolvedFormat.contains('z') && !resolvedFormat.contains('t')) {
            return fallbackIntegralString(value, false);
        }
        if (conversion == 'c') {
            return QString::asprintf(fmt.constData(), static_cast<unsigned int>(value));
        }
        if (resolvedFormat.contains("ll")) {
            return QString::asprintf(fmt.constData(), static_cast<unsigned long long>(value));
        }
        if (resolvedFormat.contains('z')) {
            return QString::asprintf(fmt.constData(), static_cast<size_t>(value));
        }
        if (resolvedFormat.contains('j')) {
            return QString::asprintf(fmt.constData(), static_cast<uintmax_t>(value));
        }
        if (resolvedFormat.contains('l')) {
            return QString::asprintf(fmt.constData(), static_cast<unsigned long>(value));
        }
        return QString::asprintf(fmt.constData(), static_cast<unsigned int>(value));
    }
    case ARGUMENT_TYPE_SIGN_INT: {
        qlonglong value = 0;
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
            if (ok) *ok = false;
            return QString("<?>");
        }
        if (argSizeBytes == 1 && !resolvedFormat.contains('h') && !resolvedFormat.contains('l') &&
            !resolvedFormat.contains('j') && !resolvedFormat.contains('z') && !resolvedFormat.contains('t')) {
            return fallbackIntegralString(static_cast<qulonglong>(value), true);
        }
        if (conversion == 'c') {
            return QString::asprintf(fmt.constData(), static_cast<int>(value));
        }
        if (resolvedFormat.contains("ll")) {
            return QString::asprintf(fmt.constData(), static_cast<long long>(value));
        }
        if (resolvedFormat.contains('z')) {
            return QString::asprintf(fmt.constData(), static_cast<qsizetype>(value));
        }
        if (resolvedFormat.contains('j')) {
            return QString::asprintf(fmt.constData(), static_cast<intmax_t>(value));
        }
        if (resolvedFormat.contains('t')) {
            return QString::asprintf(fmt.constData(), static_cast<ptrdiff_t>(value));
        }
        if (resolvedFormat.contains('l')) {
            return QString::asprintf(fmt.constData(), static_cast<long>(value));
        }
        return QString::asprintf(fmt.constData(), static_cast<int>(value));
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
            if (ok) *ok = false;
            return QString("<?>");
        }
        return QString::asprintf(fmt.constData(), value);
    }
    case ARGUMENT_TYPE_STRING: {
        if (dataPtr + static_cast<int>(sizeof(uint32_t)) > dataEnd) {
            if (ok) *ok = false;
            return QString("<?>");
        }
        const uint32_t strLen = *reinterpret_cast<const uint32_t*>(dataPtr);
        dataPtr += sizeof(uint32_t);
        if (dataPtr + strLen > dataEnd) {
            if (ok) *ok = false;
            return QString("<?>");
        }
        const QByteArray utf8Value(dataPtr, static_cast<int>(strLen));
        dataPtr += strLen;
        return QString::asprintf(fmt.constData(), utf8Value.constData());
    }
    case ARGUMENT_TYPE_POINTER: {
        quintptr ptrValue = 0;
        if (argSizeBytes == 4 && dataPtr + 4 <= dataEnd) {
            ptrValue = *reinterpret_cast<const uint32_t*>(dataPtr);
            dataPtr += 4;
        } else if (argSizeBytes == 8 && dataPtr + 8 <= dataEnd) {
            ptrValue = static_cast<quintptr>(*reinterpret_cast<const uint64_t*>(dataPtr));
            dataPtr += 8;
        } else {
            if (ok) *ok = false;
            return QString("<?>");
        }
        return QString::asprintf(fmt.constData(), reinterpret_cast<void*>(ptrValue));
    }
    default:
        if (ok) *ok = false;
        return QString("<?>");
    }
}

QString HLogBinaryParser::formatValue(const EntryMeta &meta, const RawEntry &entry,
                                      int &argIndex, const char *&dataPtr, const QString &formatSpec)
{
    if (argIndex >= meta.num_args) {
        return QString("<?>");
    }

    FormatSpec spec;
    int formatPos = 0;
    spec.raw = formatSpec;
    parseNextFormatSpec(formatSpec, formatPos, spec);

    const char *dataEnd = entry.data.constData() + entry.data.size();
    if (dataPtr >= dataEnd) {
        return QString("<?>");
    }

    QString resolvedFormat = formatSpec;
    if (spec.dynamicWidth) {
        bool widthOk = false;
        const int width = consumeDynamicInt(meta, entry, argIndex, dataPtr, &widthOk);
        if (!widthOk) {
            return QString("<?>");
        }
        const int starIndex = resolvedFormat.indexOf('*');
        if (starIndex >= 0) {
            resolvedFormat.replace(starIndex, 1, QString::number(width));
        }
    }
    if (spec.dynamicPrecision) {
        bool precisionOk = false;
        const int precision = consumeDynamicInt(meta, entry, argIndex, dataPtr, &precisionOk);
        if (!precisionOk) {
            return QString("<?>");
        }
        const int dotStarIndex = resolvedFormat.indexOf(QLatin1String(".*"));
        if (dotStarIndex >= 0) {
            resolvedFormat.replace(dotStarIndex, 2, QString(".%1").arg(precision));
        }
    }

    const quint8 argInfo = meta.argument_infos[argIndex];
    argIndex++;

    uint8_t argType = 0;
    uint8_t argSizeBytes = 0;
    resolveArgumentInfo(argInfo, resolvedFormat, argType, argSizeBytes);

    bool ok = false;
    const QString value = applyResolvedFormat(resolvedFormat, argType, argSizeBytes, dataPtr, dataEnd, &ok);
    if (!ok) {
        return QString("<?>");
    }
    return value;
}

QString HLogBinaryParser::formatTimestamp(uint64_t walltimeMs)
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(walltimeMs));
    return dt.toString("yyyy-MM-dd HH:mm:ss");
}

QChar HLogBinaryParser::levelToChar(uint8_t level)
{
    switch (level) {
    case LOG_LEVEL_UNKNOWN: return 'U';
    case LOG_LEVEL_DEBUG: return 'D';
    case LOG_LEVEL_INFO: return 'I';
    case LOG_LEVEL_WARN: return 'W';
    case LOG_LEVEL_ERROR: return 'E';
    case LOG_LEVEL_FATAL: return 'F';
    default: return 'U';
    }
}
