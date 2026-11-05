#include "PcbCanvas.h"
#include "PcbDocument.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr double Pi = 3.14159265358979323846;
const wxColour Background(22, 29, 40);
const wxColour Selection(255, 222, 86);

wxPoint2DDouble Point(PcbPoint p) { return {p.x, p.y}; }
double Distance(wxPoint2DDouble a, wxPoint2DDouble b) { return std::hypot(a.m_x - b.m_x, a.m_y - b.m_y); }
wxPoint2DDouble Rotate(wxPoint2DDouble p, double angle)
{
    const double a = angle * Pi / 180;
    return {p.m_x * std::cos(a) + p.m_y * std::sin(a), -p.m_x * std::sin(a) + p.m_y * std::cos(a)};
}
double SegmentDistance(wxPoint2DDouble p, wxPoint2DDouble a, wxPoint2DDouble b)
{
    const double dx = b.m_x - a.m_x, dy = b.m_y - a.m_y;
    const double length = dx * dx + dy * dy;
    if (length <= 1e-24) return Distance(p, a);
    const double t = std::clamp(((p.m_x - a.m_x) * dx + (p.m_y - a.m_y) * dy) / length, 0., 1.);
    return Distance(p, {a.m_x + t * dx, a.m_y + t * dy});
}
bool Inside(wxPoint2DDouble p, const std::vector<wxPoint2DDouble>& polygon)
{
    if (polygon.size() < 3) return false;
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const auto& a = polygon[i]; const auto& b = polygon[j];
        if ((a.m_y > p.m_y) != (b.m_y > p.m_y) &&
            p.m_x < (b.m_x - a.m_x) * (p.m_y - a.m_y) / (b.m_y - a.m_y) + a.m_x)
            inside = !inside;
    }
    return inside;
}
wxRect2DDouble Bounds(const std::vector<wxPoint2DDouble>& points, double margin = 0)
{
    if (points.empty()) return {};
    double x = points[0].m_x, y = points[0].m_y, right = x, bottom = y;
    for (const auto& p : points)
    { x = std::min(x, p.m_x); y = std::min(y, p.m_y); right = std::max(right, p.m_x); bottom = std::max(bottom, p.m_y); }
    return {x - margin, y - margin, right - x + margin * 2, bottom - y + margin * 2};
}
bool Intersects(const wxRect2DDouble& a, const wxRect2DDouble& b)
{
    return a.m_x <= b.m_x + b.m_width && a.m_x + a.m_width >= b.m_x &&
        a.m_y <= b.m_y + b.m_height && a.m_y + a.m_height >= b.m_y;
}
std::vector<wxPoint2DDouble> Ellipse(wxPoint2DDouble center, double width, double height, double angle = 0)
{
    std::vector<wxPoint2DDouble> result;
    constexpr int count = 64;
    for (int i = 0; i < count; ++i)
    {
        auto p = Rotate({width / 2 * std::cos(i * Pi * 2 / count), height / 2 * std::sin(i * Pi * 2 / count)}, angle);
        result.push_back({center.m_x + p.m_x, center.m_y + p.m_y});
    }
    return result;
}
std::vector<wxPoint2DDouble> RoundedRectangle(wxPoint2DDouble center, double width, double height, double angle, double radius)
{
    radius = std::clamp(radius, 0., std::min(width, height) / 2);
    std::vector<wxPoint2DDouble> result;
    const double x = width / 2, y = height / 2;
    for (int corner = 0; corner < 4; ++corner)
    {
        const std::array<wxPoint2DDouble, 4> centres{{{x - radius, y - radius}, {-x + radius, y - radius},
            {-x + radius, -y + radius}, {x - radius, -y + radius}}};
        const int steps = radius > 0 ? 8 : 1;
        for (int step = 0; step <= steps; ++step)
        {
            const double a = (corner * 90. + step * 90. / steps) * Pi / 180;
            auto p = Rotate({centres[corner].m_x + radius * std::cos(a), centres[corner].m_y + radius * std::sin(a)}, angle);
            result.push_back({center.m_x + p.m_x, center.m_y + p.m_y});
        }
    }
    return result;
}
std::vector<wxPoint2DDouble> Arc(PcbPoint start, PcbPoint middle, PcbPoint end)
{
    const double determinant = 2 * (start.x * (middle.y - end.y) + middle.x * (end.y - start.y) + end.x * (start.y - middle.y));
    if (std::fabs(determinant) < 1e-12) return {Point(start), Point(middle), Point(end)};
    const double a = start.x * start.x + start.y * start.y;
    const double b = middle.x * middle.x + middle.y * middle.y;
    const double c = end.x * end.x + end.y * end.y;
    const wxPoint2DDouble center{(a * (middle.y - end.y) + b * (end.y - start.y) + c * (start.y - middle.y)) / determinant,
        (a * (end.x - middle.x) + b * (start.x - end.x) + c * (middle.x - start.x)) / determinant};
    const double from = std::atan2(start.y - center.m_y, start.x - center.m_x);
    const auto positive = [](double v) { v = std::fmod(v, Pi * 2); return v < 0 ? v + Pi * 2 : v; };
    const double through = positive(std::atan2(middle.y - center.m_y, middle.x - center.m_x) - from);
    double sweep = positive(std::atan2(end.y - center.m_y, end.x - center.m_x) - from);
    if (through > sweep + 1e-10) sweep -= Pi * 2;
    const double radius = Distance(Point(start), center);
    const int count = std::clamp(static_cast<int>(std::ceil(std::fabs(sweep) * 24)), 8, 160);
    std::vector<wxPoint2DDouble> points;
    for (int i = 0; i <= count; ++i)
    { const double t = from + sweep * i / count; points.push_back({center.m_x + radius * std::cos(t), center.m_y + radius * std::sin(t)}); }
    return points;
}
bool Copper(const std::string& name) { return name.ends_with(".Cu"); }
std::vector<std::string> PadLayers(const std::vector<std::string>& source)
{
    std::vector<std::string> result;
    for (const auto& layer : source) if (Copper(layer)) result.push_back(layer);
    return result.empty() ? source : result;
}
int CopperRank(const std::string& name)
{
    if (name == "F.Cu") return 0;
    if (name == "B.Cu") return 1000;
    if (name.starts_with("In") && name.ends_with(".Cu"))
    {
        int rank = 0;
        for (std::size_t i = 2; i < name.size() - 3; ++i)
        { if (name[i] < '0' || name[i] > '9') return -1; rank = rank * 10 + name[i] - '0'; if (rank > 998) return -1; }
        return rank > 0 ? rank : -1;
    }
    return -1;
}
std::vector<std::string> ViaLayers(const PcbDocument& document, const std::vector<std::string>& source)
{
    if (source.size() != 2) return PadLayers(source);
    const auto first = CopperRank(source[0]), second = CopperRank(source[1]);
    if (first < 0 || second < 0) return PadLayers(source);
    std::vector<std::string> result;
    for (const auto& layer : document.layers)
    {
        const auto rank = CopperRank(layer.name);
        if (rank >= std::min(first, second) && rank <= std::max(first, second)) result.push_back(layer.name);
    }
    return result.empty() ? PadLayers(source) : result;
}
}

