#include "InputWaveformDialog.h"
#include "Canvas.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
    long double TickStep(long double approximate)
    {
        approximate = std::max(1.L, approximate);
        const long double power = std::pow(10.L, std::floor(std::log10(approximate)));
        const long double fraction = approximate / power;
        return power * (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10);
    }

    std::int64_t ClampTime(long double value, std::int64_t maximum)
    {
        if (!std::isfinite(static_cast<double>(value))) return 0;
        const long double upper = static_cast<long double>(maximum);
        const long double clamped = std::max(0.L, std::min(upper, value));
        return static_cast<std::int64_t>(std::llround(clamped));
    }
}

int WaveformUi::EffectiveUnit(int unit, std::int64_t durationNs)
{
    if (unit >= 1 && unit <= 4) return unit;
    if (durationNs >= 1000000000LL) return 4;
    if (durationNs >= 1000000LL) return 3;
    if (durationNs >= 1000LL) return 2;
    return 1;
}

long double WaveformUi::Divisor(int unit)
{
    switch (EffectiveUnit(unit, 0))
    {
    case 2: return 1000.L;
    case 3: return 1000000.L;
    case 4: return 1000000000.L;
    default: return 1.L;
    }
}

wxString WaveformUi::UnitName(int unit)
{
    switch (EffectiveUnit(unit, 0))
    {
    case 2: return wxS("us");
    case 3: return wxS("ms");
    case 4: return wxS("s");
    default: return wxS("ns");
    }
}

bool WaveformUi::ParseTime(const wxString& text, int unit, std::int64_t durationNs, std::int64_t& timeNs)
{
    wxString trimmed(text);
    trimmed.Trim(true).Trim(false);
    if (trimmed.empty()) return false;

    // 对话框的默认值由 FormatTime 生成，形如 "100 ns" / "1.5 ms"，带单位后缀。
    // ToDouble 无法直接解析这种字符串，先剥离后缀；显式写出的单位优先于下拉选择。
    static const wxString suffixes[] = {wxS("ns"), wxS("us"), wxS("ms"), wxS("s")};
    static const int suffixUnits[] = {1, 2, 3, 4};
    int parsedUnit = unit;
    for (int i = 0; i < 4; ++i)
    {
        if (trimmed.EndsWith(suffixes[i]))
        {
            parsedUnit = suffixUnits[i];
            trimmed.Truncate(trimmed.Length() - suffixes[i].Length());
            trimmed.Trim(true);
            break;
        }
    }

    double value = 0;
    if (!trimmed.ToDouble(&value) || value < 0 || !std::isfinite(value)) return false;
    const long double ns = static_cast<long double>(value) * Divisor(parsedUnit);
    if (ns > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) return false;
    timeNs = ClampTime(ns, durationNs);
    return true;
}

wxString WaveformUi::FormatTime(std::int64_t timeNs, int unit, std::int64_t durationNs)
{
    const int effective = EffectiveUnit(unit, durationNs);
    return wxString::Format(wxS("%.6g %s"),
        static_cast<double>(static_cast<long double>(timeNs) / Divisor(effective)), UnitName(effective));
}

bool WaveformUi::LevelAt(const Signal& signal, std::int64_t timeNs)
{
    bool level = signal.initialHigh;
    for (const auto& edge : signal.edges)
    {
        if (edge.timeNs > timeNs) break;
        level = edge.high;
    }
    return level;
}

void WaveformUi::Normalize(std::vector<WaveformEdge>& edges, std::int64_t durationNs)
{
    std::sort(edges.begin(), edges.end(), [](const auto& a, const auto& b) { return a.timeNs < b.timeNs; });
    std::vector<WaveformEdge> merged;
    merged.reserve(edges.size());
    for (const auto& edge : edges)
    {
        if (edge.timeNs < 0 || edge.timeNs > durationNs) continue;
        if (!merged.empty() && merged.back().timeNs == edge.timeNs) merged.back() = edge;
        else merged.push_back(edge);
    }
    if (merged.size() > CircuitLimits::MaxWaveformEdges)
        merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(CircuitLimits::MaxWaveformEdges), merged.end());
    edges = std::move(merged);
}

