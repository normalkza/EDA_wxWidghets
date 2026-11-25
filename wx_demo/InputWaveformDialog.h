#pragma once

#include <wx/wx.h>
#include "CircuitDocument.h"
#include <string>
#include <vector>

class Canvas;

// Shared helpers for the waveform editor. Kept free of wx controls so the
// time conversion and waveform generators can be regression tested directly.
namespace WaveformUi
{
    // 0 = automatic, 1 = ns, 2 = us, 3 = ms, 4 = s.
    int EffectiveUnit(int unit, std::int64_t durationNs);
    long double Divisor(int unit);
    wxString UnitName(int unit);
    bool ParseTime(const wxString& text, int unit, std::int64_t durationNs, std::int64_t& timeNs);
    wxString FormatTime(std::int64_t timeNs, int unit, std::int64_t durationNs);

    struct Signal
    {
        int id = 0;
        std::string type;
        bool initialHigh = false;
        std::vector<WaveformEdge> edges;
    };

    bool LevelAt(const Signal& signal, std::int64_t timeNs);
    void Normalize(std::vector<WaveformEdge>& edges, std::int64_t durationNs);
    std::vector<WaveformEdge> MakeSquare(std::int64_t startNs, std::int64_t periodNs,
        int dutyPercent, std::int64_t endNs, bool initialHigh);
    std::vector<WaveformEdge> MakePulse(std::int64_t startNs, std::int64_t widthNs,
        bool highPulse, bool initialHigh);
}

class InputWaveformDialog : public wxDialog
{
public:
    InputWaveformDialog(wxWindow* parent, Canvas* canvas);

    const std::vector<WaveformUi::Signal>& GetSignals() const { return m_signals; }
    std::int64_t GetDurationNs() const { return m_durationNs; }

private:
    friend struct WaveformEditorRegressionAccess;
    void OnUnitChanged(wxCommandEvent& event);
    void OnSquare(wxCommandEvent& event);
    void OnPulse(wxCommandEvent& event);
    void OnClear(wxCommandEvent& event);
    void OnOk(wxCommandEvent& event);
    void RefreshDurationLabel();
    void RefreshHints();
    bool Apply(wxString& error);
    int SelectedRow() const { return m_selectedRow; }
    void SetSelectedRow(int row);

    Canvas* m_canvas;
    std::vector<WaveformUi::Signal> m_signals;
    std::int64_t m_durationNs;
    int m_unit;
    int m_selectedRow;
    class WaveformEditCanvas* m_editor;
    wxChoice* m_unitChoice;
    wxTextCtrl* m_duration;
    wxStaticText* m_durationLabel;
    wxStaticText* m_hint;
};