PcbCanvas::PcbCanvas(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Background);
    SetMinSize(FromDIP(wxSize(320, 220)));
    SetToolTip(wxT("滚轮缩放；中键或右键拖动平移；单击选择对象并高亮网络。"));
    Bind(wxEVT_PAINT, &PcbCanvas::Paint, this);
    for (const auto& type : {wxEVT_LEFT_DOWN, wxEVT_MIDDLE_DOWN, wxEVT_MIDDLE_UP, wxEVT_RIGHT_DOWN,
        wxEVT_RIGHT_UP, wxEVT_MOTION, wxEVT_MOUSEWHEEL}) Bind(type, &PcbCanvas::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &PcbCanvas::OnCaptureLost, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { if (m_fitted) Fit(); else Refresh(false); event.Skip(); });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_HOME) Fit();
        else if (event.GetKeyCode() == WXK_ESCAPE) { SetHighlightedNet(std::nullopt); Select({}); }
        else event.Skip();
    });
}

void PcbCanvas::SetDocument(std::shared_ptr<const PcbDocument> document, bool preserveView)
{
    if (HasCapture()) ReleaseMouse();
    m_panning = false;
    std::optional<std::string> highlightedName;
    if (preserveView && m_highlightedNet && m_document)
    {
        const auto found = m_document->nets.find(*m_highlightedNet);
        if (found != m_document->nets.end() && !found->second.empty()) highlightedName = found->second;
    }
    m_document = std::move(document);
    m_highlightedNet.reset(); m_selection = {};
    if (!preserveView) m_hiddenLayers.clear();
    else if (highlightedName && m_document)
        for (const auto& [id, name] : m_document->nets)
            if (id > 0 && name == *highlightedName) { m_highlightedNet = id; break; }
    RebuildItems();
    if (!preserveView || !m_document) Fit(); else Refresh(false);
    if (m_selectionCallback) m_selectionCallback(m_selection);
}