std::vector<WaveformEdge> WaveformUi::MakeSquare(std::int64_t startNs, std::int64_t periodNs,
    int dutyPercent, std::int64_t endNs, bool initialHigh)
{
    std::vector<WaveformEdge> edges;
    if (periodNs < 2 || startNs < 0 || endNs <= startNs) return edges;
    const std::int64_t highNs = std::max<std::int64_t>(1, std::min<std::int64_t>(periodNs - 1,
        static_cast<std::int64_t>(periodNs * std::clamp(dutyPercent, 1, 99) / 100.0)));
    (void)initialHigh;
    for (std::int64_t time = startNs; time <= endNs;)
    {
        edges.push_back({time, true});
        if (time + highNs <= endNs) edges.push_back({time + highNs, false});
        if (periodNs > std::numeric_limits<std::int64_t>::max() - time) break;
        time += periodNs;
        if (edges.size() >= CircuitLimits::MaxWaveformEdges) break;
    }
    return edges;
}

std::vector<WaveformEdge> WaveformUi::MakePulse(std::int64_t startNs, std::int64_t widthNs,
    bool highPulse, bool initialHigh)
{
    (void)initialHigh;
    if (startNs < 0 || widthNs < 1) return {};
    return {{startNs, highPulse}, {startNs + widthNs, !highPulse}};
}

