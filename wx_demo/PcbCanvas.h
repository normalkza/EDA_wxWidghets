#pragma once

#include <wx/wx.h>
#include <wx/geometry.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct PcbDocument;

struct PcbSelection
{
    enum class Kind { None, Footprint, Pad, Track, Via, Graphic, Zone };
    Kind kind = Kind::None;
    // Pad / footprint graphics: index is the parent footprint, subIndex is
    // the child. Other selections use index in the corresponding board array.
    std::size_t index = 0;
    std::size_t subIndex = 0;
    bool footprintGraphic = false;
    bool operator==(const PcbSelection&) const = default;
};

// A separate, value-owned PCB view. Electrical board nets never enter the
// digital single-driver simulation model.
class PcbCanvas : public wxPanel
{
public:
    explicit PcbCanvas(wxWindow* parent);
    void SetDocument(std::shared_ptr<const PcbDocument> document, bool preserveView = false);
    const std::shared_ptr<const PcbDocument>& GetDocument() const { return m_document; }
    void SetLayerVisible(const std::string& layer, bool visible);
    bool IsLayerVisible(const std::string& layer) const;
    void ShowAllLayers(bool visible = true);
    void SetHighlightedNet(std::optional<int> net);
    void Fit();
    // factor > 1 zooms in; wheel zoom keeps the world point under the cursor.
    void Zoom(double factor);
    void ResetView() { Fit(); }
    const PcbSelection& GetSelection() const { return m_selection; }
    wxPoint2DDouble ScreenToWorld(const wxPoint& screen) const;
    wxPoint WorldToScreen(const wxPoint2DDouble& world) const;
    void SetSelectionCallback(std::function<void(const PcbSelection&)> callback)
    { m_selectionCallback = std::move(callback); }
    void SetStatusCallback(std::function<void(const wxString&)> callback)
    { m_statusCallback = std::move(callback); }

private:
    friend struct PcbViewRegressionAccess;
    struct Item
    {
        enum class Shape { Polyline, Polygon, Text, Marker };
        Shape shape = Shape::Polyline;
        std::vector<wxPoint2DDouble> points;
        std::vector<wxPoint2DDouble> hitPolygon;
        std::vector<std::string> layers;
        PcbSelection selection;
        int net = 0;
        double width = .15;
        double textHeight = 1;
        double textAngle = 0;
        wxString text;
        wxRect2DDouble bounds;
        bool filled = false;
        bool zone = false;
        bool hole = false;
        bool selectionOnly = false;
        bool approximate = false;
    };
    void RebuildItems();
    void Paint(wxPaintEvent& event);
    void Draw(wxDC& dc);
    void OnMouse(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);
    void ZoomAt(double factor, const wxPoint& anchor);
    bool ItemVisible(const Item& item) const;
    wxColour ItemColour(const Item& item, bool selected) const;
    std::optional<PcbSelection> HitTest(const wxPoint& screen) const;
    void Select(const PcbSelection& selection);
    void ReportPosition(const wxPoint& screen);
    std::shared_ptr<const PcbDocument> m_document;
    std::vector<Item> m_items;
    std::unordered_set<std::string> m_hiddenLayers;
    std::optional<int> m_highlightedNet;
    PcbSelection m_selection;
    std::function<void(const PcbSelection&)> m_selectionCallback;
    std::function<void(const wxString&)> m_statusCallback;
    wxRect2DDouble m_boardBounds;
    wxPoint2DDouble m_center;
    double m_scale = 10;
    bool m_fitted = true;
    bool m_panning = false;
    wxPoint m_panStart;
    wxPoint2DDouble m_panCenter;
};