wxPoint2DDouble PcbCanvas::ScreenToWorld(const wxPoint& screen) const
{
    const auto size = GetClientSize();
    return {m_center.m_x + (screen.x - size.x / 2.) / m_scale, m_center.m_y + (screen.y - size.y / 2.) / m_scale};
}

wxPoint PcbCanvas::WorldToScreen(const wxPoint2DDouble& world) const
{
    const auto size = GetClientSize();
    const auto coordinate = [](double v) { return static_cast<int>(std::round(std::clamp(v, -100000000., 100000000.))); };
    return {coordinate((world.m_x - m_center.m_x) * m_scale + size.x / 2.),
        coordinate((world.m_y - m_center.m_y) * m_scale + size.y / 2.)};
}

void PcbCanvas::Fit()
{
    const auto size = GetClientSize();
    m_fitted = true;
    if (!m_document || !m_document->bounds.valid)
    { m_center = {}; m_scale = 10; Refresh(false); return; }
    const auto& bounds = m_document->bounds;
    m_center = {(bounds.minX + bounds.maxX) / 2, (bounds.minY + bounds.maxY) / 2};
    m_scale = std::clamp(std::min(std::max(1, size.x - FromDIP(80)) / std::max(1., bounds.Width()),
        std::max(1, size.y - FromDIP(80)) / std::max(1., bounds.Height())), .00001, 10000.);
    Refresh(false);
}

void PcbCanvas::Zoom(double factor)
{ const auto size = GetClientSize(); ZoomAt(factor, wxPoint(size.x / 2, size.y / 2)); }
void PcbCanvas::ZoomAt(double factor, const wxPoint& anchor)
{
    if (!std::isfinite(factor) || factor <= 0) return;
    const auto before = ScreenToWorld(anchor);
    m_scale = std::clamp(m_scale * factor, .00001, 10000.);
    const auto after = ScreenToWorld(anchor);
    m_center.m_x += before.m_x - after.m_x; m_center.m_y += before.m_y - after.m_y;
    m_fitted = false; Refresh(false); ReportPosition(anchor);
}

bool PcbCanvas::IsLayerVisible(const std::string& layer) const
{
    if (layer.starts_with("*."))
    {
        if (!m_document) return true;
        for (const auto& candidate : m_document->layers)
            if (candidate.name.ends_with(layer.substr(1)) && !m_hiddenLayers.contains(candidate.name)) return true;
        return false;
    }
    if (Copper(layer) && m_document && std::none_of(m_document->layers.begin(), m_document->layers.end(),
        [&layer](const auto& candidate) { return candidate.name == layer; })) return false;
    return !m_hiddenLayers.contains(layer);
}
bool PcbCanvas::ItemVisible(const Item& item) const
{
    return !item.layers.empty() && std::any_of(item.layers.begin(), item.layers.end(),
        [this](const auto& layer) { return IsLayerVisible(layer); });
}
void PcbCanvas::SetLayerVisible(const std::string& layer, bool visible)
{
    if (visible) m_hiddenLayers.erase(layer); else m_hiddenLayers.insert(layer);
    if (m_selection.kind != PcbSelection::Kind::None && !std::any_of(m_items.begin(), m_items.end(),
        [this](const auto& item) { return item.selection == m_selection && ItemVisible(item); })) Select({});
    Refresh(false);
}
void PcbCanvas::ShowAllLayers(bool visible)
{
    m_hiddenLayers.clear();
    if (!visible && m_document)
    {
        for (const auto& layer : m_document->layers) m_hiddenLayers.insert(layer.name);
        for (const auto& item : m_items) for (const auto& layer : item.layers)
            if (!layer.starts_with("*.")) m_hiddenLayers.insert(layer);
        Select({});
    }
    Refresh(false);
}
void PcbCanvas::SetHighlightedNet(std::optional<int> net)
{
    m_highlightedNet = net && *net > 0 ? net : std::nullopt;
    Refresh(false);
}

