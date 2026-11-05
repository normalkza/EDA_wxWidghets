#pragma once

#include <wx/wx.h>
#include "TraceRecorder.h"
#include <cstdint>
#include <optional>
#include <vector>

class wxScrollBar;
class wxStaticText;
class wxPaintEvent;
class wxMouseEvent;

// A value-owned view of one simulation revision. No solver or canvas pointers
// are retained, so document edits can safely discard the running simulation.
class WaveformPanel : public wxPanel
{
public:
    explicit WaveformPanel(wxWindow* parent);

    void SetTraces(const std::vector<DigitalTrace>& traces, std::int64_t currentTimeNs);
    void Clear();

private:
    void Paint(wxPaintEvent& event);
    void OnMouse(wxMouseEvent& event);
    void Zoom(long double factor, int anchorX = -1);
    void Fit();
    void UpdateScrollbars();
    void UpdateCaption();
    void RefreshView();
    int PlotLeft() const;
    int PlotWidth() const;
    int RowHeight() const;
    int HeaderHeight() const;
    std::int64_t TimeAtX(int x) const;
    std::optional<LogicValue> ValueAt(const DigitalTrace& trace, std::int64_t time) const;

    wxPanel* m_plot = nullptr;
    wxScrollBar* m_horizontal = nullptr;
    wxScrollBar* m_vertical = nullptr;
    wxStaticText* m_caption = nullptr;
    std::vector<DigitalTrace> m_traces;
    std::int64_t m_currentTimeNs = 0;
    std::optional<std::int64_t> m_cursorA;
    std::optional<std::int64_t> m_cursorB;
    long double m_extentNs = 1;
    long double m_startNs = 0;
    long double m_nsPerPixel = 1;
    int m_firstRow = 0;
    bool m_autoFit = true;
    bool m_syncingScrollbars = false;
};
