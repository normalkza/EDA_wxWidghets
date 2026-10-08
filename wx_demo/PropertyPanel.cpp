#include "PropertyPanel.h"
#include <wx/dcbuffer.h>

namespace
{
    wxString DisplayName(const wxString& type)
    {
        if (type == "INPUT") return wxT("输入 (INPUT)");
        if (type == "OUTPUT") return wxT("输出 (OUTPUT)");
        if (type == "AND") return wxT("与门 (AND)");
        if (type == "OR") return wxT("或门 (OR)");
        if (type == "NOT") return wxT("非门 (NOT)");
        if (type == "NAND") return wxT("与非门 (NAND)");
        if (type == "NOR") return wxT("或非门 (NOR)");
        if (type == "XOR") return wxT("异或门 (XOR)");
        if (type == "BUTTON") return wxT("按钮 (BUTTON)");
        if (type == "LED") return wxT("发光二极管 (LED)");
        if (type == "DFF") return wxT("D 触发器 (DFF)");
        if (type == "REGISTER") return wxT("寄存器 (REGISTER)");
        if (type == "COUNTER") return wxT("计数器 (COUNTER)");
        if (type == "ADDER") return wxT("加法器 (ADDER)");
        if (type == "SUBTRACTOR") return wxT("减法器 (SUBTRACTOR)");
        if (type == "COMPARATOR") return wxT("比较器 (COMPARATOR)");
        if (type == "MUX") return wxT("数据选择器 (MUX)");
        if (type == "DEMUX") return wxT("解复用器 (DEMUX)");
        if (type == "SPLITTER") return wxT("分线器 (SPLITTER)");
        if (type == "PROBE") return wxT("探针 (PROBE)");
        if (type == "CLOCK") return wxT("时钟 (CLOCK)");
        if (type == "CONSTANT") return wxT("常量 (CONSTANT)");
        return type;
    }
}

PropertyPanel::PropertyPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxColour(245, 245, 245));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* heading = new wxStaticText(this, wxID_ANY, wxT("当前元件状态"));
    heading->SetFont(wxFontInfo(11).Bold());
    root->Add(heading, 0, wxEXPAND | wxALL, FromDIP(8));
    m_content = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_content->SetBackgroundColour(*wxWHITE);
    m_content->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_content->SetScrollRate(FromDIP(8), FromDIP(8));
    m_content->Bind(wxEVT_PAINT, &PropertyPanel::PaintTable, this);
    m_content->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        UpdateTableSize();
        event.Skip();
    });
    root->Add(m_content, 1, wxEXPAND);
    m_textSettings = new wxPanel(this);
    auto* textSizer = new wxBoxSizer(wxHORIZONTAL);
    textSizer->Add(new wxStaticText(m_textSettings, wxID_ANY, wxT("字号 (pt)")),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_fontSize = new wxSpinCtrl(m_textSettings, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 6, 96, 14);
    textSizer->Add(m_fontSize, 1, wxEXPAND);
    m_textSettings->SetSizer(textSizer);
    root->Add(m_textSettings, 0, wxEXPAND | wxALL, FromDIP(8));
    m_fontSize->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        if (m_textFontSizeCallback) m_textFontSizeCallback(m_fontSize->GetValue());
    });
    m_fontSize->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        long value;
        if (event.GetString().ToLong(&value) && value >= 6 && value <= 96 &&
            m_textFontSizeCallback)
            m_textFontSizeCallback(static_cast<int>(value));
    });
    SetSizer(root);
    Clear();
}

void PropertyPanel::SetRows(const wxString& title,
    const std::vector<std::pair<wxString, wxString>>& rows)
{
    if (m_title == title && m_rows == rows)
    {
        Layout();
        return;
    }

    bool sameRows = m_title == title && m_rows.size() == rows.size();
    for (std::size_t i = 0; sameRows && i < rows.size(); ++i)
        sameRows = m_rows[i].first == rows[i].first;

    m_title = title;
    m_rows = rows;
    if (!sameRows) m_content->Scroll(0, 0);
    UpdateTableSize();
    Layout();
}