void PcbCanvas::RebuildItems()
{
    m_items.clear(); m_boardBounds = {};
    if (!m_document) return;
    const auto add = [this](Item item) {
        if (item.points.empty()) return;
        item.bounds = Bounds(item.points, item.width / 2);
        if (item.shape == Item::Shape::Text)
        {
            const auto& p = item.points[0];
            const double width = std::max(1., static_cast<double>(item.text.length())) * item.textHeight * .7;
            for (const auto corner : std::array<wxPoint2DDouble, 4>{{{-width / 2, -item.textHeight / 2},
                {width / 2, -item.textHeight / 2}, {width / 2, item.textHeight / 2}, {-width / 2, item.textHeight / 2}}})
            {
                const auto rotated = Rotate(corner, item.textAngle);
                item.hitPolygon.push_back({p.m_x + rotated.m_x, p.m_y + rotated.m_y});
            }
            item.bounds = Bounds(item.hitPolygon);
        }
        m_items.push_back(std::move(item));
    };
    const auto graphic = [&add](const PcbGraphic& value, const PcbSelection& selection) {
        if (value.hidden || value.points.empty()) return;
        Item item; item.layers = {value.layer}; item.selection = selection; item.width = value.width;
        item.filled = value.filled;
        if (value.kind == PcbGraphicKind::Text)
        { item.shape = Item::Shape::Text; item.text = wxString::FromUTF8(value.text); item.textHeight = value.textSize.y;
            item.textAngle = value.rotation; item.points = {Point(value.points[0])}; }
        else if (value.kind == PcbGraphicKind::Arc && value.points.size() >= 3)
            item.points = Arc(value.points[0], value.points[1], value.points[2]);
        else if (value.kind == PcbGraphicKind::Circle && value.points.size() >= 2)
        { const auto radius = Distance(Point(value.points[0]), Point(value.points[1]));
            item.points = Ellipse(Point(value.points[0]), radius * 2, radius * 2); item.shape = Item::Shape::Polygon; }
        else
        {
            for (const auto p : value.points) item.points.push_back(Point(p));
            if (value.kind == PcbGraphicKind::Rect || value.kind == PcbGraphicKind::Polygon) item.shape = Item::Shape::Polygon;
        }
        add(std::move(item));
    };
    // Zone fills are background geometry; holes are a documented importer limit.
    for (std::size_t index = 0; index < m_document->zones.size(); ++index)
        for (const auto& polygon : m_document->zones[index].polygons)
        {
            Item item; item.shape = Item::Shape::Polygon; item.zone = true; item.width = .1;
            item.filled = polygon.filled; item.net = m_document->zones[index].net;
            item.layers = polygon.layer.empty() ? m_document->zones[index].layers : std::vector<std::string>{polygon.layer};
            item.selection = {PcbSelection::Kind::Zone, index};
            for (const auto p : polygon.points) item.points.push_back(Point(p));
            add(std::move(item));
        }
    for (std::size_t index = 0; index < m_document->graphics.size(); ++index)
        graphic(m_document->graphics[index], {PcbSelection::Kind::Graphic, index});
    for (std::size_t index = 0; index < m_document->tracks.size(); ++index)
    {
        const auto& track = m_document->tracks[index];
        Item item; item.layers = {track.layer}; item.net = track.net; item.width = track.width;
        item.selection = {PcbSelection::Kind::Track, index};
        item.points = track.mid ? Arc(track.start, *track.mid, track.end) : std::vector<wxPoint2DDouble>{Point(track.start), Point(track.end)};
        add(std::move(item));
    }
    for (std::size_t index = 0; index < m_document->footprints.size(); ++index)
    {
        const auto& footprint = m_document->footprints[index];
        const auto first = m_items.size();
        for (std::size_t child = 0; child < footprint.pads.size(); ++child)
        {
            const auto& pad = footprint.pads[child];
            Item item; item.shape = Item::Shape::Polygon; item.filled = true; item.net = pad.net;
            item.selection = {PcbSelection::Kind::Pad, index, child}; item.layers = PadLayers(pad.layers); item.width = .05;
            if (pad.shape == "circle") item.points = Ellipse(Point(pad.position), pad.size.x, pad.size.y, pad.rotation);
            else item.points = RoundedRectangle(Point(pad.position), pad.size.x, pad.size.y, pad.rotation,
                pad.shape == "oval" ? std::min(pad.size.x, pad.size.y) / 2 :
                pad.shape == "roundrect" ? std::min(pad.size.x, pad.size.y) * pad.roundRectRatio : 0);
            if (pad.shape != "circle" && pad.shape != "oval" && pad.shape != "roundrect" && pad.shape != "rect")
            { item.approximate = true; item.filled = false; }
            add(item);
            if (pad.drill.x > 0 && pad.drill.y > 0)
            {
                item.hole = true; item.width = 0; item.filled = true;
                item.points = Ellipse({pad.position.x + pad.drillOffset.x, pad.position.y + pad.drillOffset.y},
                    pad.drill.x, pad.drill.y, pad.rotation);
                add(std::move(item));
            }
        }
        for (std::size_t child = 0; child < footprint.graphics.size(); ++child)
            graphic(footprint.graphics[child], {PcbSelection::Kind::Graphic, index, child, true});
        PcbBounds bounds;
        for (std::size_t i = first; i < m_items.size(); ++i)
            for (const auto& p : m_items[i].points) bounds.Include({p.m_x, p.m_y});
        if (!bounds.valid) bounds.Include({footprint.x, footprint.y}, 1);
        Item envelope; envelope.shape = Item::Shape::Polygon; envelope.selectionOnly = true;
        envelope.layers = {footprint.layer}; envelope.width = 0;
        envelope.selection = {PcbSelection::Kind::Footprint, index};
        envelope.points = {{bounds.minX - .5, bounds.minY - .5}, {bounds.maxX + .5, bounds.minY - .5},
            {bounds.maxX + .5, bounds.maxY + .5}, {bounds.minX - .5, bounds.maxY + .5}};
        add(std::move(envelope));
    }
    for (std::size_t index = 0; index < m_document->vias.size(); ++index)
    {
        const auto& via = m_document->vias[index];
        Item item; item.shape = Item::Shape::Polygon; item.filled = true; item.width = .05;
        item.selection = {PcbSelection::Kind::Via, index}; item.layers = ViaLayers(*m_document, via.layers); item.net = via.net;
        item.points = Ellipse(Point(via.position), via.diameter, via.diameter); add(item);
        if (via.drill > 0)
        { item.points = Ellipse(Point(via.position), via.drill, via.drill); item.hole = true; item.width = 0; add(std::move(item)); }
    }
}

