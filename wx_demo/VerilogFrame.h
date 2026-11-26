#pragma once

#include "VerilogRunner.h"
#include <wx/frame.h>
#include <wx/timer.h>
#include <wx/fdrepdlg.h>
#include <wx/weakref.h>
#include <atomic>
#include <future>
#include <cstdint>
#include <memory>

class wxStyledTextCtrl;
class wxNotebook;
class wxListCtrl;
class wxTextCtrl;
class wxChoice;
class wxSpinCtrl;
class wxButton;

class VerilogFrame final : public wxFrame
{
public:
    explicit VerilogFrame(wxWindow* parent);
    ~VerilogFrame() override;
    bool AddFromCircuit(const CircuitDocument& document, wxString& error);
    bool LoadSource(const wxString& path, wxString& error);
    bool PrepareClose();

private:
    friend struct VerilogRegressionAccess;
    struct Document
    {
        unsigned id = 0;
        wxStyledTextCtrl* editor = nullptr;
        wxString path;
        wxString name;
    };
    Document* Current() const;
    Document* Find(unsigned id) const;
    Document* AddDocument(const wxString& name, const wxString& code = wxString(), const wxString& path = wxString());
    void RefreshTitles();
    void UpdateActions();
    void ChooseSources();
    bool Save(Document& document, bool saveAs = false);
    bool ConfirmSave(Document& document);
    void CloseTab();
    void InsertTemplate(int kind);
    void ConfigureTools();
    void Build(bool simulate);
    void Poll();
    void Stop();
    void SaveWave();
    void ShowFind(bool replace);
    void FindText(wxFindDialogEvent& event);
    void JumpToDiagnostic(long row);
    void Report(const wxString& text);

    std::vector<std::unique_ptr<Document>> m_documents;
    wxNotebook* m_notebook = nullptr;
    wxTextCtrl* m_output = nullptr;
    wxListCtrl* m_diagnostics = nullptr;
    wxTextCtrl* m_top = nullptr;
    wxChoice* m_scope = nullptr;
    wxSpinCtrl* m_timeout = nullptr;
    wxButton *m_check = nullptr, *m_run = nullptr, *m_stop = nullptr, *m_tools = nullptr, *m_wave = nullptr;
    wxTimer m_timer;
    wxFindReplaceData m_findData;
    wxWeakRef<wxFindReplaceDialog> m_find;
    std::future<VerilogRunner::Result> m_task;
    std::shared_ptr<std::atomic_bool> m_cancel;
    VerilogRunner::Result m_lastResult;
    std::uint64_t m_generation = 0, m_buildGeneration = 0;
    unsigned m_nextId = 1;
    wxString m_compiler, m_runtime;
};