void PropertyPanel::UpdateTableSize()
{
    if (!m_content) return;
    wxClientDC dc(m_content);
    dc.SetFont(m_content->GetFont());
    const int padding = FromDIP(6);
    const int minimumColumn = FromDIP(120);
    int nameWidth = minimumColumn;
    int valueWidth = minimumColumn;
    for (const auto& row : m_rows)
    {
        nameWidth = wxMax(nameWidth, dc.GetTextExtent(row.first).GetWidth() + 2 * padding);
        valueWidth = wxMax(valueWidth, dc.GetTextExtent(row.second).GetWidth() + 2 * padding);
    }
    m_tableWidth = wxMax(m_content->GetClientSize().GetWidth(),
        wxMax(nameWidth + valueWidth, 2 * nameWidth));
    m_nameWidth = wxMax(nameWidth, wxMin(m_tableWidth / 2, m_tableWidth - valueWidth));
    m_headerHeight = wxMax(FromDIP(28), dc.GetCharHeight() + 2 * padding);
    m_rowHeight = wxMax(FromDIP(29), dc.GetCharHeight() + 2 * padding);
    m_content->SetVirtualSize(m_tableWidth,
        m_headerHeight + static_cast<int>(m_rows.size()) * m_rowHeight + 1);
    m_content->Refresh();
}

void PropertyPanel::PaintTable(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_content);
    m_content->PrepareDC(dc);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    const int bottom = m_headerHeight + static_cast<int>(m_rows.size()) * m_rowHeight;
    dc.SetPen(wxPen(wxColour(145, 145, 145)));
    dc.SetBrush(wxBrush(wxColour(238, 238, 238)));
    dc.DrawRectangle(0, 0, m_tableWidth, m_headerHeight);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, m_headerHeight, m_tableWidth, bottom - m_headerHeight + 1);
    dc.DrawLine(m_nameWidth, m_headerHeight, m_nameWidth, bottom);
    for (std::size_t i = 1; i < m_rows.size(); ++i)
    {
        const int y = m_headerHeight + static_cast<int>(i) * m_rowHeight;
        dc.DrawLine(0, y, m_tableWidth, y);
    }

    dc.SetFont(m_content->GetFont());
    dc.SetTextForeground(*wxBLACK);
    const wxSize titleSize = dc.GetTextExtent(m_title);
    dc.DrawText(m_title, wxMax(FromDIP(6), (m_tableWidth - titleSize.GetWidth()) / 2),
        (m_headerHeight - titleSize.GetHeight()) / 2);

    const int padding = FromDIP(6);
    for (std::size_t i = 0; i < m_rows.size(); ++i)
    {
        const int y = m_headerHeight + static_cast<int>(i) * m_rowHeight;
        const auto& row = m_rows[i];
        dc.SetClippingRegion(padding, y + 1, m_nameWidth - 2 * padding, m_rowHeight - 2);
        dc.DrawText(row.first, padding,
            y + (m_rowHeight - dc.GetTextExtent(row.first).GetHeight()) / 2);
        dc.DestroyClippingRegion();
        dc.SetClippingRegion(m_nameWidth + padding, y + 1,
            m_tableWidth - m_nameWidth - 2 * padding, m_rowHeight - 2);
        dc.DrawText(row.second, m_nameWidth + padding,
            y + (m_rowHeight - dc.GetTextExtent(row.second).GetHeight()) / 2);
        dc.DestroyClippingRegion();
    }
}

