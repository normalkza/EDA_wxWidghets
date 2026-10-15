#include "PropertyPanel.h"

namespace
{
    const wxColour PanelBackground(243, 246, 251);
    const wxColour CardBackground(255, 255, 255);
    const wxColour TextColour(36, 50, 71);
    const wxColour SecondaryColour(100, 116, 139);
    const wxColour DividerColour(220, 228, 239);
    const wxColour AccentColour(53, 120, 229);
    const wxColour HighColour(22, 139, 88);

    wxFont InterfaceFont(int pointSize = 9, bool bold = false)
    {
        return wxFontInfo(pointSize).FaceName(wxT("Microsoft YaHei UI"))
            .Family(wxFONTFAMILY_SWISS).Bold(bold);
    }

    wxStaticText* Caption(wxWindow* parent, const wxString& text, bool strong = false)
    {
        auto* caption = new wxStaticText(parent, wxID_ANY, text);
        caption->SetFont(InterfaceFont(9, strong));
        caption->SetForegroundColour(strong ? TextColour : SecondaryColour);
        return caption;
    }

    wxString WrappableText(const wxString& text)
    {
        // wxStaticText 按空白断行；零宽空格让中文说明在窄侧栏内自然换行。
        wxString result;
        for (std::size_t i = 0; i < text.length(); ++i)
        {
            const wxUniChar character = text[i];
            result += character;
            const auto code = character.GetValue();
            if (i + 1 < text.length() &&
                ((code >= 0x2e80 && code <= 0x9fff) || (code >= 0xff00 && code <= 0xffef)))
                result += wxUniChar(0x200b);
        }
        return result;
    }

