#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct KiCadSExpr
{
    bool list = false;
    bool quoted = false;
    std::string value;
    std::vector<KiCadSExpr> children;
    std::size_t offset = 0;
    std::string_view Head() const;
    const KiCadSExpr* Find(std::string_view name) const;
    std::vector<const KiCadSExpr*> FindAll(std::string_view name) const;
};

class KiCadSExprParser
{
public:
    static constexpr std::size_t MaxDepth = 128;
    static constexpr std::size_t MaxNodes = 500000;
    static bool Parse(std::string_view source, KiCadSExpr& root, std::string& error);
};
