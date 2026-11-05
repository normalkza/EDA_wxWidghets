#include "WaveformPanel.h"

#include <wx/button.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

namespace
{
    const wxColour Background(255, 255, 255);
    const wxColour Header(243, 246, 251);
    const wxColour Text(36, 50, 71);
    const wxColour Secondary(100, 116, 139);
    const wxColour Grid(229, 235, 244);
    const wxColour Signal(22, 139, 88);
    const wxColour CursorA(53, 120, 229);
    const wxColour CursorB(203, 112, 30);
    constexpr int ScrollRange = 1000000;

    wxString ExactTime(std::int64_t value)
    {
        return wxString::FromUTF8(std::to_string(value)) + wxS(" ns");
    }

    wxString TickLabel(long double timeNs, long double stepNs)
    {
        const long double magnitude = std::max(std::fabs(timeNs), stepNs);
        long double divisor = 1;
        wxString unit = wxS("ns");
        if (magnitude >= 1000000000.L) { divisor = 1000000000.L; unit = wxS("s"); }
        else if (magnitude >= 1000000.L) { divisor = 1000000.L; unit = wxS("ms"); }
        else if (magnitude >= 1000.L) { divisor = 1000.L; unit = wxS("µs"); }
        return wxString::Format(wxS("%.5g %s"), static_cast<double>(timeNs / divisor), unit);
    }

    long double TickStep(long double approximate)
    {
        approximate = std::max(1.L, approximate);
        const long double power = std::pow(10.L, std::floor(std::log10(approximate)));
        const long double fraction = approximate / power;
        return power * (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10);
    }
}

WaveformPanel::WaveformPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(Background);
    SetForegroundColour(Text);
    SetFont(wxFont(wxFontInfo(9).FaceName(wxS("Microsoft YaHei UI"))));
    SetMinSize(FromDIP(wxSize(320, 170)));

    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* title = new wxStaticText(this, wxID_ANY, wxS("数字时序波形"));
    title->SetFont(wxFont(wxFontInfo(10).FaceName(wxS("Microsoft YaHei UI")).Bold()));
    toolbar->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));
    auto addButton = [this, toolbar](const wxString& label, const wxString& tooltip,
        const std::function<void()>& action) {
        auto* button = new wxButton(this, wxID_ANY, label, wxDefaultPosition,
            FromDIP(wxSize(52, 27)), wxBU_EXACTFIT);
        button->SetToolTip(tooltip);
        button->Bind(wxEVT_BUTTON, [action](wxCommandEvent&) { action(); });
        toolbar->Add(button, 0, wxRIGHT, FromDIP(4));
    };
    addButton(wxS("−"), wxS("缩小时间轴（Ctrl + 滚轮）"), [this] { Zoom(2); });
    addButton(wxS("＋"), wxS("放大时间轴（Ctrl + 滚轮）"), [this] { Zoom(.5L); });
    addButton(wxS("适配"), wxS("显示完整的已记录时间范围"), [this] { Fit(); });
    addButton(wxS("清游标"), wxS("清除 A / B 测量游标"), [this] {
        m_cursorA.reset(); m_cursorB.reset(); UpdateCaption(); m_plot->Refresh(false);
    });
    toolbar->AddStretchSpacer();
    auto* hint = new wxStaticText(this, wxID_ANY, wxS("左键 A · 右键 B"));
    hint->SetForegroundColour(Secondary);
    toolbar->Add(hint, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

    m_plot = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_plot->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_plot->SetBackgroundColour(Background);
    m_plot->SetMinSize(wxSize(0, FromDIP(84)));
    m_plot->SetToolTip(wxS("左键设置 A，右键设置 B；Ctrl + 滚轮缩放；Shift + 滚轮平移时间轴。"));
    m_plot->Bind(wxEVT_PAINT, &WaveformPanel::Paint, this);
    m_plot->Bind(wxEVT_LEFT_DOWN, &WaveformPanel::OnMouse, this);
    m_plot->Bind(wxEVT_RIGHT_DOWN, &WaveformPanel::OnMouse, this);
    m_plot->Bind(wxEVT_MOUSEWHEEL, &WaveformPanel::OnMouse, this);
    m_plot->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (m_autoFit) m_nsPerPixel = std::max(1.L / 32, m_extentNs / PlotWidth());
        RefreshView();
        event.Skip();
    });

    m_vertical = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
    m_horizontal = new wxScrollBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_HORIZONTAL);
    auto scrollTime = [this](wxScrollEvent& event) {
        if (m_syncingScrollbars) return;
        m_autoFit = false;
        m_startNs = m_extentNs * event.GetPosition() / ScrollRange;
        RefreshView();
    };
    auto scrollRows = [this](wxScrollEvent& event) {
        if (m_syncingScrollbars) return;
        m_firstRow = event.GetPosition();
        RefreshView();
    };
    for (const auto& eventType : {wxEVT_SCROLL_TOP, wxEVT_SCROLL_BOTTOM,
        wxEVT_SCROLL_LINEUP, wxEVT_SCROLL_LINEDOWN, wxEVT_SCROLL_PAGEUP,
        wxEVT_SCROLL_PAGEDOWN, wxEVT_SCROLL_THUMBTRACK, wxEVT_SCROLL_THUMBRELEASE})
    {
        m_horizontal->Bind(eventType, scrollTime);
        m_vertical->Bind(eventType, scrollRows);
    }

    m_caption = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxST_ELLIPSIZE_END);
    m_caption->SetForegroundColour(Secondary);
    auto* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(m_plot, 1, wxEXPAND);
    body->Add(m_vertical, 0, wxEXPAND);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(toolbar, 0, wxEXPAND | wxALL, FromDIP(8));
    layout->Add(body, 1, wxEXPAND);
    layout->Add(m_horizontal, 0, wxEXPAND);
    layout->Add(m_caption, 0, wxEXPAND | wxALL, FromDIP(6));
    SetSizer(layout);
    UpdateCaption();
    UpdateScrollbars();
}

