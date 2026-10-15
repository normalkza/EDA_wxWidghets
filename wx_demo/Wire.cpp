#include "Wire.h"
#include <algorithm>
#include <cmath>

Wire::Path Wire::Route(const wxPoint& output, const wxPoint& input, int gridSize)
{
    gridSize = std::max(1, gridSize);
    Path path;
    if (input.x >= output.x + gridSize * 2)
    {
        const int middle = ((output.x + input.x) / (2 * gridSize)) * gridSize;
        path = { output, wxPoint(middle, output.y), wxPoint(middle, input.y), input };
    }
    else
    {
        const int right = output.x + gridSize, left = input.x - gridSize;
        const int row = std::max(0, std::min(output.y, input.y) - 2 * gridSize);
        path = { output, wxPoint(right, output.y), wxPoint(right, row),
            wxPoint(left, row), wxPoint(left, input.y), input };
    }
    path.erase(std::unique(path.begin(), path.end()), path.end());
    return path;
}

wxRect Wire::GetBounds(const Path& path)
{
    if (path.empty()) return wxRect();
    int left = path[0].x, right = left, top = path[0].y, bottom = top;
    for (const auto& point : path)
    {
        left = std::min(left, point.x); right = std::max(right, point.x);
        top = std::min(top, point.y); bottom = std::max(bottom, point.y);
    }
    return wxRect(left - 6, top - 6, right - left + 13, bottom - top + 13);
}

bool Wire::HitTest(const Path& path, const wxPoint& point, int tolerance)
{
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        const auto& a = path[i - 1];
        const auto& b = path[i];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double length = dx * dx + dy * dy;
        const double t = length > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0, 1.0) : 0;
        const double px = point.x - (a.x + t * dx), py = point.y - (a.y + t * dy);
        if (px * px + py * py <= tolerance * tolerance) return true;
    }
    return false;
}

void Wire::Draw(wxDC& dc, const Path& path, LogicValue value, bool selected, bool preview)
{
    if (path.size() < 2) return;
    const wxColour colour = preview || selected ? wxColour(53, 120, 229) :
        (value == LogicValue::High ? wxColour(30, 143, 83) : wxColour(119, 133, 153));
    dc.SetPen(wxPen(colour, selected ? 3 : 2, preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
    dc.DrawLines(static_cast<int>(path.size()), path.data());
}
