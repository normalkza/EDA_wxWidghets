#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/timer.h>
#include <vector>
#include <array>

#include "Component.h"
#include <functional>
#include <utility>
#include <string>

class wxClipboard;


class Canvas : public wxPanel
{
public:
    Canvas(wxWindow* parent, wxClipboard* clipboard = nullptr);
    bool HandleEditCommand(int command);
    void OnToolChanged();
    void SetTextFontSize(int pointSize);
    int GetTextFontSize() const;
    void SetTextSelectionCallback(std::function<void(const wxRect&, int)> callback)
    {
        m_textSelectionCallback = std::move(callback);
    }
    void SetSelectionCallback(std::function<void(const Component*, int, int)> callback)
    {
        m_selectionCallback = std::move(callback);
    }
    void SetComponentPlacedCallback(std::function<void()> callback)
    {
        m_componentPlacedCallback = std::move(callback);
    }


private:
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

    // 把坐标对齐到网格点
    int SnapToGrid(int value) const { return (value / m_gridSize) * m_gridSize; }

    // 六种门电路的绘制函数
    void DrawAndGate(wxDC& dc, int x, int y);
    void DrawOrGate(wxDC& dc, int x, int y);
    void DrawNotGate(wxDC& dc, int x, int y);
    void DrawNandGate(wxDC& dc, int x, int y);
    void DrawNorGate(wxDC& dc, int x, int y);
    void DrawXorGate(wxDC& dc, int x, int y);
    void DrawInput(wxDC& dc, int x, int y, LogicValue value);
    void DrawOutput(wxDC& dc, int x, int y, LogicValue value);

    // 画布上的元件
    struct PlacedComponent
    {
        Component* comp;
        int x, y;
    };
    int HitTest(int x, int y) const;
    wxRect GetComponentRect(const PlacedComponent& pc) const;
    std::vector<PlacedComponent> m_components;
    int m_selectedIndex = -1;
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
    std::function<void()> m_componentPlacedCallback;
    wxClipboard* m_clipboard;
    std::string m_lastPastePayload;
    int m_pasteCount = 0;
    struct TextBox
    {
        // 原生控件只负责键盘、剪贴板和输入法，放在画布外；画布绘制透明文字。
        wxTextCtrl* editor;
        int pointSize;
        wxRect rect;
        int scrollY = 0;
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
    int m_textTransformHandle = -1; // 0..7 为八个缩放手柄，8 为移动。
    wxRect m_textTransformRect;
    wxPoint m_textTransformStart;
    bool m_textTransformMoved = false;
    int m_textTransformScrollY = 0;


    // 网格
    int m_gridSize = 20;
    wxColour m_gridColour = wxColour(220, 220, 220);
    wxBitmap m_gridBitmap;

    wxDECLARE_EVENT_TABLE();
};
