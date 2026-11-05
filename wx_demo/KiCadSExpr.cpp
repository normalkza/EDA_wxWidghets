#include "KiCadSExpr.h"

#include <exception>
#include <stdexcept>
#include <utility>

std::string_view KiCadSExpr::Head() const
{
    return list && !children.empty() && !children[0].list ? children[0].value : std::string_view{};
}

const KiCadSExpr* KiCadSExpr::Find(std::string_view name) const
{
    for (const auto& child : children) if (child.list && child.Head() == name) return &child;
    return nullptr;
}

std::vector<const KiCadSExpr*> KiCadSExpr::FindAll(std::string_view name) const
{
    std::vector<const KiCadSExpr*> result;
    for (const auto& child : children) if (child.list && child.Head() == name) result.push_back(&child);
    return result;
}

namespace
{
bool ValidUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
        {
            if (first == 0 || (first < 0x20 && first != '\t' && first != '\n' && first != '\r')) return false;
            continue;
        }
        unsigned remaining = 0;
        unsigned code = 0, minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { remaining = 1; code = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { remaining = 2; code = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; code = first & 0x07; minimum = 0x10000; }
        else return false;
        if (remaining > text.size() - i) return false;
        while (remaining--)
        {
            const unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

class Parser
{
public:
    explicit Parser(std::string_view text) : m_text(text)
    {
        if (text.starts_with("\xef\xbb\xbf")) m_position = 3;
    }
    KiCadSExpr Parse()
    {
        Skip();
        auto root = Read(0);
        Skip();
        if (m_position != m_text.size()) Fail("根表达式之后仍有内容");
        return root;
    }
private:
    [[noreturn]] void Fail(const char* message) const
    {
        std::size_t line = 1;
        for (std::size_t i = 0; i < m_position && i < m_text.size(); ++i) if (m_text[i] == '\n') ++line;
        throw std::runtime_error(std::string(message) + "（第 " + std::to_string(line) + " 行，字节 " +
            std::to_string(m_position) + "）");
    }
    void Skip()
    {
        while (m_position < m_text.size())
        {
            const char c = m_text[m_position];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++m_position; continue; }
            if (c == ';')
            {
                while (m_position < m_text.size() && m_text[m_position] != '\n') ++m_position;
                continue;
            }
            break;
        }
    }
    KiCadSExpr Read(std::size_t depth)
    {
        Skip();
        if (m_position >= m_text.size()) Fail("表达式不完整");
        if (++m_nodes > KiCadSExprParser::MaxNodes) Fail("表达式节点超过 500000 个");
        KiCadSExpr node;
        node.offset = m_position;
        const char current = m_text[m_position++];
        if (current == '(')
        {
            if (depth >= KiCadSExprParser::MaxDepth) Fail("表达式嵌套超过 128 层");
            node.list = true;
            while (true)
            {
                Skip();
                if (m_position >= m_text.size()) Fail("缺少右括号");
                if (m_text[m_position] == ')') { ++m_position; break; }
                node.children.push_back(Read(depth + 1));
            }
        }
        else if (current == ')') Fail("出现多余的右括号");
        else if (current == '"')
        {
            node.quoted = true;
            bool closed = false;
            while (m_position < m_text.size())
            {
                char c = m_text[m_position++];
                if (c == '"') { closed = true; break; }
                if (c == '\\')
                {
                    if (m_position >= m_text.size()) Fail("字符串转义不完整");
                    c = m_text[m_position++];
                    if (c == 'n') c = '\n';
                    else if (c == 'r') c = '\r';
                    else if (c == 't') c = '\t';
                    else if (c != '\\' && c != '"')
                    {
                        // Preserve unknown escapes in path/model strings.
                        node.value += '\\';
                    }
                }
                node.value += c;
                if (node.value.size() > 8u * 1024u * 1024u) Fail("单个字符串超过 8 MiB");
            }
            if (!closed) Fail("字符串缺少结束引号");
        }
        else
        {
            node.value += current;
            while (m_position < m_text.size())
            {
                const char c = m_text[m_position];
                if (c == '(' || c == ')' || c == ';' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
                if (c == '"') Fail("非字符串标记中出现引号");
                node.value += c;
                ++m_position;
                if (node.value.size() > 8u * 1024u * 1024u) Fail("单个标记超过 8 MiB");
            }
        }
        return node;
    }
    std::string_view m_text;
    std::size_t m_position = 0, m_nodes = 0;
};
}

bool KiCadSExprParser::Parse(std::string_view source, KiCadSExpr& root, std::string& error)
{
    error.clear();
    if (source.empty() || source.size() > 64u * 1024u * 1024u)
    { error = "文件为空或超过 64 MiB。"; return false; }
    if (!ValidUtf8(source)) { error = "文件不是有效 UTF-8，或包含 NUL/非法控制字符。"; return false; }
    try
    {
        auto candidate = Parser(source).Parse();
        root = std::move(candidate);
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