int WaveformPanel::PlotLeft() const { return FromDIP(190); }
int WaveformPanel::PlotWidth() const { return std::max(1, m_plot->GetClientSize().x - PlotLeft() - FromDIP(12)); }
int WaveformPanel::RowHeight() const { return FromDIP(38); }
int WaveformPanel::HeaderHeight() const { return FromDIP(30); }

void WaveformPanel::SetTraces(const std::vector<DigitalTrace>& traces, std::int64_t currentTimeNs)
{
    m_traces = traces;
    m_currentTimeNs = std::max<std::int64_t>(0, currentTimeNs);
    m_extentNs = std::max(1.L, static_cast<long double>(m_currentTimeNs));
    for (const auto& trace : m_traces)
        if (!trace.samples.empty())
            m_extentNs = std::max(m_extentNs, static_cast<long double>(trace.samples.back().timeNs));
    if (m_autoFit) m_nsPerPixel = std::max(1.L / 32, m_extentNs / PlotWidth());
    RefreshView();
}

void WaveformPanel::Clear()
{
    m_traces.clear();
    m_currentTimeNs = 0;
    m_cursorA.reset();
    m_cursorB.reset();
    m_extentNs = 1;
    m_startNs = 0;
    m_firstRow = 0;
    m_nsPerPixel = 1;
    m_autoFit = true;
    RefreshView();
}

void WaveformPanel::Zoom(long double factor, int anchorX)
{
    m_autoFit = false;
    const int relativeX = anchorX < 0 ? PlotWidth() / 2 :
        std::clamp(anchorX - PlotLeft(), 0, PlotWidth());
    const long double anchor = m_startNs + relativeX * m_nsPerPixel;
    m_nsPerPixel = std::clamp(m_nsPerPixel * factor, 1.L / 32,
        std::max(1.L / 32, m_extentNs / PlotWidth()));
    m_startNs = anchor - relativeX * m_nsPerPixel;
    RefreshView();
}

void WaveformPanel::Fit()
{
    m_autoFit = true;
    m_startNs = 0;
    m_nsPerPixel = std::max(1.L / 32, m_extentNs / PlotWidth());
    RefreshView();
}

