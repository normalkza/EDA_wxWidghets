#include "KiCadPcbImporter.h"
#include "KiCadSExpr.h"

#include <wx/file.h>
#include <wx/log.h>
#include <charconv>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using Node = KiCadSExpr;
constexpr double Pi = 3.14159265358979323846;

[[noreturn]] void Invalid(const Node& node, const std::string& text)
{
    throw std::runtime_error(text + "（" + std::string(node.Head()) + "，字节 " + std::to_string(node.offset) + "）");
}

const Node& Atom(const Node& node, std::size_t index)
{
    if (index >= node.children.size() || node.children[index].list) Invalid(node, "字段数量或类型不正确");
    return node.children[index];
}
std::string Value(const Node& node, std::size_t index) { return Atom(node, index).value; }
const Node* One(const Node& node, std::string_view name, bool required = false)
{
    const Node* result = nullptr;
    for (const auto& child : node.children)
        if (child.list && child.Head() == name)
        {
            if (result) Invalid(node, "重复字段 " + std::string(name));
            result = &child;
        }
    if (!result && required) Invalid(node, "缺少字段 " + std::string(name));
    return result;
}
int Integer(const Node& node, std::size_t index)
{
    const auto& text = Atom(node, index).value;
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        Invalid(node, "无效或溢出的整数");
    return value;
}
double Number(const Node& node, std::size_t index)
{
    std::string_view text = Atom(node, index).value;
    if (text.starts_with('+')) text.remove_prefix(1);
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value) || std::abs(value) > 1000000)
        Invalid(node, "无效、非有限或超过 ±1000000 的数值");
    return value;
}
double Dimension(const Node& node, std::size_t index, bool allowZero = false)
{
    const double value = Number(node, index);
    if (value < 0 || (!allowZero && value == 0)) Invalid(node, "尺寸必须为正数");
    return value;
}
PcbPoint Point(const Node& node) { return {Number(node, 1), Number(node, 2)}; }
PcbPoint Point(const Node& node, std::string_view name) { return Point(*One(node, name, true)); }
double Rotation(const Node& node)
{
    return node.children.size() > 3 && !node.children[3].list ? Number(node, 3) : 0;
}
PcbPoint Rotate(PcbPoint point, double degrees)
{
    const double radians = degrees * Pi / 180;
    return {point.x * std::cos(radians) + point.y * std::sin(radians),
        -point.x * std::sin(radians) + point.y * std::cos(radians)};
}
std::string Layer(const Node& node)
{
    const auto* layer = One(node, "layer", true);
    auto name = Value(*layer, 1);
    if (name.empty() || name.size() > 128) Invalid(*layer, "图层名称无效");
    return name;
}
std::vector<std::string> Layers(const Node& node)
{
    if (const auto* layers = One(node, "layers"))
    {
        std::vector<std::string> result;
        for (std::size_t i = 1; i < layers->children.size(); ++i)
        {
            auto name = Value(*layers, i);
            if (name.empty() || name.size() > 128) Invalid(*layers, "图层名称无效");
            result.push_back(std::move(name));
        }
        if (result.empty()) Invalid(*layers, "图层列表不能为空");
        return result;
    }
    return {Layer(node)};
}
bool Flag(const Node& node, std::string_view flag)
{
    for (const auto& child : node.children)
    {
        if (!child.list && child.value == flag) return true;
        if (child.list && child.Head() == flag)
            return child.children.size() == 1 || Value(child, 1) == "yes" || Value(child, 1) == "true";
    }
    return false;
}

double Normalize(double value)
{
    value = std::fmod(value, 2 * Pi);
    return value < 0 ? value + 2 * Pi : value;
}
void ArcBounds(PcbBounds& bounds, PcbPoint start, PcbPoint mid, PcbPoint end, double margin)
{
    bounds.Include(start, margin); bounds.Include(mid, margin); bounds.Include(end, margin);
    const double bx = mid.x - start.x, by = mid.y - start.y;
    const double cx = end.x - start.x, cy = end.y - start.y;
    const double determinant = 2 * (bx * cy - by * cx);
    if (std::abs(determinant) < 1e-12) return;
    const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const PcbPoint center{start.x + (cy * b2 - by * c2) / determinant,
        start.y + (bx * c2 - cx * b2) / determinant};
    const double radius = std::hypot(start.x - center.x, start.y - center.y);
    if (!std::isfinite(radius)) return;
    const double a = std::atan2(start.y - center.y, start.x - center.x);
    const double m = std::atan2(mid.y - center.y, mid.x - center.x);
    const double e = std::atan2(end.y - center.y, end.x - center.x);
    const double positiveSweep = Normalize(e - a);
    const bool positive = Normalize(m - a) <= positiveSweep + 1e-9;
    const double sweep = positive ? positiveSweep : 2 * Pi - positiveSweep;
    for (int i = 0; i < 4; ++i)
    {
        const double angle = i * Pi / 2;
        const double distance = positive ? Normalize(angle - a) : Normalize(a - angle);
        if (distance <= sweep + 1e-9)
            bounds.Include({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)}, margin);
    }
}