    void StyleValue(const wxString& name, const wxString& text, wxStaticText* value)
    {
        const bool pinValue = name == wxT("当前值") ||
            name.StartsWith(wxT("输入 ")) || name.StartsWith(wxT("输出 "));
        if (pinValue)
        {
            const bool high = text == wxT("1");
            value->SetFont(InterfaceFont(9, true));
            value->SetForegroundColour(high ? HighColour : SecondaryColour);
            value->GetParent()->SetBackgroundColour(high ?
                wxColour(232, 247, 239) : wxColour(239, 243, 248));
            value->SetToolTip(text);
            value->GetParent()->Refresh();
        }
        else
        {
            const bool status = name == wxT("状态");
            value->SetFont(InterfaceFont(9, status));
            value->SetForegroundColour(status ? AccentColour : TextColour);
            if (status) value->GetParent()->SetBackgroundColour(wxColour(234, 241, 255));
            value->SetToolTip(text);
        }
    }

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
    SetBackgroundColour(PanelBackground);
    SetForegroundColour(TextColour);
    SetFont(InterfaceFont());
    // 标题、属性和参数使用同一个滚动区域，小窗口下所有控件仍可触达。
    m_viewport = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxHSCROLL | wxVSCROLL | wxBORDER_NONE);
    m_viewport->SetBackgroundColour(PanelBackground);
    m_viewport->SetScrollRate(FromDIP(8), FromDIP(8));
    m_viewport->SetMinSize(wxSize(0, 0));
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* heading = Caption(m_viewport, wxT("属性与状态"), true);
    heading->SetFont(InterfaceFont(10, true));
    root->Add(heading, 0, wxEXPAND | wxALL, FromDIP(12));

    auto* header = new wxPanel(m_viewport);
    header->SetBackgroundColour(CardBackground);
    auto* headerSizer = new wxBoxSizer(wxVERTICAL);
    headerSizer->Add(Caption(header, wxT("当前对象")), 0, wxEXPAND | wxALL, FromDIP(12));
    m_title = new wxStaticText(header, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_title->SetFont(InterfaceFont(10, true));
    m_title->SetForegroundColour(AccentColour);
    headerSizer->Add(m_title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    auto* divider = new wxPanel(header);
    divider->SetBackgroundColour(DividerColour);
    headerSizer->Add(divider, 0, wxEXPAND);
    divider->SetMinSize(wxSize(-1, FromDIP(1)));
    header->SetSizer(headerSizer);
    root->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

    m_content = new wxPanel(m_viewport);
    m_content->SetBackgroundColour(CardBackground);
    m_grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
    m_grid->AddGrowableCol(1, 1);
    auto* contentSizer = new wxBoxSizer(wxVERTICAL);
    contentSizer->Add(m_grid, 0, wxEXPAND | wxALL, FromDIP(12));
    m_content->SetSizer(contentSizer);
    root->Add(m_content, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

    m_textSettings = new wxPanel(m_viewport);
    m_textSettings->SetBackgroundColour(CardBackground);
    auto* textCardSizer = new wxBoxSizer(wxVERTICAL);
    textCardSizer->Add(Caption(m_textSettings, wxT("文本参数"), true),
        0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    auto* textSizer = new wxBoxSizer(wxHORIZONTAL);
    textSizer->Add(Caption(m_textSettings, wxT("字号 (pt)")),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_fontSize = new wxSpinCtrl(m_textSettings, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 6, 96, 14);
    m_fontSize->SetFont(InterfaceFont());
    m_fontSize->SetForegroundColour(TextColour);
    m_fontSize->SetMinSize(FromDIP(wxSize(90, 30)));
    textSizer->Add(m_fontSize, 1, wxEXPAND);
    textCardSizer->Add(textSizer, 0, wxEXPAND | wxALL, FromDIP(12));
    m_textSettings->SetSizer(textCardSizer);
    root->Add(m_textSettings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    m_fontSize->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        if (m_textFontSizeCallback) m_textFontSizeCallback(m_fontSize->GetValue());
    });
    m_fontSize->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        long value;
        if (event.GetString().ToLong(&value) && value >= 6 && value <= 96 &&
            m_textFontSizeCallback)
            m_textFontSizeCallback(static_cast<int>(value));
    });
    m_gateSettings = new wxPanel(m_viewport);
    m_gateSettings->SetBackgroundColour(CardBackground);
    auto* gateCardSizer = new wxBoxSizer(wxVERTICAL);
    gateCardSizer->Add(Caption(m_gateSettings, wxT("逻辑门参数"), true),
        0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    auto* gateSizer = new wxBoxSizer(wxHORIZONTAL);
    gateSizer->Add(Caption(m_gateSettings, wxT("输入引脚数")),
        0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_gateInputCount = new wxSpinCtrl(m_gateSettings, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        MultiInputGate::MinInputCount, MultiInputGate::MaxInputCount, 2);
    m_gateInputCount->SetFont(InterfaceFont());
    m_gateInputCount->SetForegroundColour(TextColour);
    m_gateInputCount->SetMinSize(FromDIP(wxSize(90, 30)));
    gateSizer->Add(m_gateInputCount, 1, wxEXPAND);
    gateCardSizer->Add(gateSizer, 0, wxEXPAND | wxALL, FromDIP(12));
    m_gateSettings->SetSizer(gateCardSizer);
    root->Add(m_gateSettings, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    m_gateInputCount->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) {
        if (!m_updatingGateSettings && m_gateInputCountCallback)
            m_gateInputCountCallback(m_gateInputCount->GetValue());
    });
    m_gateInputCount->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        long count;
        if (!m_updatingGateSettings && event.GetString().ToLong(&count) &&
            count >= MultiInputGate::MinInputCount && count <= MultiInputGate::MaxInputCount &&
            m_gateInputCountCallback)
            m_gateInputCountCallback(static_cast<int>(count));
    });
    m_viewport->SetSizer(root);
    auto* outerSizer = new wxBoxSizer(wxVERTICAL);
    outerSizer->Add(m_viewport, 1, wxEXPAND);
    SetSizer(outerSizer);
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
    m_title->SetToolTip(title);
    if (!sameRows)
    {
        m_grid->Clear(true);
        m_rowNames.clear();
        m_rowValues.clear();
        m_valueLabels.clear();
        for (const auto& row : rows)
        {
            auto* name = Caption(m_content, row.first);
            name->SetMinSize(wxSize(FromDIP(68), -1));
            const bool pinValue = row.first == wxT("当前值") ||
                row.first.StartsWith(wxT("输入 ")) || row.first.StartsWith(wxT("输出 "));
            if (row.first.StartsWith(wxT("输入 "))) name->SetForegroundColour(AccentColour);
            if (row.first.StartsWith(wxT("输出 "))) name->SetForegroundColour(wxColour(197, 83, 95));
            const bool badge = pinValue || row.first == wxT("状态");
            wxWindow* valueParent = m_content;
            wxPanel* valueCell = nullptr;
            if (badge)
            {
                valueCell = new wxPanel(m_content);
                valueParent = valueCell;
            }
            auto* value = new wxStaticText(valueParent, wxID_ANY, WrappableText(row.second),
                wxDefaultPosition, wxDefaultSize, wxST_WRAP);
            value->SetMinSize(wxSize(FromDIP(100), -1));
            StyleValue(row.first, row.second, value);
            m_grid->Add(name, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(3));
            if (valueCell)
            {
                auto* badgeSizer = new wxBoxSizer(wxHORIZONTAL);
                badgeSizer->Add(value, 1, wxEXPAND | wxALL, FromDIP(6));
                valueCell->SetSizer(badgeSizer);
                m_grid->Add(valueCell, 0, wxEXPAND);
            }
            else
                m_grid->Add(value, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(3));
            m_rowNames.push_back(row.first);
            m_rowValues.push_back(row.second);
            m_valueLabels.push_back(value);
        }
        m_viewport->Scroll(0, 0);
    }
    else
    {
        // 原位更新坐标和电平，保留滚动位置，避免拖动时反复销毁控件。
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (m_rowValues[i] != rows[i].second)
            {
                m_rowValues[i] = rows[i].second;
                const long style = m_valueLabels[i]->GetWindowStyleFlag();
                m_valueLabels[i]->SetWindowStyleFlag(style & ~wxST_WRAP);
                m_valueLabels[i]->SetLabel(WrappableText(rows[i].second));
                m_valueLabels[i]->SetWindowStyleFlag(style);
                StyleValue(rows[i].first, rows[i].second, m_valueLabels[i]);
            }
        }
    }
    Layout();
    m_content->Layout();
    m_viewport->Layout();
    m_viewport->FitInside();
    Thaw();
}

