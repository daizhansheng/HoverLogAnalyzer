#ifndef HLOGBINARYPARSER_H
#define HLOGBINARYPARSER_H

#include <QString>
#include <QByteArray>
#include <QList>
#include <QIODevice>
#include "HLogParser.h"

// .hlog 二进制格式解析器
class HLogBinaryParser {
public:
    HLogBinaryParser();
    ~HLogBinaryParser();

    // 从文件路径解析 .hlog 文件
    QList<HLogEntry> parseFromFile(const QString &filePath);

    // 从 QIODevice 解析
    QList<HLogEntry> parse(QIODevice *device);

private:
    // Magic number: 0x676f4c68 ("hLog")
    static constexpr uint32_t HLOG_MAGIC = 0x676f4c68;

    // Entry type IDs
    enum EntryTypeId {
        ENTRY_TYPE_ID_MODULE_INIT = 0,
        ENTRY_TYPE_ID_TIME_SYNCHRONIZATION = 1,
        ENTRY_TYPE_ID_META = 2,
        ENTRY_TYPE_ID_MAGIC_NUMBER = HLOG_MAGIC
    };

    // Log levels
    enum LogLevel {
        LOG_LEVEL_UNKNOWN = 0,
        LOG_LEVEL_DEBUG = 1,
        LOG_LEVEL_INFO = 2,
        LOG_LEVEL_WARN = 3,
        LOG_LEVEL_ERROR = 4,
        LOG_LEVEL_FATAL = 5
    };

    // Argument types
    enum ArgumentType {
        ARGUMENT_TYPE_UNSIGN_INT = 0,
        ARGUMENT_TYPE_SIGN_INT = 1,
        ARGUMENT_TYPE_FLOAT = 2,
        ARGUMENT_TYPE_STRING = 3,
        ARGUMENT_TYPE_POINTER = 4
    };

    // Raw entry structure
    struct RawEntry {
        uint32_t type_id;
        uint32_t size;
        uint64_t monotonic_raw_timestamp_ms;
        uint64_t utc_walltime_timestamp_ms;
        bool has_utc_walltime_timestamp = false;
        QByteArray data;
    };

    enum class EntryLayout {
        Unknown,
        Legacy,
        WithWalltime
    };

    // Entry meta structure
    struct EntryMeta {
        uint32_t type_id;
        uint8_t level;
        uint8_t num_args;
        uint8_t num_params;
        QByteArray param_types;
        QList<quint8> argument_infos;  // 每个元素包含 size(4bit) + type(4bit)
        uint32_t linenum;
        QString filename;
        QString tags;
        QString format;
    };

    struct FormatSpec {
        QString raw;
        QChar conversion;
        bool dynamicWidth = false;
        bool dynamicPrecision = false;
    };

    // 解析下一个原始条目
    bool parseNextRawEntry(QIODevice *device, RawEntry &entry);

    // 处理不同类型的条目
    bool handleMagicNumber(const RawEntry &entry, uint8_t &version, uint8_t &flags);
    bool handleModuleInit(const RawEntry &entry, QString &moduleName);
    bool handleTimeSync(const RawEntry &entry, int32_t &timezoneOffset, uint64_t &utcWalltime);
    bool handleEntryMeta(const RawEntry &entry, EntryMeta &meta);
    bool handleLogEntry(const RawEntry &entry, const EntryMeta &meta,
                       uint64_t monotonicTimestampMs, uint64_t walltime, HLogEntry &logEntry);

    // 格式化日志条目
    QString formatLogEntry(const EntryMeta &meta, const RawEntry &entry,
                           uint64_t monotonicTimestampMs, uint64_t walltime);

    QString formatContent(const EntryMeta &meta, const RawEntry &entry);
    bool parseNextFormatSpec(const QString &format, int &pos, FormatSpec &spec);
    int consumeDynamicInt(const EntryMeta &meta, const RawEntry &entry, int &argIndex, const char *&dataPtr, bool *ok);
    void resolveArgumentInfo(quint8 argInfo,
                             const QString &resolvedFormat,
                             uint8_t &argType,
                             uint8_t &argSizeBytes) const;
    QString applyResolvedFormat(const QString &resolvedFormat,
                                uint8_t argType,
                                uint8_t argSizeBytes,
                                const char *&dataPtr,
                                const char *dataEnd,
                                bool *ok);

    // 解析参数值
    QString formatValue(const EntryMeta &meta, const RawEntry &entry,
                        int &argIndex, const char *&dataPtr, const QString &formatSpec);

    // 时间戳转换
    QString formatTimestamp(uint64_t walltimeMs);

    // 级别字符转换
    QChar levelToChar(uint8_t level);

private:
    uint8_t hlog_version_;
    uint8_t hlog_flags_;
    int32_t timezone_offset_s_;
    uint64_t diff_from_utc_to_monotonic_ms_;
    QMap<uint32_t, EntryMeta> entry_meta_map_;
    bool magic_number_parsed_;
    EntryLayout entry_layout_;
};

#endif // HLOGBINARYPARSER_H