wxColour PcbCanvas::ItemColour(const Item& item, bool selected) const
{
    if (item.hole) return Background;
    if (selected || (m_highlightedNet && item.net == *m_highlightedNet)) return Selection;
    wxColour colour(166, 179, 195);
    for (const auto& layer : item.layers)
    {
        if (!IsLayerVisible(layer)) continue;
        if (layer == "*.Cu") colour = IsLayerVisible("F.Cu") ? wxColour(230, 91, 88) :
            IsLayerVisible("B.Cu") ? wxColour(91, 145, 241) : wxColour(92, 197, 129);
        else if (layer == "F.Cu") colour = wxColour(230, 91, 88);
        else if (layer == "B.Cu") colour = wxColour(91, 145, 241);
        else if (Copper(layer)) colour = wxColour(92, 197, 129);
        else if (layer == "Edge.Cuts") colour = wxColour(244, 211, 104);
        else if (layer == "F.SilkS") colour = wxColour(235, 238, 217);
        else if (layer == "B.SilkS") colour = wxColour(198, 158, 228);
        else if (layer.ends_with(".CrtYd")) colour = wxColour(163, 113, 187);
        else if (layer.ends_with(".Mask")) colour = wxColour(117, 192, 143);
        break;
    }
    if (m_highlightedNet && item.net > 0 && item.net != *m_highlightedNet)
        colour = wxColour((colour.Red() + Background.Red()) / 2, (colour.Green() + Background.Green()) / 2,
            (colour.Blue() + Background.Blue()) / 2);
    return colour;
}