void WaveformPanel::UpdateScrollbars()
{
    if (!m_horizontal || !m_vertical) return;
    m_syncingScrollbars = true;
    const long double visibleTime = PlotWidth() * m_nsPerPixel;
    m_startNs = std::clamp(m_startNs, 0.L, std::max(0.L, m_extentNs - visibleTime));
    const int thumb = std::clamp(static_cast<int>(
        std::min(1.L, visibleTime / m_extentNs) * ScrollRange), 1, ScrollRange);
    const int position = std::clamp(static_cast<int>(m_startNs / m_extentNs * ScrollRange),
        0, ScrollRange - thumb);
    m_horizontal->SetScrollbar(position, thumb, ScrollRange, std::max(1, thumb * 4 / 5));
    m_horizontal->Enable(thumb < ScrollRange);
    const int visibleRows = std::max(1, (m_plot->GetClientSize().y - HeaderHeight()) / RowHeight());
    const int rowCount = static_cast<int>(std::min<std::size_t>(m_traces.size(),
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    m_firstRow = std::clamp(m_firstRow, 0, std::max(0, rowCount - visibleRows));
    m_vertical->SetScrollbar(m_firstRow, visibleRows, std::max(visibleRows, rowCount), visibleRows);
    m_vertical->Enable(rowCount > visibleRows);
    m_syncingScrollbars = false;
}

void WaveformPanel::UpdateCaption()
{
    wxString text = wxS("当前 ") + ExactTime(m_currentTimeNs);
    if (m_cursorA) text += wxS("    A ") + ExactTime(*m_cursorA);
    if (m_cursorB) text += wxS("    B ") + ExactTime(*m_cursorB);
    if (m_cursorA && m_cursorB)
    {
        // Both cursors are non-negative signed 64-bit times, so this difference
        // is representable even at the largest supported virtual time.
        text += wxS("    Δt (B−A) ") + ExactTime(*m_cursorB - *m_cursorA);
    }
    if (!m_traces.empty())
        text += wxS("    ") + wxString::Format(wxS("%llu 个信号"),
            static_cast<unsigned long long>(m_traces.size()));
    m_caption->SetLabel(text);
    m_caption->SetToolTip(text);
}

void WaveformPanel::RefreshView()
{
    UpdateScrollbars();
    UpdateCaption();
    m_plot->Refresh(false);
}

std::int64_t WaveformPanel::TimeAtX(int x) const
{
    const long double time = std::clamp(m_startNs +
        (x - PlotLeft()) * m_nsPerPixel, 0.L, m_extentNs);
    const long double maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (time >= maximum) return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(std::floor(time + .5L));
}

std::optional<LogicValue> WaveformPanel::ValueAt(const DigitalTrace& trace, std::int64_t time) const
{
    const auto next = std::upper_bound(trace.samples.begin(), trace.samples.end(), time,
        [](std::int64_t value, const TraceSample& sample) { return value < sample.timeNs; });
    if (next == trace.samples.begin()) return std::nullopt;
    return std::prev(next)->value;
}

void WaveformPanel::OnMouse(wxMouseEvent& event)
{
    if (event.GetEventType() == wxEVT_MOUSEWHEEL)
    {
        const int rotation = event.GetWheelRotation();
        if (rotation == 0) return;
        if (event.ControlDown()) Zoom(rotation > 0 ? .5L : 2.L, event.GetX());
        else if (event.ShiftDown())
        {
            m_autoFit = false;
            m_startNs += (rotation > 0 ? -1 : 1) * PlotWidth() * m_nsPerPixel / 5;
            RefreshView();
        }
        else
        {
            m_firstRow += rotation > 0 ? -3 : 3;
            RefreshView();
        }
        return;
    }
    if (m_traces.empty() || event.GetX() < PlotLeft()) return;
    if (event.LeftDown()) m_cursorA = TimeAtX(event.GetX());
    if (event.RightDown()) m_cursorB = TimeAtX(event.GetX());
    UpdateCaption();
    m_plot->Refresh(false);
}

void WaveformPanel::Paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_plot);
    dc.SetBackground(wxBrush(Background));
    dc.Clear();
    dc.SetFont(GetFont());
    const wxSize size = m_plot->GetClientSize();
    const int left = PlotLeft();
    const int width = PlotWidth();
    const int header = HeaderHeight();
    const int rowHeight = RowHeight();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(Header));
    dc.DrawRectangle(0, 0, size.x, header);
    dc.SetTextForeground(Secondary);
    dc.DrawText(wxS("信号"), FromDIP(12), FromDIP(7));
    dc.DrawText(m_cursorA ? wxS("A 值") : wxS("当前"), left - FromDIP(42), FromDIP(7));
    dc.SetPen(wxPen(Grid));
    dc.DrawLine(left, 0, left, size.y);
    dc.DrawLine(0, header, size.x, header);
    if (m_traces.empty())
    {
        dc.SetTextForeground(Secondary);
        dc.DrawText(wxS("运行或步进仿真后显示波形；连接 PROBE 可命名观察信号。"),
            FromDIP(16), header + FromDIP(20));
        return;
    }

    const long double visibleEnd = std::min(m_extentNs, m_startNs + width * m_nsPerPixel);
    const long double step = TickStep(FromDIP(100) * m_nsPerPixel);
    const long double firstTick = std::ceil(m_startNs / step) * step;
    dc.SetClippingRegion(left, 0, width + 1, size.y);
    // The pixel-derived bound prevents rounding at very large times from ever
    // turning this drawing loop into an unbounded increment of a floating value.
    const int maximumTicks = width / std::max(1, FromDIP(30)) + 3;
    for (int tick = 0; tick < maximumTicks; ++tick)
    {
        const long double time = firstTick + tick * step;
        if (time > visibleEnd) break;
        const int x = left + static_cast<int>((time - m_startNs) / m_nsPerPixel);
        dc.SetPen(wxPen(Grid));
        dc.DrawLine(x, header, x, size.y);
        dc.SetTextForeground(Secondary);
        dc.DrawText(TickLabel(time, step), x + FromDIP(3), FromDIP(7));
    }
    dc.DestroyClippingRegion();

    const std::int64_t valueTime = m_cursorA.value_or(m_currentTimeNs);
    const int rowsVisible = std::max(0, (size.y - header + rowHeight - 1) / rowHeight);
    for (int row = 0; row < rowsVisible; ++row)
    {
        const std::size_t index = static_cast<std::size_t>(m_firstRow) + row;
        if (index >= m_traces.size()) break;
        const auto& trace = m_traces[index];
        const int top = header + row * rowHeight;
        const int lowY = top + rowHeight - FromDIP(9);
        const int highY = top + FromDIP(9);
        dc.SetPen(wxPen(Grid));
        dc.DrawLine(0, top + rowHeight, size.x, top + rowHeight);
        dc.SetTextForeground(Text);
        const wxString name = wxString::FromUTF8(trace.name);
        dc.DrawText(wxControl::Ellipsize(name, dc, wxELLIPSIZE_END,
            std::max(1, left - FromDIP(62))), FromDIP(12), top + FromDIP(10));
        const auto value = ValueAt(trace, valueTime);
        dc.SetTextForeground(value == LogicValue::High ? Signal : Secondary);
        dc.DrawText(value ? (*value == LogicValue::High ? wxS("1") : wxS("0")) : wxS("—"),
            left - FromDIP(28), top + FromDIP(10));
        if (trace.samples.empty()) continue;

        dc.SetClippingRegion(left, header, width + 1, std::max(0, size.y - header));
        dc.SetPen(wxPen(Signal, std::max(1, FromDIP(2))));
        const auto next = std::upper_bound(trace.samples.begin(), trace.samples.end(), m_startNs,
            [](long double time, const TraceSample& sample) { return time < sample.timeNs; });
        auto sample = next == trace.samples.begin() ? next : std::prev(next);
        bool hasValue = sample != trace.samples.end() && sample->timeNs <= m_startNs;
        LogicValue previous = hasValue ? sample->value : LogicValue::Low;
        int previousX = left;
        if (hasValue) ++sample;
        for (; sample != trace.samples.end() && sample->timeNs <= visibleEnd; ++sample)
        {
            const int x = left + std::clamp(static_cast<int>(
                (static_cast<long double>(sample->timeNs) - m_startNs) / m_nsPerPixel), 0, width);
            const int oldY = previous == LogicValue::High ? highY : lowY;
            const int newY = sample->value == LogicValue::High ? highY : lowY;
            if (hasValue)
            {
                dc.DrawLine(previousX, oldY, x, oldY);
                if (oldY != newY) dc.DrawLine(x, oldY, x, newY);
            }
            previousX = x;
            previous = sample->value;
            hasValue = true;
        }
        if (hasValue)
        {
            const int endX = left + std::clamp(static_cast<int>((visibleEnd - m_startNs) / m_nsPerPixel), 0, width);
            dc.DrawLine(previousX, previous == LogicValue::High ? highY : lowY,
                endX, previous == LogicValue::High ? highY : lowY);
        }
        dc.DestroyClippingRegion();
    }

    auto drawCursor = [&](const std::optional<std::int64_t>& cursor, const wxColour& colour,
        const wxString& label) {
        if (!cursor || *cursor < m_startNs || *cursor > visibleEnd) return;
        const int x = left + static_cast<int>((static_cast<long double>(*cursor) - m_startNs) / m_nsPerPixel);
        dc.SetPen(wxPen(colour, std::max(1, FromDIP(1)), wxPENSTYLE_SHORT_DASH));
        dc.DrawLine(x, header, x, size.y);
        dc.SetTextForeground(colour);
        dc.DrawText(label, x + FromDIP(4), header + FromDIP(2));
    };
    drawCursor(m_cursorA, CursorA, wxS("A"));
    drawCursor(m_cursorB, CursorB, wxS("B"));
}