class Importer
{
public:
    PcbDocument Read(const Node& root, const std::string& source)
    {
        if (root.Head() != "kicad_pcb") Invalid(root, "这不是 KiCad PCB 板图（需要 kicad_pcb 根节点）");
        m_board.version = Integer(*One(root, "version", true), 1);
        if (m_board.version < 20170101 || m_board.version > 20991231)
            Invalid(root, "不支持此 PCB 格式版本");
        if (m_board.version > 20260206) Warn("文件格式新于已验证的 KiCad 10；未识别内容不会显示，但原始文件完整保留");
        const auto& layers = *One(root, "layers", true);
        std::set<int> ids;
        std::set<std::string> names;
        for (std::size_t i = 1; i < layers.children.size(); ++i)
        {
            const auto& layer = layers.children[i];
            if (!layer.list) Invalid(layer, "图层定义必须为列表");
            PcbLayer data{Integer(layer, 0), Value(layer, 1), Value(layer, 2)};
            if (data.id < 0 || data.id > 1024 || data.name.empty() || data.name.size() > 128 ||
                !ids.insert(data.id).second || !names.insert(data.name).second)
                Invalid(layer, "图层 ID 或名称无效/重复");
            if (layer.children.size() > 3) data.userName = Value(layer, 3);
            m_board.layers.push_back(std::move(data));
        }
        if (m_board.layers.empty()) Invalid(layers, "缺少可用 PCB 图层");
        m_activeLayerNames = names;
        ReadNets(root);
        for (std::size_t i = 1; i < root.children.size(); ++i)
        {
            const auto& child = root.children[i];
            if (!child.list) Invalid(root, "PCB 根节点包含非法原子字段");
            const auto head = child.Head();
            if (head == "footprint" || head == "module") m_board.footprints.push_back(Footprint(child));
            else if (head == "segment" || head == "arc") m_board.tracks.push_back(Track(child));
            else if (head == "via") m_board.vias.push_back(Via(child));
            else if (head == "zone") m_board.zones.push_back(Zone(child));
            else if (head.starts_with("gr_"))
            {
                if (auto graphic = Graphic(child, nullptr)) m_board.graphics.push_back(std::move(*graphic));
            }
            else if (head == "dimension" || head == "image" || head == "target" || head == "table" ||
                head == "pcb_text" || head == "embedded_files")
                Warn("未显示的 PCB 对象：" + std::string(head));
            else if (head != "version" && head != "generator" && head != "generator_version" && head != "general" &&
                head != "paper" && head != "page" && head != "title_block" && head != "layers" && head != "setup" &&
                head != "net" && head != "net_class" && head != "property" && head != "group" && head != "uuid" &&
                head != "embedded_fonts") Warn("未识别的 PCB 区域：" + std::string(head));
        }
        GatherBoundsAndLayers();
        for (const auto& [message, count] : m_warnings)
            m_board.warnings.push_back(message + (count > 1 ? "（" + std::to_string(count) + " 项）" : ""));
        m_board.originalSource = source;
        return std::move(m_board);
    }
private:
    void Warn(const std::string& message) { ++m_warnings[message]; }
    void ReadNets(const Node& root)
    {
        m_board.nets.emplace(0, "");
        std::set<int> declared;
        for (const auto* node : root.FindAll("net"))
        {
            const int id = Integer(*node, 1);
            const auto name = Value(*node, 2);
            if (id < 0 || !declared.insert(id).second || (id == 0 && !name.empty())) Invalid(*node, "网络 ID 无效或重复");
            if (id != 0 && name.empty()) Invalid(*node, "非零网络的名称不能为空");
            if (const auto found = m_nameToNet.find(name); found != m_nameToNet.end() && found->second != id)
                Invalid(*node, "网络名称对应多个 ID");
            m_board.nets[id] = name;
            m_nameToNet[name] = id;
        }
        std::set<std::string> modernNames;
        GatherModernNetNames(root, modernNames);
        int next = 1;
        for (const auto& name : modernNames)
        {
            if (name.empty() || m_nameToNet.contains(name)) continue;
            while (m_board.nets.contains(next)) ++next;
            m_nameToNet[name] = next;
            m_board.nets[next++] = name;
        }
    }
    void GatherModernNetNames(const Node& node, std::set<std::string>& names)
    {
        if (node.Head() == "net" && node.children.size() > 1 && !node.children[1].list && node.children[1].quoted)
            names.insert(node.children[1].value);
        for (const auto& child : node.children) if (child.list) GatherModernNetNames(child, names);
    }
    int Net(const Node& node)
    {
        const auto* net = One(node, "net");
        if (!net) return 0;
        const auto& value = Atom(*net, 1);
        if (value.quoted)
        {
            if (value.value.empty()) return 0;
            const auto found = m_nameToNet.find(value.value);
            if (found == m_nameToNet.end()) Invalid(*net, "网络名称不存在");
            return found->second;
        }
        const int id = Integer(*net, 1);
        const auto found = m_board.nets.find(id);
        if (id < 0 || found == m_board.nets.end()) Invalid(*net, "引用了未定义的网络 ID");
        if (net->children.size() > 2 && Value(*net, 2) != found->second)
            Invalid(*net, "网络 ID 与名称不一致");
        return id;
    }
    std::vector<PcbPoint> Points(const Node& node)
    {
        const auto& pts = *One(node, "pts", true);
        std::vector<PcbPoint> points;
        for (std::size_t i = 1; i < pts.children.size(); ++i)
        {
            const auto& xy = pts.children[i];
            if (xy.Head() == "xy") points.push_back(Point(xy));
            else if (xy.Head() == "arc")
            {
                const auto start = Point(xy, "start"), middle = Point(xy, "mid"), end = Point(xy, "end");
                const double bx = middle.x - start.x, by = middle.y - start.y;
                const double cx = end.x - start.x, cy = end.y - start.y;
                const double determinant = 2 * (bx * cy - by * cx);
                if (std::abs(determinant) < 1e-12)
                    points.insert(points.end(), {start, middle, end});
                else
                {
                    const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
                    const PcbPoint center{start.x + (cy * b2 - by * c2) / determinant,
                        start.y + (bx * c2 - cx * b2) / determinant};
                    const double radius = std::hypot(start.x - center.x, start.y - center.y);
                    const double a = std::atan2(start.y - center.y, start.x - center.x);
                    const double m = std::atan2(middle.y - center.y, middle.x - center.x);
                    const double e = std::atan2(end.y - center.y, end.x - center.x);
                    const double positiveSweep = Normalize(e - a);
                    const double sweep = Normalize(m - a) <= positiveSweep + 1e-9 ? positiveSweep : positiveSweep - 2 * Pi;
                    const int pieces = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) * 18 / Pi)));
                    for (int part = 0; part <= pieces; ++part)
                    {
                        const double angle = a + sweep * part / pieces;
                        points.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)});
                    }
                }
                Warn("多边形中的圆弧以最多 10° 步长折线显示");
            }
            else Invalid(xy, "多边形中暂不支持此坐标表达式");
        }
        if (points.size() < 3) Invalid(node, "多边形至少需要三个点");
        return points;
    }
    std::optional<PcbGraphic> Graphic(const Node& node, const PcbFootprint* footprint)
    {
        const auto head = node.Head();
        PcbGraphic result;
        result.layer = Layer(node);
        if (const auto* width = One(node, "width")) result.width = Dimension(*width, 1, true);
        if (const auto* stroke = One(node, "stroke"))
            if (const auto* width = One(*stroke, "width")) result.width = Dimension(*width, 1, true);
        if (const auto* fill = One(node, "fill"))
            result.filled = Value(*fill, 1) == "solid" || Value(*fill, 1) == "yes";
        if (head == "gr_line" || head == "fp_line")
        { result.kind = PcbGraphicKind::Line; result.points = {Point(node, "start"), Point(node, "end")}; }
        else if (head == "gr_arc" || head == "fp_arc")
        {
            result.kind = PcbGraphicKind::Arc;
            if (const auto* middle = One(node, "mid"))
                result.points = {Point(node, "start"), Point(*middle), Point(node, "end")};
            else
            {
                const auto center = Point(node, "start"), first = Point(node, "end");
                const double angle = Number(*One(node, "angle", true), 1);
                const auto middleOffset = Rotate({first.x - center.x, first.y - center.y}, -angle / 2);
                const auto last = Rotate({first.x - center.x, first.y - center.y}, -angle);
                result.points = {first, {center.x + middleOffset.x, center.y + middleOffset.y}, {center.x + last.x, center.y + last.y}};
                Warn("旧式 angle 圆弧已转换为三点圆弧");
            }
        }
        else if (head == "gr_circle" || head == "fp_circle")
        { result.kind = PcbGraphicKind::Circle; result.points = {Point(node, "center"), Point(node, "end")}; }
        else if (head == "gr_rect" || head == "fp_rect")
        {
            result.kind = PcbGraphicKind::Rect;
            const auto start = Point(node, "start"), end = Point(node, "end");
            result.points = {start, {end.x, start.y}, end, {start.x, end.y}};
        }
        else if (head == "gr_poly" || head == "fp_poly")
        { result.kind = PcbGraphicKind::Polygon; result.points = Points(node); }
        else if (head == "gr_text" || head == "fp_text" || head == "property")
        {
            result.kind = PcbGraphicKind::Text;
            result.text = Value(node, head == "gr_text" ? 1 : 2);
            const auto& at = *One(node, "at", true);
            result.points = {Point(at)}; result.rotation = Rotation(at);
            result.hidden = Flag(node, "hide");
            if (const auto* effects = One(node, "effects"))
            {
                result.hidden = result.hidden || Flag(*effects, "hide");
                if (const auto* font = One(*effects, "font"))
                    if (const auto* size = One(*font, "size"))
                        result.textSize = {Dimension(*size, 1), Dimension(*size, 2)};
            }
            if (result.text.size() > 65536) Invalid(node, "单条 PCB 文字超过 65536 字节");
            if (footprint && result.text == "${REFERENCE}") result.text = footprint->reference;
            if (footprint && result.text == "${VALUE}") result.text = footprint->value;
        }
        else
        { Warn("未显示的图形类型：" + std::string(head)); return std::nullopt; }
        if (footprint)
            for (auto& point : result.points) point = PcbTransformFootprintPoint(*footprint, point);
        return result;
    }
    PcbFootprint Footprint(const Node& node)
    {
        PcbFootprint footprint;
        footprint.libraryId = Value(node, 1);
        footprint.layer = Layer(node);
        if (const auto* at = One(node, "at"))
        { footprint.x = Number(*at, 1); footprint.y = Number(*at, 2); footprint.rotation = Rotation(*at); }
        for (const auto& child : node.children)
        {
            if (child.Head() == "property")
            {
                const auto name = Value(child, 1);
                if (name == "Reference") footprint.reference = Value(child, 2);
                if (name == "Value") footprint.value = Value(child, 2);
            }
            if (child.Head() == "fp_text")
            {
                const auto type = Value(child, 1);
                if (type == "reference" && footprint.reference.empty()) footprint.reference = Value(child, 2);
                if (type == "value" && footprint.value.empty()) footprint.value = Value(child, 2);
            }
        }
        for (const auto& child : node.children)
        {
            const auto head = child.Head();
            if (head == "pad") footprint.pads.push_back(Pad(child, footprint));
            else if (head.starts_with("fp_") || (head == "property" && One(child, "at") && One(child, "layer")))
            {
                if (auto graphic = Graphic(child, &footprint)) footprint.graphics.push_back(std::move(*graphic));
            }
            else if (head == "zone")
            {
                auto zone = Zone(child);
                for (auto& polygon : zone.polygons)
                    for (auto& point : polygon.points) point = PcbTransformFootprintPoint(footprint, point);
                m_board.zones.push_back(std::move(zone));
            }
            else if (head == "model" || head == "image" || head == "embedded_files" || head == "dimension" || head == "table")
                Warn("未显示的封装对象：" + std::string(head));
        }
        return footprint;
    }
    PcbPad Pad(const Node& node, const PcbFootprint& footprint)
    {
        PcbPad pad;
        pad.number = Value(node, 1); pad.type = Value(node, 2); pad.shape = Value(node, 3);
        const auto& at = *One(node, "at", true);
        pad.position = PcbTransformFootprintPoint(footprint, Point(at));
        pad.rotation = Rotation(at);
        const auto& size = *One(node, "size", true);
        pad.size = {Dimension(size, 1), Dimension(size, 2)};
        pad.layers = Layers(node); pad.net = Net(node);
        // Padstacks may list all 32 copper layers even on a two-layer board.
        // They do not enable those inner layers in the physical board stackup.
        std::erase_if(pad.layers, [&](const std::string& name) {
            return name.find('*') == std::string::npos && name.ends_with(".Cu") && !m_activeLayerNames.contains(name);
        });
        if (pad.layers.empty()) Warn("焊盘仅引用未启用的板层，未显示该焊盘");
        if (pad.shape != "circle" && pad.shape != "rect" && pad.shape != "oval" && pad.shape != "roundrect")
            Warn("复杂焊盘以定位框显示：" + pad.shape);
        if (One(node, "chamfer") || One(node, "chamfer_ratio"))
            Warn("倒角焊盘以未倒角外框近似显示");
        if (pad.type != "smd" && pad.type != "thru_hole" && pad.type != "np_thru_hole" && pad.type != "connect")
            Warn("未验证的焊盘类型：" + pad.type);
        if (const auto* ratio = One(node, "roundrect_rratio"))
        {
            pad.roundRectRatio = Number(*ratio, 1);
            if (pad.roundRectRatio < 0 || pad.roundRectRatio > 0.5) Invalid(*ratio, "圆角比例须在 0 至 0.5 之间");
        }
        if (const auto* drill = One(node, "drill"))
        {
            const bool oval = Value(*drill, 1) == "oval";
            const std::size_t first = oval ? 2 : 1;
            pad.drill = {Dimension(*drill, first), Dimension(*drill, oval ? first + 1 : first)};
            if (const auto* offset = One(*drill, "offset")) pad.drillOffset = Rotate(Point(*offset), pad.rotation);
            if (pad.drill.x > pad.size.x || pad.drill.y > pad.size.y)
                Invalid(*drill, "钻孔尺寸不能大于焊盘尺寸");
        }
        else if (pad.type == "thru_hole" || pad.type == "np_thru_hole") Invalid(node, "通孔焊盘缺少钻孔尺寸");
        return pad;
    }
    PcbTrack Track(const Node& node)
    {
        PcbTrack result;
        result.start = Point(node, "start"); result.end = Point(node, "end");
        if (node.Head() == "arc") result.mid = Point(node, "mid");
        result.width = Dimension(*One(node, "width", true), 1);
        result.layer = Layer(node); result.net = Net(node);
        return result;
    }
    PcbVia Via(const Node& node)
    {
        PcbVia via;
        via.position = Point(node, "at");
        via.diameter = Dimension(*One(node, "size", true), 1);
        via.drill = Dimension(*One(node, "drill", true), 1);
        if (via.drill > via.diameter) Invalid(node, "过孔钻孔不能大于过孔直径");
        via.layers = Layers(node); via.net = Net(node);
        return via;
    }
    PcbZone Zone(const Node& node)
    {
        PcbZone zone;
        zone.layers = Layers(node); zone.net = Net(node); zone.keepout = One(node, "keepout") != nullptr;
        for (const auto& child : node.children)
        {
            if (child.Head() == "polygon")
            {
                for (const auto& layer : zone.layers) zone.polygons.push_back({layer, Points(child), false});
            }
            else if (child.Head() == "filled_polygon")
                zone.polygons.push_back({One(child, "layer") ? Layer(child) : zone.layers.front(), Points(child), true});
            else if (child.Head() == "fill_segments") Warn("旧式分段覆铜未显示；保留原始数据");
        }
        if (zone.polygons.empty()) Warn("区域不含可显示多边形；未重新灌铜");
        return zone;
    }
    void GatherBoundsAndLayers()
    {
        std::set<std::string> names;
        for (const auto& layer : m_board.layers) names.insert(layer.name);
        auto layer = [&](const std::string& name) {
            if (name.find('*') != std::string::npos || names.contains(name)) return;
            names.insert(name);
            m_board.layers.push_back({-static_cast<int>(m_board.layers.size()) - 1, name, "user", ""});
            Warn("补充显示文件中未声明的图层：" + name);
        };
        auto graphic = [&](const PcbGraphic& item) {
            layer(item.layer);
            if (item.hidden) return;
            if (item.kind == PcbGraphicKind::Circle)
            {
                const auto a = item.points[0], b = item.points[1];
                m_board.bounds.Include(a, std::hypot(b.x - a.x, b.y - a.y) + item.width / 2);
            }
            else if (item.kind == PcbGraphicKind::Arc)
                ArcBounds(m_board.bounds, item.points[0], item.points[1], item.points[2], item.width / 2);
            else
            {
                for (const auto point : item.points) m_board.bounds.Include(point, item.width / 2);
                if (item.kind == PcbGraphicKind::Text)
                {
                    const auto p = item.points[0];
                    const double size = std::min(1000000.0, item.textSize.x * item.text.size() * .7);
                    m_board.bounds.Include({p.x - size / 2, p.y - item.textSize.y});
                    m_board.bounds.Include({p.x + size / 2, p.y + item.textSize.y});
                }
            }
        };
        for (const auto& footprint : m_board.footprints)
        {
            layer(footprint.layer);
            m_board.bounds.Include({footprint.x, footprint.y});
            for (const auto& pad : footprint.pads)
            {
                for (const auto& name : pad.layers) layer(name);
                m_board.bounds.Include(pad.position, std::hypot(pad.size.x, pad.size.y) / 2);
            }
            for (const auto& item : footprint.graphics) graphic(item);
        }
        for (const auto& track : m_board.tracks)
        {
            layer(track.layer);
            if (track.mid) ArcBounds(m_board.bounds, track.start, *track.mid, track.end, track.width / 2);
            else { m_board.bounds.Include(track.start, track.width / 2); m_board.bounds.Include(track.end, track.width / 2); }
        }
        for (const auto& via : m_board.vias)
        {
            for (const auto& name : via.layers) layer(name);
            m_board.bounds.Include(via.position, via.diameter / 2);
        }
        for (const auto& item : m_board.graphics) graphic(item);
        for (const auto& zone : m_board.zones)
            for (const auto& polygon : zone.polygons)
            {
                layer(polygon.layer);
                for (const auto point : polygon.points) m_board.bounds.Include(point);
            }
    }
    PcbDocument m_board;
    std::map<std::string, int> m_nameToNet;
    std::set<std::string> m_activeLayerNames;
    std::map<std::string, std::size_t> m_warnings;
};
}

