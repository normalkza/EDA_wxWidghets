#include "Wire.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    wxPoint Direction(const wxPoint& point, const wxPoint& fallback = wxPoint(1, 0))
    {
        if (point.x != 0) return wxPoint(point.x > 0 ? 1 : -1, 0);
        if (point.y != 0) return wxPoint(0, point.y > 0 ? 1 : -1);
        return fallback;
    }

    bool Orthogonal(const Wire::Path& path)
    {
        if (path.size() < 2) return false;
        for (std::size_t i = 1; i < path.size(); ++i)
            if (path[i] == path[i - 1] ||
                (path[i].x != path[i - 1].x && path[i].y != path[i - 1].y)) return false;
        return true;
    }

    Wire::Path Simplify(const Wire::Path& path)
    {
        Wire::Path result;
        for (const auto& point : path)
        {
            if (!result.empty() && point == result.back()) continue;
            while (result.size() >= 2)
            {
                const auto& a = result[result.size() - 2];
                const auto& b = result.back();
                if ((a.x == b.x && b.x == point.x) || (a.y == b.y && b.y == point.y))
                    result.pop_back();
                else break;
            }
            if (result.empty() || result.back() != point) result.push_back(point);
        }
        return result;
    }

    Wire::Path LimitPath(Wire::Path path)
    {
        path = Simplify(path);
        // 保留两端的接线方向；达到文件点数上限时合并中部的小折线。
        while (path.size() > 256)
        {
            const std::size_t middle = path.size() / 2;
            Wire::Path reduced(path.begin(), path.begin() + middle);
            reduced.push_back(wxPoint(path[middle - 1].x, path[middle + 2].y));
            reduced.insert(reduced.end(), path.begin() + middle + 2, path.end());
            path = Simplify(reduced);
        }
        return path;
    }

    bool MatchingDirections(const Wire::Path& path, const wxPoint& output, const wxPoint& input)
    {
        return path.size() >= 2 && Direction(path[1] - path.front()) == output &&
            Direction(path[path.size() - 2] - path.back()) == input;
    }

    double SegmentDistanceSquared(const wxPoint& a, const wxPoint& b, const wxPoint& point)
    {
        const double dx = static_cast<double>(b.x) - a.x, dy = static_cast<double>(b.y) - a.y;
        const double length = dx * dx + dy * dy;
        const double t = length > 0 ? std::clamp(
            ((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0, 1.0) : 0;
        const double px = point.x - (a.x + t * dx), py = point.y - (a.y + t * dy);
        return px * px + py * py;
    }
}

Wire::Path Wire::Route(const wxPoint& output, const wxPoint& input, int gridSize,
    const wxPoint& outputDirection, const wxPoint& inputDirection)
{
    gridSize = std::max(1, gridSize);
    const wxPoint out = Direction(outputDirection), in = Direction(inputDirection, wxPoint(-1, 0));
    // 未旋转的元件保留原有的中线/上方绕行布局。
    if (out == wxPoint(1, 0) && in == wxPoint(-1, 0))
    {
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
        path = Simplify(path);
        if (Orthogonal(path) && MatchingDirections(path, out, in)) return path;
    }

    const wxPoint start = output + out * gridSize, finish = input + in * gridSize;
    Path best;
    double bestCost = std::numeric_limits<double>::max();
    const auto consider = [&](const Path& middle) {
        Path path{ output };
        path.insert(path.end(), middle.begin(), middle.end());
        path.push_back(input);
        path = Simplify(path);
        if (!Orthogonal(path) || !MatchingDirections(path, out, in)) return;
        double cost = static_cast<double>(path.size() - 2) * gridSize * 0.35;
        for (std::size_t i = 1; i < path.size(); ++i)
            cost += std::abs(path[i].x - path[i - 1].x) + std::abs(path[i].y - path[i - 1].y);
        if (cost < bestCost) { bestCost = cost; best = std::move(path); }
    };
    if (start.x == finish.x || start.y == finish.y) consider({ start, finish });
    consider({ start, wxPoint(start.x, finish.y), finish });
    consider({ start, wxPoint(finish.x, start.y), finish });
    const int columns[] = { start.x, finish.x, (start.x + finish.x) / 2,
        std::min(start.x, finish.x) - gridSize, std::max(start.x, finish.x) + gridSize };
    const int rows[] = { start.y, finish.y, (start.y + finish.y) / 2,
        std::min(start.y, finish.y) - gridSize, std::max(start.y, finish.y) + gridSize };
    for (int column : columns)
        consider({ start, wxPoint(column, start.y), wxPoint(column, finish.y), finish });
    for (int row : rows)
        consider({ start, wxPoint(start.x, row), wxPoint(finish.x, row), finish });
    // 相向重叠的引脚也需要保留可见的正交回路。
    if (best.empty())
        for (int column : columns)
            for (int row : rows)
            {
                consider({ start, wxPoint(column, start.y), wxPoint(column, row),
                    wxPoint(finish.x, row), finish });
                consider({ start, wxPoint(start.x, row), wxPoint(column, row),
                    wxPoint(column, finish.y), finish });
            }
    return best;
}

Wire::Path Wire::Reanchor(const Path& path, const wxPoint& output, const wxPoint& input,
    int gridSize, const wxPoint& outputDirection, const wxPoint& inputDirection)
{
    const wxPoint out = Direction(outputDirection), in = Direction(inputDirection, wxPoint(-1, 0));
    if (!Orthogonal(path)) return Route(output, input, gridSize, out, in);
    if (path.front() == output && path.back() == input && MatchingDirections(path, out, in)) return path;
    if (path.size() == 2) return Route(output, input, gridSize, out, in);

    const wxPoint firstDirection = Direction(path[1] - path.front());
    const wxPoint lastDirection = Direction(path.back() - path[path.size() - 2]);
    const Path prefix = path.front() == output && firstDirection == out ? Path{ output, path[1] } :
        Route(output, path[1], gridSize, out, -firstDirection);
    const Path suffix = path.back() == input && -lastDirection == in ?
        Path{ path[path.size() - 2], input } :
        Route(path[path.size() - 2], input, gridSize, lastDirection, in);
    if (prefix.empty() || suffix.empty()) return Route(output, input, gridSize, out, in);
    Path result = prefix;
    result.insert(result.end(), path.begin() + 2, path.end() - 1);
    result.insert(result.end(), suffix.begin() + 1, suffix.end());
    result = LimitPath(std::move(result));
    if (!Orthogonal(result) || !MatchingDirections(result, out, in))
        return Route(output, input, gridSize, out, in);
    return result;
}

Wire::Path Wire::MoveSegment(const Path& path, std::size_t segment, const wxPoint& delta, int gridSize)
{
    if (!Orthogonal(path) || segment + 1 >= path.size()) return path;
    gridSize = std::max(1, gridSize);
    const wxPoint a = path[segment], b = path[segment + 1];
    const bool horizontal = a.y == b.y;
    const int movement = horizontal ? delta.y : delta.x;
    if (movement == 0) return path;
    const int origin = horizontal ? a.y : a.x;
    const double snapped = std::round((static_cast<double>(origin) + movement) / gridSize) * gridSize;
    const int position = static_cast<int>(std::clamp(snapped, -1000000.0, 1000000.0));
    if (position == origin) return path;
    const auto shifted = [&](wxPoint point) {
        if (horizontal) point.y = position;
        else point.x = position;
        return point;
    };
    const bool first = segment == 0, last = segment + 2 == path.size();
    const wxPoint direction = Direction(b - a);
    const int length = std::abs(b.x - a.x) + std::abs(b.y - a.y);
    const int leadLength = length == 1 ? std::max(2, gridSize) :
        std::max(1, std::min(gridSize, length / 3));
    Path result;
    if (first)
    {
        const wxPoint lead = a + direction * leadLength;
        result = { a, lead, shifted(lead) };
    }
    else
    {
        result.insert(result.end(), path.begin(), path.begin() + segment);
        result.push_back(shifted(a));
    }
    if (last)
    {
        const wxPoint lead = b - direction * leadLength;
        result.push_back(shifted(lead));
        result.push_back(lead);
        result.push_back(b);
    }
    else
    {
        result.push_back(shifted(b));
        result.insert(result.end(), path.begin() + segment + 2, path.end());
    }
    result = LimitPath(std::move(result));
    return Orthogonal(result) ? result : path;
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

int Wire::HitTestSegment(const Path& path, const wxPoint& point, int tolerance)
{
    int best = -1;
    double distance = static_cast<double>(tolerance) * tolerance;
    double handleDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 1; i < path.size(); ++i)
    {
        if (path[i] == path[i - 1]) continue;
        const double candidate = SegmentDistanceSquared(path[i - 1], path[i], point);
        const wxPoint middle((path[i - 1].x + path[i].x) / 2, (path[i - 1].y + path[i].y) / 2);
        const double middleDistance = SegmentDistanceSquared(middle, middle, point);
        if (candidate < distance || (candidate == distance && middleDistance < handleDistance))
        {
            best = static_cast<int>(i) - 1;
            distance = candidate;
            handleDistance = middleDistance;
        }
    }
    return best;
}

bool Wire::HitTest(const Path& path, const wxPoint& point, int tolerance)
{
    return HitTestSegment(path, point, tolerance) >= 0;
}

void Wire::Draw(wxDC& dc, const Path& path, LogicValue value, bool selected, bool preview)
{
    if (path.size() < 2) return;
    const wxColour colour = preview || selected ? wxColour(53, 120, 229) :
        (value == LogicValue::High ? wxColour(30, 143, 83) : wxColour(119, 133, 153));
    dc.SetPen(wxPen(colour, selected ? 3 : 2, preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
    dc.DrawLines(static_cast<int>(path.size()), path.data());
    if (selected && !preview)
    {
        dc.SetPen(wxPen(colour, 1));
        dc.SetBrush(*wxWHITE_BRUSH);
        for (std::size_t i = 1; i < path.size(); ++i)
        {
            if (path[i] == path[i - 1]) continue;
            const wxPoint middle((path[i - 1].x + path[i].x) / 2, (path[i - 1].y + path[i].y) / 2);
            dc.DrawRectangle(middle.x - 3, middle.y - 3, 7, 7);
        }
    }
}