class EdgeDialog : public wxDialog
{
public:
    EdgeDialog(wxWindow* parent, std::int64_t timeNs, bool high, int unit, std::int64_t duration)
        : wxDialog(parent, wxID_ANY, wxS("编辑边沿"), wxDefaultPosition, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, wxS("时间")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        m_time = new wxTextCtrl(this, wxID_ANY, WaveformUi::FormatTime(timeNs, unit, duration));
        row->Add(m_time, 1, wxEXPAND);
        row->Add(new wxStaticText(this, wxID_ANY, WaveformUi::UnitName(unit)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        root->Add(row, 0, wxEXPAND | wxALL, FromDIP(12));
        m_level = new wxChoice(this, wxID_ANY);
        m_level->AppendString(wxS("低电平 (0)"));
        m_level->AppendString(wxS("高电平 (1)"));
        m_level->SetSelection(high ? 1 : 0);
        root->Add(m_level, 0, wxALL, FromDIP(12));
        root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
        SetSizerAndFit(root);
        CentreOnParent();
    }
    bool Get(int unit, std::int64_t duration, std::int64_t& timeNs, bool& high) const
    {
        if (!WaveformUi::ParseTime(m_time->GetValue(), unit, duration, timeNs)) return false;
        high = m_level->GetSelection() == 1;
        return true;
    }
private:
    wxTextCtrl* m_time;
    wxChoice* m_level;
};

class GeneratorDialog : public wxDialog
{
public:
    GeneratorDialog(wxWindow* parent, bool square, int unit, std::int64_t duration)
        : wxDialog(parent, wxID_ANY, square ? wxS("生成方波") : wxS("生成脉冲"),
            wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_square(square), m_unit(unit), m_duration(duration)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
        m_start = AddRow(grid, wxS("起始时间"), WaveformUi::FormatTime(0, unit, duration));
        m_first = AddRow(grid, square ? wxS("周期") : wxS("脉宽"),
            WaveformUi::FormatTime(std::min<std::int64_t>(duration, 1000), unit, duration));
        if (square)
        {
            m_second = AddRow(grid, wxS("高电平时间"), WaveformUi::FormatTime(500, unit, duration));
            m_end = AddRow(grid, wxS("结束时间"), WaveformUi::FormatTime(duration, unit, duration));
        }
        else
        {
            m_level = new wxChoice(this, wxID_ANY);
            m_level->AppendString(wxS("高脉冲"));
            m_level->AppendString(wxS("低脉冲"));
            m_level->SetSelection(0);
            grid->Add(new wxStaticText(this, wxID_ANY, wxS("脉冲类型")), 0, wxALIGN_CENTER_VERTICAL);
            grid->Add(m_level, 1, wxEXPAND);
        }
        root->Add(grid, 0, wxEXPAND | wxALL, FromDIP(12));
        root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
        SetSizerAndFit(root);
        CentreOnParent();
    }
    bool Get(std::vector<WaveformEdge>& edges) const
    {
        std::int64_t start = 0, first = 0, second = 0, end = m_duration;
        if (!WaveformUi::ParseTime(m_start->GetValue(), m_unit, m_duration, start) ||
            !WaveformUi::ParseTime(m_first->GetValue(), m_unit, m_duration, first) ||
            start >= m_duration || first < 1) return false;
        if (m_square)
        {
            if (!WaveformUi::ParseTime(m_second->GetValue(), m_unit, m_duration, second) ||
                !WaveformUi::ParseTime(m_end->GetValue(), m_unit, m_duration, end) || end <= start)
                return false;
            const long double ratio = 100.L * static_cast<long double>(second) / static_cast<long double>(first);
            edges = WaveformUi::MakeSquare(start, first, static_cast<int>(std::llround(ratio)), end, false);
        }
        else
        {
            edges = WaveformUi::MakePulse(start, first, m_level->GetSelection() == 0, false);
            for (auto& edge : edges)
                if (edge.timeNs > m_duration) return false;
        }
        WaveformUi::Normalize(edges, m_duration);
        return !edges.empty();
    }
private:
    wxTextCtrl* AddRow(wxFlexGridSizer* grid, const wxString& label, const wxString& value)
    {
        grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto* input = new wxTextCtrl(this, wxID_ANY, value);
        grid->Add(input, 1, wxEXPAND);
        return input;
    }
    bool m_square;
    int m_unit;
    std::int64_t m_duration;
    wxTextCtrl *m_start = nullptr, *m_first = nullptr, *m_second = nullptr, *m_end = nullptr;
    wxChoice* m_level = nullptr;
};

class WaveformEditCanvas : public wxPanel
{
public:
    WaveformEditCanvas(wxWindow* parent, std::vector<WaveformUi::Signal>& signals,
        std::int64_t duration, int unit)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN)
        , m_signals(signals), m_duration(duration), m_unit(unit)
    {
        SetBackgroundColour(*wxWHITE);
        // 使用 wxAutoBufferedPaintDC 自绘，必须声明由绘制处理器负责背景。
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(FromDIP(wxSize(420, 220)));
        Bind(wxEVT_PAINT, &WaveformEditCanvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, &WaveformEditCanvas::OnLeftDown, this);
        Bind(wxEVT_LEFT_UP, &WaveformEditCanvas::OnLeftUp, this);
        Bind(wxEVT_MOTION, &WaveformEditCanvas::OnMotion, this);
        Bind(wxEVT_RIGHT_DOWN, &WaveformEditCanvas::OnRightDown, this);
        Bind(wxEVT_LEFT_DCLICK, &WaveformEditCanvas::OnDoubleClick, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent&) { Refresh(false); });
    }
    void SetDuration(std::int64_t duration) { m_duration = duration; Refresh(false); }
    void SetUnit(int unit) { m_unit = unit; Refresh(false); }
    std::function<void(int)> onSelectRow;
    std::function<void()> onEdgesChanged;

private:
    int LabelWidth() const { return FromDIP(132); }
    int HeaderHeight() const { return FromDIP(30); }
    int RowHeight() const { return FromDIP(48); }
    int PlotWidth() const { return std::max(1, GetClientSize().x - LabelWidth() - FromDIP(8)); }
    int XAtTime(std::int64_t time) const
    {
        return LabelWidth() + static_cast<int>(static_cast<long double>(time) /
            std::max<std::int64_t>(1, m_duration) * PlotWidth());
    }
    std::int64_t TimeAtX(int x) const
    {
        const long double ratio = static_cast<long double>(std::max(0, x - LabelWidth())) / PlotWidth();
        return ClampTime(ratio * m_duration, m_duration);
    }
    int RowAtY(int y) const
    {
        const int row = (y - HeaderHeight()) / RowHeight();
        return row >= 0 && row < static_cast<int>(m_signals.size()) ? row : -1;
    }
    int EdgeAt(int row, int x) const
    {
        if (row < 0 || row >= static_cast<int>(m_signals.size())) return -1;
        const auto& edges = m_signals[static_cast<std::size_t>(row)].edges;
        for (std::size_t i = 0; i < edges.size(); ++i)
            if (std::abs(XAtTime(edges[i].timeNs) - x) <= FromDIP(6)) return static_cast<int>(i);
        return -1;
    }
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        dc.SetFont(wxFont(wxFontInfo(9).FaceName(wxS("Microsoft YaHei UI"))));
        const int left = LabelWidth(), header = HeaderHeight(), rowH = RowHeight();
        const int width = PlotWidth();
        const int effective = WaveformUi::EffectiveUnit(m_unit, m_duration);

