#pragma once

#include "LogicValue.h"
#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <vector>

// 导线路径、绘制和鼠标命中判定独立于电路计算。
class Wire
{
public:
    using Path = std::vector<wxPoint>;
    static Path Route(const wxPoint& output, const wxPoint& input, int gridSize = 20);
    static wxRect GetBounds(const Path& path);
    static bool HitTest(const Path& path, const wxPoint& point, int tolerance = 6);
    static void Draw(wxDC& dc, const Path& path, LogicValue value, bool selected = false, bool preview = false);
};
