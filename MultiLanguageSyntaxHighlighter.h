// MultiLanguageSyntaxHighlighter.h - 按文件扩展名自适应的语法高亮
//
// 支持: .log/.txt (默认：数字 + 级别关键字)
//       .json (键/字符串/数字/常量)
//       .xml/.html (标签 / 属性 / 字符串 / 注释)
//       .c/.cpp/.h/.hpp (关键字 / 数字 / 字符串 / 单行和多行注释 / 预处理)
//       .py (关键字 / 字符串 / 数字 / 注释)
//       .sh/.bash (关键字 / 变量 / 字符串 / 注释)
//       .yaml/.yml/.ini (键 / 值 / 注释)
//
// 没有匹配到的扩展名回退到 log/默认规则。
//
// 调用：
//   MultiLanguageSyntaxHighlighter *h = new MultiLanguageSyntaxHighlighter(doc);
//   h->setMode(MultiLanguageSyntaxHighlighter::modeForFile(filePath));
//
#ifndef MULTI_LANGUAGE_SYNTAX_HIGHLIGHTER_H
#define MULTI_LANGUAGE_SYNTAX_HIGHLIGHTER_H

#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVector>

class MultiLanguageSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    enum Mode {
        Log = 0,   // 默认：仅数字 + 级别关键字
        Json,
        Xml,
        CFamily,   // C/C++/h/hpp
        Python,
        Shell,
        Yaml,
        Ini
    };

    explicit MultiLanguageSyntaxHighlighter(QTextDocument *parent = nullptr)
        : QSyntaxHighlighter(parent)
    {
        setMode(Log);
    }

    static Mode modeForFile(const QString &path)
    {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == QLatin1String("json")) return Json;
        if (ext == QLatin1String("xml") || ext == QLatin1String("html") ||
            ext == QLatin1String("htm") || ext == QLatin1String("xaml")) return Xml;
        if (ext == QLatin1String("c") || ext == QLatin1String("cpp") ||
            ext == QLatin1String("cc") || ext == QLatin1String("cxx") ||
            ext == QLatin1String("h") || ext == QLatin1String("hpp") ||
            ext == QLatin1String("hxx")) return CFamily;
        if (ext == QLatin1String("py") || ext == QLatin1String("pyw")) return Python;
        if (ext == QLatin1String("sh") || ext == QLatin1String("bash") ||
            ext == QLatin1String("zsh")) return Shell;
        if (ext == QLatin1String("yaml") || ext == QLatin1String("yml")) return Yaml;
        if (ext == QLatin1String("ini") || ext == QLatin1String("conf") ||
            ext == QLatin1String("cfg") || ext == QLatin1String("toml")) return Ini;
        return Log;
    }

    void setMode(Mode m)
    {
        m_mode = m;
        buildRules();
        rehighlight();
    }

    Mode mode() const { return m_mode; }

protected:
    void highlightBlock(const QString &text) override
    {
        // 顺序：规则按优先级排序，后面规则不覆盖已经被前面规则高亮的部分不是必须 —
        // 这里允许后面覆盖，使注释等晚生效规则能遮盖关键字误命中。
        for (const Rule &r : m_rules) {
            auto it = r.re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                const int start = (r.captureGroup >= 0) ? m.capturedStart(r.captureGroup)
                                                        : m.capturedStart();
                const int len   = (r.captureGroup >= 0) ? m.capturedLength(r.captureGroup)
                                                        : m.capturedLength();
                if (len <= 0) continue;
                setFormat(start, len, r.fmt);
            }
        }

        // 多行注释（C/XML）由跨块状态控制
        if (m_mode == CFamily || m_mode == Xml) {
            const QString startDelim = (m_mode == CFamily) ? QStringLiteral("/*")
                                                           : QStringLiteral("<!--");
            const QString endDelim   = (m_mode == CFamily) ? QStringLiteral("*/")
                                                           : QStringLiteral("-->");
            setCurrentBlockState(0);
            int startIndex = 0;
            if (previousBlockState() != 1) {
                startIndex = text.indexOf(startDelim);
            }
            while (startIndex >= 0) {
                const int endIndex = text.indexOf(endDelim, startIndex);
                int length;
                if (endIndex == -1) {
                    setCurrentBlockState(1);
                    length = text.length() - startIndex;
                } else {
                    length = endIndex - startIndex + endDelim.length();
                }
                setFormat(startIndex, length, m_commentFmt);
                startIndex = text.indexOf(startDelim, startIndex + length);
            }
        }
    }