std::optional<PcbSelection> PcbCanvas::HitTest(const wxPoint& screen) const
{
    const auto world = ScreenToWorld(screen);
    const double tolerance = FromDIP(7) / m_scale;
    double best = std::numeric_limits<double>::max();
    std::optional<PcbSelection> result;
    for (auto current = m_items.rbegin(); current != m_items.rend(); ++current)
    {
        const auto& item = *current;
        if (!ItemVisible(item) || item.hole) continue;
        const auto& b = item.bounds;
        if (world.m_x < b.m_x - tolerance || world.m_x > b.m_x + b.m_width + tolerance ||
            world.m_y < b.m_y - tolerance || world.m_y > b.m_y + b.m_height + tolerance) continue;
        double distance = std::numeric_limits<double>::max();
        if ((item.shape == Item::Shape::Text && Inside(world, item.hitPolygon)) || (item.shape == Item::Shape::Polygon &&
            (item.filled || item.selectionOnly) && Inside(world, item.points))) distance = 0;
        for (std::size_t i = 1; i < item.points.size(); ++i)
            distance = std::min(distance, SegmentDistance(world, item.points[i - 1], item.points[i]));
        if (item.shape == Item::Shape::Polygon && item.points.size() > 2)
            distance = std::min(distance, SegmentDistance(world, item.points.back(), item.points.front()));
        distance = std::max(0., distance - item.width / 2);
        if (distance > tolerance) continue;
        int priority = item.selection.kind == PcbSelection::Kind::Pad ? 0 : item.selection.kind == PcbSelection::Kind::Via ? 1 :
            item.selection.kind == PcbSelection::Kind::Track ? 2 : item.selection.kind == PcbSelection::Kind::Graphic ? 3 :
            item.selection.kind == PcbSelection::Kind::Footprint ? 4 : 5;
        const double score = priority * 1000. + distance / tolerance;
        if (score < best) { best = score; result = item.selection; }
    }
    return result;
}

void PcbCanvas::Select(const PcbSelection& selection)
{
    m_selection = selection;
    m_highlightedNet.reset();
    if (selection.kind != PcbSelection::Kind::None)
        for (const auto& item : m_items)
            if (item.selection == selection && item.net > 0) { m_highlightedNet = item.net; break; }
    if (m_selectionCallback) m_selectionCallback(selection);
    Refresh(false);
}

void PcbCanvas::ReportPosition(const wxPoint& screen)
{
    if (!m_statusCallback) return;
    const auto world = ScreenToWorld(screen);
    m_statusCallback(wxString::Format(wxT("X %.3f mm   Y %.3f mm   %.2f px/mm · 滚轮缩放，中键/右键平移，Home 适配"),
        world.m_x, world.m_y, m_scale));
}
void PcbCanvas::OnMouse(wxMouseEvent& event)
{
    const auto screen = event.GetPosition();
    if (event.GetEventType() == wxEVT_MOUSEWHEEL)
    { const int delta = event.GetWheelDelta(); if (delta) ZoomAt(std::pow(1.2, static_cast<double>(event.GetWheelRotation()) / delta), screen); return; }
    if (event.MiddleDown() || event.RightDown())
    {
        SetFocus(); m_panning = true; m_panStart = screen; m_panCenter = m_center;
        if (!HasCapture()) CaptureMouse(); SetCursor(wxCursor(wxCURSOR_HAND)); return;
    }
    if (event.MiddleUp() || event.RightUp())
    { m_panning = false; if (HasCapture()) ReleaseMouse(); SetCursor(wxNullCursor); return; }
    if (m_panning && event.Dragging())
    {
        const auto movement = screen - m_panStart;
        m_center = {m_panCenter.m_x - movement.x / m_scale, m_panCenter.m_y - movement.y / m_scale};
        m_fitted = false; Refresh(false);
    }
    else if (event.LeftDown())
    { SetFocus(); Select(HitTest(screen).value_or(PcbSelection{})); }
    ReportPosition(screen);
}
void PcbCanvas::OnCaptureLost(wxMouseCaptureLostEvent&)
{ m_panning = false; SetCursor(wxNullCursor); }

void PcbCanvas::Paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC buffer(this);
    Draw(buffer);
}