void PropertyPanel::ShowTool(const wxString& type, int textPointSize)
{
    m_textSettings->Hide();
    if (type == "TEXT")
    {
        ShowTextSettings(textPointSize);
        SetRows(wxT("文本工具"), {
            {wxT("状态"), wxT("等待绘制文本框")},
            {wxT("操作"), wxT("按住鼠标拖出文本框")},
            {wxT("外观"), wxT("透明背景，选中显示范围")},
            {wxT("位置"), wxT("自由放置，不对齐网格")}
        });
        return;
    }
    if (type.IsEmpty())
    {
        SetRows(wxT("选择工具"), {
            {wxT("状态"), wxT("未选中元件")},
            {wxT("操作"), wxT("点击画布中的元件")}
        });
        return;
    }
    if (type == "DELETE")
    {
        SetRows(wxT("删除工具"), {
            {wxT("状态"), wxT("等待删除")},
            {wxT("操作"), wxT("点击元件或文本框删除")},
            {wxT("模式"), wxT("可连续删除，选择工具可退出")}
        });
        return;
    }
    if (type == "WIRE")
    {
        SetRows(wxT("连线工具"), {{wxT("状态"), wxT("当前工具尚未实现")}});
        return;
    }
    const bool gate = type == "AND" || type == "OR" || type == "NOT" ||
        type == "NAND" || type == "NOR" || type == "XOR";
    if (gate || type == "INPUT" || type == "OUTPUT")
    {
        const int inputs = type == "INPUT" ? 0 :
            ((type == "OUTPUT" || type == "NOT") ? 1 : 2);
        const int outputs = type == "OUTPUT" ? 0 : 1;
        SetRows(DisplayName(type), {
            {wxT("状态"), wxT("待放置")},
            {wxT("输入引脚"), wxString::Format("%d", inputs)},
            {wxT("输出引脚"), wxString::Format("%d", outputs)},
            {wxT("数据位宽"), wxT("1")},
            {wxT("操作"), wxT("点击画布空白处放置")}
        });
        return;
    }
    SetRows(DisplayName(type), {{wxT("状态"), wxT("此元件尚不能放置")}});
}

void PropertyPanel::ShowComponent(const Component* component, int x, int y)
{
    m_textSettings->Hide();
    if (!component)
    {
        Clear();
        return;
    }
    std::vector<std::pair<wxString, wxString>> rows = {
        {wxT("状态"), wxT("已选中")},
        {wxT("X 坐标"), wxString::Format("%d", x)},
        {wxT("Y 坐标"), wxString::Format("%d", y)},
        {wxT("输入引脚"), wxString::Format("%llu", static_cast<unsigned long long>(component->inputs.size()))},
        {wxT("输出引脚"), wxString::Format("%llu", static_cast<unsigned long long>(component->outputs.size()))}
    };
    const wxString type = wxString::FromUTF8(component->name.c_str());
    if (type == "INPUT" && !component->outputs.empty())
        rows.push_back({wxT("当前值"), component->outputs[0].value == LogicValue::High ? wxT("1") : wxT("0")});
    if (type == "OUTPUT" && !component->inputs.empty())
        rows.push_back({wxT("当前值"), component->inputs[0].value == LogicValue::High ? wxT("1") : wxT("0")});
    for (const auto& pin : component->inputs)
        rows.push_back({wxT("输入 ") + wxString::FromUTF8(pin.name.c_str()),
            pin.value == LogicValue::High ? wxT("1") : wxT("0")});
    for (const auto& pin : component->outputs)
        rows.push_back({wxT("输出 ") + wxString::FromUTF8(pin.name.c_str()),
            pin.value == LogicValue::High ? wxT("1") : wxT("0")});
    SetRows(DisplayName(type), rows);
}

void PropertyPanel::Clear()
{
    m_textSettings->Hide();
    SetRows(wxT("未选中元件"), {
        {wxT("状态"), wxT("等待选择")},
        {wxT("操作"), wxT("选择工具或画布元件")}
    });
}

void PropertyPanel::ShowTextSettings(int pointSize)
{
    if (m_fontSize->GetValue() != pointSize) m_fontSize->SetValue(pointSize);
    m_textSettings->Show();
}

void PropertyPanel::ShowTextBox(const wxRect& rect, int pointSize)
{
    ShowTextSettings(pointSize);
    SetRows(wxT("文本框"), {
        {wxT("状态"), wxT("已选中")},
        {wxT("X 坐标"), wxString::Format("%d", rect.x)},
        {wxT("Y 坐标"), wxString::Format("%d", rect.y)},
        {wxT("宽度"), wxString::Format("%d", rect.width)},
        {wxT("高度"), wxString::Format("%d", rect.height)},
        {wxT("外观"), wxT("透明背景，选中显示范围")},
        {wxT("移动"), wxT("拖动边框；选择工具可拖框内")},
        {wxT("缩放"), wxT("拖动四角或边上的小方块")},
        {wxT("编辑"), wxT("双击；文本工具可单击输入")},
        {wxT("完成"), wxT("Ctrl+Enter 或点击框外")}
    });
}