private:
    struct Rule {
        QRegularExpression re;
        QTextCharFormat    fmt;
        int                captureGroup = -1; // -1 = 整个匹配
    };

    void buildRules()
    {
        m_rules.clear();

        QTextCharFormat keywordFmt;   keywordFmt.setForeground(QColor(0, 0, 200));   keywordFmt.setFontWeight(QFont::Bold);
        QTextCharFormat typeFmt;      typeFmt.setForeground(QColor(128, 0, 128));
        QTextCharFormat stringFmt;    stringFmt.setForeground(QColor(170, 0, 0));
        QTextCharFormat numberFmt;    numberFmt.setForeground(QColor(90, 90, 255));
        QTextCharFormat commentFmt;   commentFmt.setForeground(QColor(0, 128, 0));   commentFmt.setFontItalic(true);
        QTextCharFormat preprocFmt;   preprocFmt.setForeground(QColor(0, 128, 128));
        QTextCharFormat tagFmt;       tagFmt.setForeground(QColor(0, 0, 180));       tagFmt.setFontWeight(QFont::Bold);
        QTextCharFormat attrFmt;      attrFmt.setForeground(QColor(128, 64, 0));
        QTextCharFormat keyFmt;       keyFmt.setForeground(QColor(128, 0, 64));      keyFmt.setFontWeight(QFont::Bold);
        QTextCharFormat constFmt;     constFmt.setForeground(QColor(128, 0, 128));   constFmt.setFontWeight(QFont::Bold);
        QTextCharFormat varFmt;       varFmt.setForeground(QColor(0, 96, 160));
        QTextCharFormat errorFmt;     errorFmt.setForeground(QColor(200, 0, 0));     errorFmt.setFontWeight(QFont::Bold);
        QTextCharFormat warnFmt;      warnFmt.setForeground(QColor(200, 120, 0));    warnFmt.setFontWeight(QFont::Bold);
        QTextCharFormat infoFmt;      infoFmt.setForeground(QColor(0, 120, 0));
        m_commentFmt = commentFmt;

        switch (m_mode) {
        case Log: {
            // 默认：数字 + 级别关键字（兼容既有 LogNumberHighlighter 的基础行为）
            add(R"(\b\d+(?:\.\d+)?\b)", numberFmt);
            add(R"(\b(?:ERROR|ERR|FATAL|CRITICAL)\b)", errorFmt,
                QRegularExpression::CaseInsensitiveOption);
            add(R"(\b(?:WARN|WARNING)\b)", warnFmt,
                QRegularExpression::CaseInsensitiveOption);
            add(R"(\b(?:INFO|DEBUG|TRACE|NOTICE)\b)", infoFmt,
                QRegularExpression::CaseInsensitiveOption);
            break;
        }
        case Json: {
            add(R"("(?:\\.|[^"\\])*"\s*:)", keyFmt); // "key":
            add(R"("(?:\\.|[^"\\])*")",      stringFmt);
            add(R"(\b-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)", numberFmt);
            add(R"(\b(?:true|false|null)\b)", constFmt);
            break;
        }
        case Xml: {
            add(R"(<\?xml[^?]*\?>)", preprocFmt);
            add(R"(</?\s*([A-Za-z_][A-Za-z0-9_:-]*))", tagFmt, 1);
            add(R"(\b([A-Za-z_][A-Za-z0-9_:-]*)\s*=)", attrFmt, 1);
            add(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')", stringFmt);
            break;
        }
        case CFamily: {
            const QStringList kw = {
                "auto","break","case","catch","class","const","constexpr","continue",
                "default","delete","do","double","else","enum","explicit","extern",
                "false","float","for","friend","goto","if","inline","int","long",
                "mutable","namespace","new","noexcept","nullptr","operator","override",
                "private","protected","public","register","return","short","signed",
                "sizeof","static","static_cast","const_cast","dynamic_cast","reinterpret_cast",
                "struct","switch","template","this","throw","true","try","typedef","typeid",
                "typename","union","unsigned","using","virtual","void","volatile","while"
            };
            add(QStringLiteral("\\b(?:%1)\\b").arg(kw.join('|')), keywordFmt);
            const QStringList types = {
                "bool","char","char16_t","char32_t","double","float","int","long","short",
                "signed","unsigned","void","wchar_t","size_t","ssize_t","uint8_t","uint16_t",
                "uint32_t","uint64_t","int8_t","int16_t","int32_t","int64_t","QString","QList",
                "QVector","QMap","QHash","QSet"
            };
            add(QStringLiteral("\\b(?:%1)\\b").arg(types.join('|')), typeFmt);
            add(R"(^\s*#\s*\w+.*$)", preprocFmt);
            add(R"(//[^\n]*)", commentFmt);
            add(R"("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')", stringFmt);
            add(R"(\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[uUlLfF]*\b|\b0x[0-9A-Fa-f]+\b)", numberFmt);
            break;
        }
        case Python: {
            const QStringList kw = {
                "False","None","True","and","as","assert","async","await","break","class",
                "continue","def","del","elif","else","except","finally","for","from","global",
                "if","import","in","is","lambda","nonlocal","not","or","pass","raise","return",
                "try","while","with","yield"
            };
            add(QStringLiteral("\\b(?:%1)\\b").arg(kw.join('|')), keywordFmt);
            add(R"(#[^\n]*)", commentFmt);
            add(R"("""[\s\S]*?"""|'''[\s\S]*?'''|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')", stringFmt);
            add(R"(\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)", numberFmt);
            break;
        }
        case Shell: {
            const QStringList kw = {
                "if","then","else","elif","fi","case","esac","for","while","until","do","done",
                "function","return","in","select","time","local","export","readonly","declare",
                "set","unset","source","alias","true","false"
            };
            add(QStringLiteral("\\b(?:%1)\\b").arg(kw.join('|')), keywordFmt);
            add(R"(\$\{[^}]+\}|\$[A-Za-z_][A-Za-z0-9_]*|\$\?|\$!|\$#|\$\*|\$@)", varFmt);
            add(R"(#[^\n]*)", commentFmt);
            add(R"("(?:\\.|[^"\\])*"|'[^']*')", stringFmt);
            add(R"(\b\d+(?:\.\d+)?\b)", numberFmt);
            break;
        }
        case Yaml: {
            add(R"(^[\s-]*([A-Za-z_][\w.-]*)\s*:)", keyFmt, 1);
            add(R"(#[^\n]*)", commentFmt);
            add(R"("(?:\\.|[^"\\])*"|'[^']*')", stringFmt);
            add(R"(\b-?\d+(?:\.\d+)?\b)", numberFmt);
            add(R"(\b(?:true|false|null|yes|no|on|off)\b)", constFmt,
                QRegularExpression::CaseInsensitiveOption);
            break;
        }
        case Ini: {
            add(R"(^\s*\[[^\]\n]+\])", tagFmt);
            add(R"(^\s*([^=;#\n]+?)\s*=)", keyFmt, 1);
            add(R"([;#][^\n]*)", commentFmt);
            add(R"(\b\d+(?:\.\d+)?\b)", numberFmt);
            break;
        }
        }
    }

    void add(const QString &pat, const QTextCharFormat &fmt,
             QRegularExpression::PatternOptions opts)
    {
        Rule r;
        r.re.setPattern(pat);
        r.re.setPatternOptions(opts);
        r.fmt = fmt;
        m_rules.push_back(r);
    }
    void add(const QString &pat, const QTextCharFormat &fmt)
    {
        add(pat, fmt, QRegularExpression::NoPatternOption);
    }
    void add(const QString &pat, const QTextCharFormat &fmt, int captureGroup)
    {
        Rule r;
        r.re.setPattern(pat);
        r.fmt = fmt;
        r.captureGroup = captureGroup;
        m_rules.push_back(r);
    }

    Mode m_mode = Log;
    QVector<Rule> m_rules;
    QTextCharFormat m_commentFmt;
};

#endif // MULTI_LANGUAGE_SYNTAX_HIGHLIGHTER_H