bool KiCadPcbImporter::Parse(const std::string& source, PcbDocument& document, wxString& error)
{
    error.clear();
    if (source.empty() || source.size() > MaxFileBytes)
    { error = wxS("PCB 文件为空或超过 64 MiB 上限。"); return false; }
    KiCadSExpr root;
    std::string parseError;
    if (!KiCadSExprParser::Parse(source, root, parseError))
    { error = wxS("无法解析 KiCad PCB：") + wxString::FromUTF8(parseError); return false; }
    try
    {
        auto candidate = Importer{}.Read(root, source);
        document = std::move(candidate);
        return true;
    }
    catch (const std::exception& exception)
    { error = wxS("无法导入 KiCad PCB：") + wxString::FromUTF8(exception.what()); return false; }
}

bool KiCadPcbImporter::Load(const wxString& path, PcbDocument& document, wxString& error)
{
    error.clear();
    wxLogNull quiet;
    wxFile file(path, wxFile::read);
    if (!file.IsOpened()) { error = wxS("无法打开 PCB 文件，请检查路径和读取权限。"); return false; }
    const auto length = file.Length();
    if (length <= 0 || length > static_cast<wxFileOffset>(MaxFileBytes))
    { error = wxS("PCB 文件为空、无法读取或超过 64 MiB 上限。"); return false; }
    std::string source(static_cast<std::size_t>(length), '\0');
    if (file.Read(source.data(), source.size()) != static_cast<ssize_t>(source.size()))
    { error = wxS("PCB 文件未完整读取；文件可能仍在被 KiCad 保存。"); return false; }
    PcbDocument candidate;
    if (!Parse(source, candidate, error)) return false;
    candidate.sourcePath = path;
    document = std::move(candidate);
    return true;
}