void PropertyPanel::ShowTool(const wxString& type, int textPointSize, int gateInputCount)
{
    m_textSettings->Hide();
    m_gateSettings->Hide();
    if (type == "TEXT")
    {
        ShowTextSettings(textPointSize);
        SetRows(wxT("文本工具"), {
            {wxT("状态"), wxT("等待绘制文本框")}
        });
        return;
    }
    if (type.IsEmpty())
    {
        SetRows(wxT("选择工具"), {
            {wxT("状态"), wxT("未选中元件")}
        });
        return;
    }
    if (type == "DELETE")
    {
        SetRows(wxT("删除工具"), {
            {wxT("状态"), wxT("等待删除")}
        });
        return;
    }
    if (type == "WIRE")
    {
        SetRows(wxT("连线工具"), {
            {wxT("状态"), wxT("等待连线")}
        });
        return;
    }
    const bool gate = type == "AND" || type == "OR" || type == "NOT" ||
        type == "NAND" || type == "NOR" || type == "XOR";
    if (gate || type == "INPUT" || type == "OUTPUT")
    {
        const int inputs = type == "INPUT" ? 0 :
            ((type == "OUTPUT" || type == "NOT") ? 1 : gateInputCount);
        const int outputs = type == "OUTPUT" ? 0 : 1;
        if (gate && type != "NOT") ShowGateSettings(inputs);
        SetRows(DisplayName(type), {
            {wxT("状态"), wxT("待放置")},
            {wxT("输入引脚"), wxString::Format("%d", inputs)},
            {wxT("输出引脚"), wxString::Format("%d", outputs)},
            {wxT("数据位宽"), wxT("1")}
        });
        return;
    }
    SetRows(DisplayName(type), {{wxT("状态"), wxT("此元件尚不能放置")}});
}

void PropertyPanel::ShowComponent(const Component* component, int x, int y)
{
    m_textSettings->Hide();
    m_gateSettings->Hide();
    if (!component)
    {
        Clear();
        return;
    }
    if (component->SupportsVariableInputs())
        ShowGateSettings(static_cast<int>(component->inputs.size()));
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
    m_gateSettings->Hide();
    SetRows(wxT("未选中元件"), {
        {wxT("状态"), wxT("等待选择")}
    });
}

void PropertyPanel::ShowTextSettings(int pointSize)
{
    if (m_fontSize->GetValue() != pointSize) m_fontSize->SetValue(pointSize);
    m_textSettings->Show();
}

void PropertyPanel::ShowTextBox(const wxRect& rect, int pointSize)
{
    m_gateSettings->Hide();
    ShowTextSettings(pointSize);
    SetRows(wxT("文本框"), {
        {wxT("状态"), wxT("已选中")},
        {wxT("X 坐标"), wxString::Format("%d", rect.x)},
        {wxT("Y 坐标"), wxString::Format("%d", rect.y)},
        {wxT("宽度"), wxString::Format("%d", rect.width)},
        {wxT("高度"), wxString::Format("%d", rect.height)}
    });
}

void PropertyPanel::ShowGateSettings(int inputCount)
{
    m_updatingGateSettings = true;
    if (m_gateInputCount->GetValue() != inputCount) m_gateInputCount->SetValue(inputCount);
    m_updatingGateSettings = false;
    m_gateSettings->Show();
}

void PropertyPanel::ShowConnection(const WireConnection* connection)
{
    m_textSettings->Hide();
    m_gateSettings->Hide();
    if (!connection || !connection->output.IsValid() || !connection->input.IsValid())
    {
        Clear();
        return;
    }
    const auto pinName = [](const PinReference& pin) {
        return wxString::FromUTF8(pin.component->name.c_str()) + wxT(" / ") +
            wxString::FromUTF8(pin.Get().name.c_str());
    };
    SetRows(wxT("导线"), {
        {wxT("状态"), wxT("已连接")},
        {wxT("来源"), pinName(connection->output)},
        {wxT("目标"), pinName(connection->input)},
        {wxT("当前值"), connection->GetValue() == LogicValue::High ? wxT("1") : wxT("0")}
    });
}