        const long double step = TickStep(static_cast<long double>(FromDIP(120)) / width * m_duration);
        dc.SetPen(wxPen(wxColour(229, 235, 244)));
        dc.SetTextForeground(wxColour(100, 116, 139));
        for (long double tick = 0; tick <= static_cast<long double>(m_duration) + step / 2; tick += step)
        {
            const int x = XAtTime(ClampTime(tick, m_duration));
            dc.DrawLine(x, header / 2, x, GetClientSize().y);
            dc.DrawText(WaveformUi::FormatTime(ClampTime(tick, m_duration), effective, m_duration),
                x + FromDIP(3), FromDIP(4));
        }
        dc.SetPen(wxPen(wxColour(220, 228, 239)));
        dc.DrawLine(0, header, GetClientSize().x, header);

        for (std::size_t row = 0; row < m_signals.size(); ++row)
        {
            const auto& signal = m_signals[row];
            const int top = header + static_cast<int>(row) * rowH;
            const int highY = top + FromDIP(12), lowY = top + rowH - FromDIP(14);
            if (static_cast<int>(row) == m_selectedRow)
            {
                dc.SetBrush(wxBrush(wxColour(235, 243, 255)));
                dc.SetPen(wxPen(wxColour(235, 243, 255)));
                dc.DrawRectangle(0, top, GetClientSize().x, rowH);
            }
            dc.SetPen(wxPen(wxColour(238, 242, 248)));
            dc.DrawLine(left, top + rowH, GetClientSize().x, top + rowH);
            const wxString name = wxString::FromUTF8(signal.type) + wxS(" #") + std::to_wstring(signal.id);
            dc.SetTextForeground(wxColour(36, 50, 71));
            dc.DrawText(name, FromDIP(8), top + rowH / 2 - FromDIP(8));

            std::vector<std::int64_t> points{0};
            for (const auto& edge : signal.edges) points.push_back(edge.timeNs);
            points.push_back(m_duration);
            std::sort(points.begin(), points.end());
            points.erase(std::unique(points.begin(), points.end()), points.end());
            dc.SetPen(wxPen(wxColour(22, 139, 88), 2));
            for (std::size_t i = 0; i + 1 < points.size(); ++i)
            {
                const bool level = WaveformUi::LevelAt(signal, points[i]);
                const int y = level ? highY : lowY;
                dc.DrawLine(XAtTime(points[i]), y, XAtTime(points[i + 1]), y);
                if (i + 1 < points.size() - 1)
                {
                    const bool next = WaveformUi::LevelAt(signal, points[i + 1]);
                    dc.DrawLine(XAtTime(points[i + 1]), y, XAtTime(points[i + 1]), next ? highY : lowY);
                }
            }
            dc.SetBrush(wxBrush(wxColour(22, 139, 88)));
            dc.SetPen(wxPen(wxColour(22, 139, 88)));
            for (std::size_t i = 0; i < signal.edges.size(); ++i)
            {
                const auto& edge = signal.edges[i];
                const bool isDragged = static_cast<int>(row) == m_dragRow && static_cast<int>(i) == m_dragEdge;
                dc.SetBrush(wxBrush(isDragged ? wxColour(203, 112, 30) : wxColour(53, 120, 229)));
                dc.DrawCircle(XAtTime(edge.timeNs), edge.high ? highY : lowY, isDragged ? FromDIP(5) : FromDIP(4));
            }
        }
    }
    void OnLeftDown(wxMouseEvent& event)
    {
        const int row = RowAtY(event.GetY());
        if (event.GetX() < LabelWidth())
        {
            if (row >= 0) { m_selectedRow = row; if (onSelectRow) onSelectRow(row); Refresh(false); }
            return;
        }
        if (row < 0) return;
        m_selectedRow = row;
        if (onSelectRow) onSelectRow(row);
        const int edge = EdgeAt(row, event.GetX());
        if (edge >= 0)
        {
            m_dragRow = row; m_dragEdge = edge;
            CaptureMouse();
            Refresh(false);
            return;
        }
        auto& signal = m_signals[static_cast<std::size_t>(row)];
        const auto time = TimeAtX(event.GetX());
        signal.edges.push_back({time, !WaveformUi::LevelAt(signal, time)});
        WaveformUi::Normalize(signal.edges, m_duration);
        if (onEdgesChanged) onEdgesChanged();
        Refresh(false);
    }
    void OnLeftUp(wxMouseEvent&)
    {
        if (m_dragRow < 0) return;
        auto& signal = m_signals[static_cast<std::size_t>(m_dragRow)];
        WaveformUi::Normalize(signal.edges, m_duration);
        m_dragRow = m_dragEdge = -1;
        if (HasCapture()) ReleaseMouse();
        if (onEdgesChanged) onEdgesChanged();
        Refresh(false);
    }
    void OnMotion(wxMouseEvent& event)
    {
        if (m_dragRow < 0 || !event.Dragging()) return;
        m_signals[static_cast<std::size_t>(m_dragRow)].edges[static_cast<std::size_t>(m_dragEdge)].timeNs =
            TimeAtX(event.GetX());
        Refresh(false);
    }
    void OnRightDown(wxMouseEvent& event)
    {
        const int row = RowAtY(event.GetY());
        const int edge = EdgeAt(row, event.GetX());
        if (row < 0 || edge < 0) return;
        auto& signal = m_signals[static_cast<std::size_t>(row)];
        signal.edges.erase(signal.edges.begin() + edge);
        m_selectedRow = row;
        if (onSelectRow) onSelectRow(row);
        if (onEdgesChanged) onEdgesChanged();
        Refresh(false);
    }
    void OnDoubleClick(wxMouseEvent& event)
    {
        const int row = RowAtY(event.GetY());
        const int edge = EdgeAt(row, event.GetX());
        if (row < 0 || edge < 0) return;
        auto& signal = m_signals[static_cast<std::size_t>(row)];
        auto& item = signal.edges[static_cast<std::size_t>(edge)];
        EdgeDialog dialog(this, item.timeNs, item.high, m_unit, m_duration);
        if (dialog.ShowModal() != wxID_OK) return;
        std::int64_t time = 0;
        bool high = false;
        if (!dialog.Get(m_unit, m_duration, time, high)) return;
        item = {time, high};
        WaveformUi::Normalize(signal.edges, m_duration);
        if (onEdgesChanged) onEdgesChanged();
        Refresh(false);
    }
    std::vector<WaveformUi::Signal>& m_signals;
    std::int64_t m_duration;
    int m_unit;
    int m_selectedRow = -1;
    int m_dragRow = -1, m_dragEdge = -1;
};

