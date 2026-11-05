#pragma once

#include "Toolbox.h"
#include "ComponentRegistry.h"
#include "SelectionState.h"
#include <wx/log.h>
#include <functional>
#include <memory>
#include <stdexcept>

inline void TestRegistryToolbox()
{
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(std::string("toolbox registry: ") + message);
    };
    struct RestoreTool
    {
        wxString original = g_selectedType;
        ~RestoreTool() { g_selectedType = original; }
    } restore;
    // 测试不依赖程序安装目录的图片，也不能弹出缺图日志对话框。
    wxLogNull quiet;
    auto frame = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Toolbox regression");
    auto* toolbox = new Toolbox(frame.get());
    wxTreeCtrl* tree = nullptr;
    for (wxWindow* child : toolbox->GetChildren())
        if (auto* candidate = dynamic_cast<wxTreeCtrl*>(child)) tree = candidate;
    require(tree != nullptr, "tree exists");
    int callbacks = 0;
    wxString selected;
    toolbox->SetSelectionCallback([&](const wxString& type) { ++callbacks; selected = type; });
    g_selectedType = "AND";
    toolbox->SelectTool("AND");
    const auto activeItem = tree->GetSelection();
    require(activeItem.IsOk() && toolbox->GetSelectedType() == "AND", "implemented component selectable");

    wxTreeItemId unavailable, available;
    std::size_t placeholderCount = 0;
    std::function<void(wxTreeItemId)> inspect = [&](wxTreeItemId parent) {
        wxTreeItemIdValue cookie;
        for (auto item = tree->GetFirstChild(parent, cookie); item.IsOk(); item = tree->GetNextChild(parent, cookie))
        {
            const auto label = tree->GetItemText(item);
            if (label.EndsWith(wxT("（待实现）")))
            {
                ++placeholderCount;
                if (!unavailable.IsOk()) unavailable = item;
            }
            if (label == wxT("或门")) available = item;
            inspect(item);
        }
    };
    inspect(tree->GetRootItem());
    std::size_t expectedPlaceholders = 0;
    for (const auto& definition : ComponentRegistry::All())
    {
        const auto type = wxString::FromUTF8(definition.type.data(), definition.type.size());
        require(toolbox->IsToolAvailable(type) == definition.IsAvailable(), "UI availability agrees with factory");
        if (!definition.IsAvailable())
        {
            ++expectedPlaceholders;
            toolbox->SelectTool(type);
            require(toolbox->GetSelectedType() == "AND" && tree->GetSelection() == activeItem,
                "programmatic placeholder selection preserves active tool");
        }
    }
    require(placeholderCount == expectedPlaceholders && unavailable.IsOk(), "all placeholders visibly marked");
    require(tree->GetItemTextColour(unavailable) == wxColour(148, 156, 169), "placeholder is visibly disabled");
    tree->SelectItem(unavailable);
    require(tree->GetSelection() == activeItem && toolbox->GetSelectedType() == "AND" &&
        g_selectedType == "AND" && callbacks == 0, "disabled tree selection is vetoed without tool callback");
    toolbox->SelectTool("UNKNOWN");
    require(toolbox->GetSelectedType() == "AND", "unknown tool cannot become active");
    require(available.IsOk(), "registered OR entry exists");
    tree->SelectItem(available);
    require(toolbox->GetSelectedType() == "OR" && g_selectedType == "OR" && callbacks == 1 && selected == "OR",
        "available tree selection updates active tool exactly once");
}
