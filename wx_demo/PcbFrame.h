#pragma once

#include <wx/wx.h>
#include <wx/timer.h>
#include "PcbDocument.h"
#include "PcbCanvas.h"
#include <future>
#include <memory>
#include <string>

class wxCheckListBox;
class wxListCtrl;

// An imported board stays independent from the single-driver digital circuit.
// Changes in KiCad are synchronized only after KiCad saves the source file.
class PcbFrame : public wxFrame
{
public:
    explicit PcbFrame(wxWindow* parent, const wxString& path = wxEmptyString);
    ~PcbFrame() override;
    bool LoadBoard(const wxString& path, bool automatic = false);

private:
    friend struct PcbWorkspaceRegressionAccess;
    struct LoadResult
    {
        PcbDocument document;
        wxString path;
        wxString error;
        std::string stamp;
        bool ok = false;
        bool automatic = false;
        bool retry = false;
    };
    static std::string FileStamp(const wxString& path);
    void Poll();
    void Commit(LoadResult result);
    void ChooseBoard();
    void OpenInKiCad();
    void SaveCopy();
    bool WriteCopy(const wxString& path, wxString& error) const;
    void PopulateLists();
    void PopulateNets();
    void PopulateFootprints();
    void ShowSelection(const PcbSelection& selection);
    void SelectNet(int id);
    void SetLoading(bool loading);
    void Report(const wxString& text, bool append = true);

    PcbCanvas* m_canvas = nullptr;
    wxCheckListBox* m_layers = nullptr;
    wxListCtrl* m_nets = nullptr;
    wxListCtrl* m_footprints = nullptr;
    wxTextCtrl* m_netSearch = nullptr;
    wxTextCtrl* m_footprintSearch = nullptr;
    wxTextCtrl* m_inspector = nullptr;
    wxTextCtrl* m_report = nullptr;
    wxStaticText* m_summary = nullptr;
    wxCheckBox* m_autoRefresh = nullptr;
    wxButton *m_importButton = nullptr, *m_reloadButton = nullptr, *m_openButton = nullptr, *m_copyButton = nullptr;
    wxTimer m_timer;
    std::future<LoadResult> m_pending;
    std::shared_ptr<const PcbDocument> m_document;
    std::vector<std::string> m_layerNames;
    std::string m_loadedStamp;
    std::string m_candidateStamp;
    std::string m_failedStamp;
    std::size_t m_pollCount = 0;
};