InputWaveformDialog::InputWaveformDialog(wxWindow* parent, Canvas* canvas)
    : wxDialog(parent, wxID_ANY, wxS("输入波形编辑器"), wxDefaultPosition, wxDefaultSize,
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_canvas(canvas)
{
    const auto document = canvas->ExportDocument();
    m_durationNs = std::max<std::int64_t>(1, document.simulation.stopTimeNs);
    m_unit = 0;
    for (const auto& data : document.components)
    {
        if (data.type != "INPUT" && data.type != "BUTTON") continue;
        m_signals.push_back({data.id, data.type, data.high, data.waveform});
    }
    m_selectedRow = m_signals.empty() ? -1 : 0;

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(new wxStaticText(this, wxID_ANY,
        wxS("点击波形添加边沿，拖动边沿调整时间，双击精确输入，右键删除。")),
        0, wxALL, FromDIP(10));
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    toolbar->Add(new wxStaticText(this, wxID_ANY, wxS("时间单位")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_unitChoice = new wxChoice(this, wxID_ANY);
    for (const wchar_t* name : {L"自动", L"ns", L"us", L"ms", L"s"}) m_unitChoice->AppendString(name);
    m_unitChoice->SetSelection(0);
    toolbar->Add(m_unitChoice, 0, wxRIGHT, FromDIP(14));
    auto addButton = [this, &toolbar](const wxString& label, std::function<void(wxCommandEvent&)> handler) {
        auto* control = new wxButton(this, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        control->Bind(wxEVT_BUTTON, handler);
        toolbar->Add(control, 0, wxRIGHT, FromDIP(6));
    };
    addButton(wxS("生成方波…"), [this](wxCommandEvent& event) { OnSquare(event); });
    addButton(wxS("生成脉冲…"), [this](wxCommandEvent& event) { OnPulse(event); });
    addButton(wxS("清空全部"), [this](wxCommandEvent& event) { OnClear(event); });
    root->Add(toolbar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

    m_editor = new WaveformEditCanvas(this, m_signals, m_durationNs, m_unit);
    m_editor->onSelectRow = [this](int row) { SetSelectedRow(row); };
    m_editor->onEdgesChanged = [this] { RefreshHints(); };
    root->Add(m_editor, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    auto* horizon = new wxBoxSizer(wxHORIZONTAL);
    horizon->Add(new wxStaticText(this, wxID_ANY, wxS("停止时间")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    m_duration = new wxTextCtrl(this, wxID_ANY, wxString::Format("%lld", static_cast<long long>(m_durationNs)),
        wxDefaultPosition, FromDIP(wxSize(110, -1)));
    horizon->Add(m_duration, 0, wxRIGHT, FromDIP(8));
    horizon->Add(new wxStaticText(this, wxID_ANY, wxS("ns")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    m_durationLabel = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    horizon->Add(m_durationLabel, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(horizon, 0, wxEXPAND | wxALL, FromDIP(10));
    m_hint = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    root->Add(m_hint, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    root->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizer(root);
    m_duration->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        long long value = 0;
        if (m_duration->GetValue().ToLongLong(&value) && value > 0)
        {
            m_durationNs = value;
            m_editor->SetDuration(m_durationNs);
            RefreshDurationLabel();
        }
    });
    m_unitChoice->Bind(wxEVT_CHOICE, &InputWaveformDialog::OnUnitChanged, this);
    Bind(wxEVT_BUTTON, &InputWaveformDialog::OnOk, this, wxID_OK);
    SetMinSize(FromDIP(wxSize(640, 480)));
    SetSize(FromDIP(wxSize(760, 560)));
    CentreOnParent();
    RefreshDurationLabel();
    RefreshHints();
}

void InputWaveformDialog::SetSelectedRow(int row)
{
    m_selectedRow = row;
    RefreshHints();
}

void InputWaveformDialog::RefreshDurationLabel()
{
    const int effective = WaveformUi::EffectiveUnit(m_unit, m_durationNs);
    m_durationLabel->SetLabel(wxString::Format(wxS("即 %s"),
        WaveformUi::FormatTime(m_durationNs, effective, m_durationNs)));
}

void InputWaveformDialog::RefreshHints()
{
    if (m_signals.empty())
    {
        m_hint->SetLabel(wxS("当前电路没有 INPUT 或 BUTTON。请先在画布上放置输入元件。"));
        return;
    }
    const int row = std::clamp(m_selectedRow, 0, static_cast<int>(m_signals.size()) - 1);
    const auto& signal = m_signals[static_cast<std::size_t>(row)];
    const wxString name = wxString::FromUTF8(signal.type) + wxS(" #") + std::to_wstring(signal.id);
    m_hint->SetLabel(wxString::Format(wxS("当前编辑：%s · %d 个边沿 · 初值 %d"),
        name, static_cast<int>(signal.edges.size()), signal.initialHigh ? 1 : 0));
}

void InputWaveformDialog::OnUnitChanged(wxCommandEvent&)
{
    m_unit = m_unitChoice->GetSelection();
    m_editor->SetUnit(WaveformUi::EffectiveUnit(m_unit, m_durationNs));
    RefreshDurationLabel();
}

void InputWaveformDialog::OnSquare(wxCommandEvent&)
{
    if (m_signals.empty()) { wxMessageBox(wxS("请先放置 INPUT 或 BUTTON。"), wxS("输入波形"), wxOK | wxICON_INFORMATION, this); return; }
    if (m_selectedRow < 0) m_selectedRow = 0;
    GeneratorDialog dialog(this, true, WaveformUi::EffectiveUnit(m_unit, m_durationNs), m_durationNs);
    if (dialog.ShowModal() != wxID_OK) return;
    std::vector<WaveformEdge> edges;
    if (!dialog.Get(edges) || !edges.empty() && edges.back().timeNs > m_durationNs)
    { wxMessageBox(wxS("周期、高电平时间或结束时间无效。"), wxS("输入波形"), wxOK | wxICON_WARNING, this); return; }
    m_signals[static_cast<std::size_t>(m_selectedRow)].edges = std::move(edges);
    RefreshHints();
    m_editor->Refresh(false);
}

void InputWaveformDialog::OnPulse(wxCommandEvent&)
{
    if (m_signals.empty()) { wxMessageBox(wxS("请先放置 INPUT 或 BUTTON。"), wxS("输入波形"), wxOK | wxICON_INFORMATION, this); return; }
    if (m_selectedRow < 0) m_selectedRow = 0;
    GeneratorDialog dialog(this, false, WaveformUi::EffectiveUnit(m_unit, m_durationNs), m_durationNs);
    if (dialog.ShowModal() != wxID_OK) return;
    std::vector<WaveformEdge> edges;
    if (!dialog.Get(edges) || edges.empty())
    { wxMessageBox(wxS("起始时间或脉宽无效。"), wxS("输入波形"), wxOK | wxICON_WARNING, this); return; }
    m_signals[static_cast<std::size_t>(m_selectedRow)].edges = std::move(edges);
    RefreshHints();
    m_editor->Refresh(false);
}

void InputWaveformDialog::OnClear(wxCommandEvent&)
{
    for (auto& signal : m_signals) signal.edges.clear();
    RefreshHints();
    m_editor->Refresh(false);
}

bool InputWaveformDialog::Apply(wxString& error)
{
    long long value = 0;
    wxString durationText(m_duration->GetValue());
    durationText.Trim(true);
    durationText.Trim(false);
    if (!durationText.ToLongLong(&value) || value < 0)
    {
        error = wxS("停止时间必须是非负整数纳秒。");
        return false;
    }
    auto document = m_canvas->ExportDocument();
    document.simulation.stopTimeNs = value;
    for (auto& data : document.components)
    {
        if (data.type != "INPUT" && data.type != "BUTTON") { data.waveform.clear(); continue; }
        data.waveform.clear();
        for (const auto& signal : m_signals)
            if (signal.id == data.id)
            {
                data.waveform = signal.edges;
                WaveformUi::Normalize(data.waveform, value);
                break;
            }
    }
    return m_canvas->ApplyWaveformDocument(document, error);
}

void InputWaveformDialog::OnOk(wxCommandEvent&)
{
    wxString error;
    if (!Apply(error))
    {
        const wxString message = error.empty() ? wxString(wxS("无法应用输入波形。")) : error;
        wxMessageBox(message, wxS("输入波形"), wxOK | wxICON_WARNING, this);
        return;
    }
    EndModal(wxID_OK);
}
