#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <vector>
#include <array>

#include "Component.h"
#include "Circuit.h"
#include "CircuitFile.h"
#include "Wire.h"
#include "SimulationSnapshot.h"
#include "CircuitDocument.h"

#include <optional>
#include <functional>
#include <utility>
#include <string>

class wxClipboard;

class Canvas : public wxPanel
{
    struct PlacedComponent
    {
        Component* comp;
        int x, y;
        int id = 0;
        int rotation = 0;
        std::optional<bool> initialHigh;
        CircuitComponentData parameters;

        // 教程中自动显示的引脚名称
        wxString tutorialLabel;
    };

public:
    Canvas(wxWindow* parent, wxClipboard* clipboard = nullptr);
    ~Canvas() override;
    bool HandleEditCommand(int command);
    bool Undo();
    bool CanUndo() const;
    bool RotateSelection(int degrees = 90);
    bool HasSelectedComponent() const;
    bool HasSelectedInput() const;
    int GetSelectedRotation() const;
    bool ToggleSelectedInput();
    bool EditSelectedTimingParameters();
    bool CanEditTimingParameters() const;
    std::optional<CircuitComponentData> GetSelectedComponentData() const;
    bool SetSimulationConfig(const SimulationConfig& config, wxString& error);
    void SetDocumentChangedCallback(std::function<void()> callback) { m_documentChangedCallback = std::move(callback); }
    void SetRuntimeInputCallback(std::function<bool(int)> callback) { m_runtimeInputCallback = std::move(callback); }
    void EvaluateCircuit();
    std::unique_ptr<SimulationSnapshot> CreateSimulationSnapshot(wxString& error) const;
    bool ApplySimulationResult(const SimulationResult& result);
    std::uint64_t GetDocumentRevision() const { return m_documentRevision; }
    void DeselectAll();
    void SetGridVisible(bool visible);
    bool IsGridVisible() const { return m_gridVisible; }
    void SetSnapEnabled(bool enabled);
    bool IsSnapEnabled() const { return m_snapEnabled; }
    bool ResetSelectedWireRoute();
    bool HasSelectedWire() const;
    bool CanResetWireRoute() const;
    void OnToolChanged();
    void SetTextFontSize(int pointSize);
    int GetTextFontSize() const;
    bool SetTutorialLabel(int componentId, const wxString& label);
    void SetGateInputCount(int count);
    int GetGateInputCount() const;
    CircuitDocument ExportDocument() const;
    bool ImportDocument(const CircuitDocument& document, wxString& error);
    void ClearDocument();
    bool IsModified() const { return m_modified; }
    void MarkSaved();
    void SetModifiedCallback(std::function<void()> callback) { m_modifiedCallback = std::move(callback); }
    void SetStatusCallback(std::function<void(const wxString&)> callback) { m_statusCallback = std::move(callback); }
    void SetWireSelectionCallback(std::function<void(const WireConnection*)> callback) { m_wireSelectionCallback = std::move(callback); }
    void SetTextSelectionCallback(std::function<void(const wxRect&, int)> callback) { m_textSelectionCallback = std::move(callback); }
    void SetSelectionCallback(std::function<void(const Component*, int, int)> callback) { m_selectionCallback = std::move(callback); }
    void SetComponentPlacedCallback(
        std::function<void(const wxString&, int)> callback)
    {
        m_componentPlacedCallback = std::move(callback);
    }
    void SetWireCreatedCallback(std::function<void(const CircuitWireData&)> callback)
    {
        m_wireCreatedCallback = std::move(callback);
    }
    Circuit& GetCircuit() { return m_circuit; }
    const std::vector<Canvas::PlacedComponent>& GetComponents() const { return m_components; }

private:
    friend struct CanvasRegressionAccess;
    friend struct CanvasStructureRegressionAccess;
    friend struct CanvasTimingRegressionAccess;
    void MarkModified(bool textTransform = false);
    void RefreshModifiedState();
    void UndoText(wxTextCtrl& editor);
    void FinishTextEditing();
    void RecordUndo();
    void RecordUndo(CircuitDocument document);
    bool ReplaceDocument(const CircuitDocument& document, wxString& error, bool resetHistory);
    void ResetDocument();
    int NextComponentId() const;
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnDoubleClick(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnDragTimer(wxTimerEvent& event);
    void MoveDraggedComponent(const wxPoint& mouse, bool snap);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void DeleteAtPoint(const wxPoint& point);
    void DeleteItem(int componentIndex, int textIndex);
    bool DeleteSelection();
    bool CopySelection();
    bool PasteSelection();
    bool CutSelection();
    bool IsEditingText() const;
    void CancelMouseInteraction(bool restoreText = false);
    void CreateTextBox(const wxRect& rect);
    void NotifyTextSelection();
    wxRect GetPendingTextRect() const;
    void DrawCanvas(wxDC& dc, const wxRect& updateRect);
    void DrawTextBoxes(wxDC& dc, const wxRect& updateRect);
    void UpdateGridBitmap();
    int HitTestText(const wxPoint& point, bool entireBox) const;
    void EditTextBox(int index, const wxPoint& point);
    void UpdateTextInput();
    std::array<wxPoint, 8> GetTextHandles(const wxRect& rect) const;
    int HitTestTextHandle(const wxPoint& point) const;
    bool IsTextBorder(const wxPoint& point) const;
    void BeginTextTransform(int index, int handle, const wxPoint& point);
    void UpdateTextTransform(const wxPoint& point);
    void UpdateTextCursor(const wxPoint& point);
    void DrawTextSelection(wxDC& dc);
    std::function<void()> m_documentChangedCallback;
    // 把坐标对齐到网格点
    int SnapToGrid(int value) const { return m_snapEnabled ? (value / m_gridSize) * m_gridSize : value; }

    static constexpr int m_portSize = 40;
    void DrawPort(wxDC& dc, int x, int y, LogicValue value, PinType direction, int rotation = 0);
    void DrawInput(wxDC& dc, int x, int y, LogicValue value);
    void DrawOutput(wxDC& dc, int x, int y, LogicValue value);

    // 绘制、点击判定和局部刷新统一使用同一份几何信息。
    struct GateGeometry
    {
        int width, height, left, outputX;
        std::vector<wxPoint> inputs;
        std::vector<wxPoint> outputs;
    };
    GateGeometry GetGateGeometry(const Component& component) const;
    wxPoint GetComponentCenter(const PlacedComponent& pc) const;
    wxPoint ComponentToWorld(const PlacedComponent& pc, const wxPoint& point) const;
    wxPoint WorldToComponent(const PlacedComponent& pc, const wxPoint& point) const;
    void DrawPlacedComponent(wxDC& dc, const PlacedComponent& pc);
    bool CanPersistWireGeometry() const;
    void DrawGate(wxDC& dc, const PlacedComponent& pc);
    int HitTest(int x, int y) const;
    wxRect GetComponentRect(const PlacedComponent& pc) const;
    wxRect GetComponentUpdateRect(const PlacedComponent& pc) const;
    std::optional<PinReference> HitTestPin(const wxPoint& point) const;
    wxPoint GetPinPosition(const PinReference& pin) const;
    wxPoint GetPinDirection(const PinReference& pin) const;
    Wire::Path GetWirePath(const WireConnection& connection) const;
    int HitTestWire(const wxPoint& point) const;
    void HandleWireClick(const wxPoint& point);
    void CancelPendingWire();
    void DrawWires(wxDC& dc, const wxRect& updateRect);
    void SelectWire(int index);
    void DeleteWire(int index);
    bool RecomputeCircuit();
    void ReportStatus(const wxString& message);
    bool BeginWireDrag(const wxPoint& point);
    void UpdateWireDrag(const wxPoint& point);
    void FinishWireDrag(bool cancel = false);
    bool IsDraggingWire() const { return m_wireDragIndex >= 0; }
    int m_wireDragIndex = -1;
    int m_wireDragSegment = -1;
    wxPoint m_wireDragStart;
    Wire::Path m_wireDragPath;
    Wire::Path m_wireDragOriginalRoute;
    std::optional<CircuitDocument> m_wireDragUndoDocument;
    bool m_wireDragMoved = false;
    Circuit m_circuit;
    std::uint64_t m_documentRevision = 1;
    int m_formatVersion = CircuitFile::CurrentVersion;
    SimulationConfig m_simulationConfig;
    std::function<void(const CircuitWireData&)> m_wireCreatedCallback;
    std::function<bool(int)> m_runtimeInputCallback;
    std::optional<PinReference> m_pendingWire;
    wxPoint m_wirePreview;
    int m_selectedWireIndex = -1;
    std::function<void(const wxString&)> m_statusCallback;
    std::function<void(const WireConnection*)> m_wireSelectionCallback;
    std::vector<PlacedComponent> m_components;
    int m_selectedIndex = -1;
    int m_defaultGateInputCount = 2;
    int m_pressedIndex = -1;
    bool m_dragging = false;
    bool m_togglePending = false;
    wxPoint m_pressPosition;
    static constexpr int m_dragThreshold = 4;
    wxPoint m_dragOffset;
    wxPoint m_dragPosition;
    wxTimer m_dragTimer;
    int m_dragFrame = 0;
    std::function<void(const Component*, int, int)> m_selectionCallback;
    std::function<void(const wxString&, int)> m_componentPlacedCallback;

    std::function<void()> m_modifiedCallback;
    bool m_modified = false;
    bool m_loadingDocument = false;
    static constexpr std::size_t m_undoLimit = 50;
    std::vector<CircuitDocument> m_undoHistory;
    CircuitDocument m_savedDocument;
    std::optional<CircuitDocument> m_dragUndoDocument;
    std::optional<CircuitDocument> m_textTransformUndoDocument;
    std::optional<CircuitDocument> m_textEditUndoDocument;
    bool m_nativeTextUndo = false;
    wxClipboard* m_clipboard;
    std::string m_lastPastePayload;
    int m_pasteCount = 0;
    struct TextBox
    {
        wxTextCtrl* editor;
        int pointSize;
        wxRect rect;
        int scrollY = 0;
        wxString lastValue;
    };
    struct TextLine
    {
        wxString text;
        long start;
        wxPoint origin;
        int height;
    };
    std::vector<TextLine> LayoutText(const TextBox& box, wxDC& dc) const;
    std::vector<TextBox> m_textBoxes;
    int m_selectedTextIndex = -1;
    int m_defaultTextPointSize = 14;
    bool m_creatingText = false;
    wxPoint m_textStart;
    wxPoint m_textEnd;
    std::function<void(const wxRect&, int)> m_textSelectionCallback;
    wxTimer m_textTimer;
    int m_textTransformIndex = -1;
    int m_textTransformHandle = -1;
    wxRect m_textTransformRect;
    wxPoint m_textTransformStart;
    bool m_textTransformMoved = false;
    bool m_textTransformWasModified = false;
    int m_textTransformScrollY = 0;

    int m_gridSize = 20;
    bool m_gridVisible = true;
    bool m_snapEnabled = true;
    wxColour m_gridColour = wxColour(220, 228, 239);
    wxBitmap m_gridBitmap;

    wxDECLARE_EVENT_TABLE();
};
