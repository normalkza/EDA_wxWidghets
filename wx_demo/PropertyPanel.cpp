#include "PropertyPanel.h"

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
    m_title = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    m_content = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_content->SetBackgroundColour(GetBackgroundColour());
    m_content->SetScrollRate(FromDIP(8), FromDIP(8));
    m_grid = new wxFlexGridSizer(2, FromDIP(4), FromDIP(8));
    m_grid->AddGrowableCol(1, 1);
    auto* contentSizer = new wxBoxSizer(wxVERTICAL);
    contentSizer->Add(m_grid, 0, wxEXPAND | wxALL, FromDIP(8));
    m_content->SetSizer(contentSizer);
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
    bool sameRows = title == m_title->GetLabel() && rows.size() == m_rowNames.size();
    for (std::size_t i = 0; sameRows && i < rows.size(); ++i)
        sameRows = rows[i].first == m_rowNames[i];
    Freeze();
    m_title->SetLabel(title);
    if (!sameRows)
    {
        m_grid->Clear(true);
        m_rowNames.clear();
        m_valueLabels.clear();
        for (const auto& row : rows)
        {
            auto* name = new wxStaticText(m_content, wxID_ANY, row.first);
            auto* value = new wxStaticText(m_content, wxID_ANY, row.second);
            m_grid->Add(name, 0, wxALIGN_TOP | wxTOP | wxBOTTOM, FromDIP(3));
            m_grid->Add(value, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(3));
            m_rowNames.push_back(row.first);
            m_valueLabels.push_back(value);
        }
        m_content->Scroll(0, 0);
    }
    else
    {
        // 原位更新坐标和电平，保留滚动位置，避免拖动时反复销毁控件。
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (m_valueLabels[i]->GetLabel() != rows[i].second)
                m_valueLabels[i]->SetLabel(rows[i].second);
        }
    }
    Layout();
    m_content->Layout();
    m_content->FitInside();
    Thaw();
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
    if (type == "WIRE" || type == "DELETE")
    {
        const wxString title = type == "WIRE" ? wxT("连线工具") : wxT("删除工具");
        SetRows(title, {{wxT("状态"), wxT("当前工具尚未实现")}});
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