void PcbCanvas::Draw(wxDC& buffer)
{
    buffer.SetBackground(wxBrush(Background)); buffer.Clear();
    const auto size = GetClientSize();
    auto gc = std::unique_ptr<wxGraphicsContext>(wxGraphicsContext::CreateFromUnknownDC(buffer));
    if (!gc) return;
    const auto screenPoint = [this, size](wxPoint2DDouble p) {
        return wxPoint2DDouble{(p.m_x - m_center.m_x) * m_scale + size.x / 2.,
            (p.m_y - m_center.m_y) * m_scale + size.y / 2.};
    };
    const auto topLeft = ScreenToWorld({0, 0}), bottomRight = ScreenToWorld({size.x, size.y});
    const wxRect2DDouble viewport{topLeft.m_x, topLeft.m_y, bottomRight.m_x - topLeft.m_x, bottomRight.m_y - topLeft.m_y};
    const double step = std::pow(10., std::floor(std::log10(30 / m_scale)));
    gc->SetPen(wxPen(wxColour(40, 49, 63), 1));
    for (int i = 0; i < 400; ++i)
    {
        const double x = (std::ceil(topLeft.m_x / step) + i) * step;
        if (x > bottomRight.m_x) break;
        const auto p = screenPoint({x, 0}); gc->StrokeLine(p.m_x, 0, p.m_x, size.y);
    }
    for (int i = 0; i < 400; ++i)
    {
        const double y = (std::ceil(topLeft.m_y / step) + i) * step;
        if (y > bottomRight.m_y) break;
        const auto p = screenPoint({0, y}); gc->StrokeLine(0, p.m_y, size.x, p.m_y);
    }
    if (!m_document)
    {
        gc->SetFont(wxFont(wxFontInfo(12).FaceName(wxT("Microsoft YaHei UI"))), wxColour(190, 200, 216));
        gc->DrawText(wxT("导入 .kicad_pcb 后，在此查看 PCB 板图。"), FromDIP(28), FromDIP(32));
        return;
    }
    gc->Clip(0, 0, size.x, size.y);
    for (const auto& item : m_items)
    {
        const bool selected = item.selection == m_selection && m_selection.kind != PcbSelection::Kind::None;
        if (!ItemVisible(item) || (item.selectionOnly && !selected) || !Intersects(item.bounds, viewport)) continue;
        const auto colour = ItemColour(item, selected);
        if (item.shape == Item::Shape::Text)
        {
            const int pixels = std::clamp(static_cast<int>(std::lround(item.textHeight * m_scale)), 4, 320);
            if (item.textHeight * m_scale < 3) continue;
            gc->PushState();
            const auto anchor = screenPoint(item.points[0]); gc->Translate(anchor.m_x, anchor.m_y); gc->Rotate(-item.textAngle * Pi / 180);
            gc->SetFont(wxFont(wxFontInfo(wxSize(0, pixels)).FaceName(wxT("Microsoft YaHei UI"))), colour);
            double width, height; gc->GetTextExtent(item.text, &width, &height);
            gc->DrawText(item.text, -width / 2, -height / 2); gc->PopState();
            continue;
        }
        auto path = gc->CreatePath();
        const auto first = screenPoint(item.points[0]); path.MoveToPoint(first.m_x, first.m_y);
        for (std::size_t i = 1; i < item.points.size(); ++i)
        { const auto p = screenPoint(item.points[i]); path.AddLineToPoint(p.m_x, p.m_y); }
        if (item.shape == Item::Shape::Polygon) path.CloseSubpath();
        wxColour fill = colour;
        if (item.zone) fill = wxColour(colour.Red(), colour.Green(), colour.Blue(), 42);
        gc->SetBrush(item.filled ? wxBrush(fill) : *wxTRANSPARENT_BRUSH);
        const double thickness = item.hole ? 0 : item.selectionOnly ? 1.2 : std::max(.7, item.width * m_scale + (selected ? 1.5 : 0));
        gc->SetPen(item.hole ? *wxTRANSPARENT_PEN : wxPen(colour, static_cast<int>(std::min(10000., std::ceil(thickness))),
            item.selectionOnly || item.approximate ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
        gc->DrawPath(path);
    }
    gc->ResetClip();
    gc->SetFont(wxFont(wxFontInfo(9).FaceName(wxT("Microsoft YaHei UI"))), wxColour(169, 184, 204));
    const double barWorld = step * 2;
    const double barPixels = barWorld * m_scale;
    const double x = FromDIP(20), y = size.y - FromDIP(24);
    gc->SetPen(wxPen(wxColour(169, 184, 204), 2));
    gc->StrokeLine(x, y, x + barPixels, y); gc->StrokeLine(x, y - 4, x, y + 4); gc->StrokeLine(x + barPixels, y - 4, x + barPixels, y + 4);
    gc->DrawText(wxString::Format("%.5g mm", barWorld), x, y - FromDIP(22));
}
