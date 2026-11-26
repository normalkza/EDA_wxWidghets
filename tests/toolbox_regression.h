#pragma once

#include "Toolbox.h"
#include "ComponentRegistry.h"
#include "SelectionState.h"
#include <wx/log.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

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

    bool hasPlaceholder = false;
    bool hasRemovedCategory = false;
    wxTreeItemId wireCategory, storageCategory, orItem, constantItem, counterItem, wireItem, clockItem;
    std::function<void(wxTreeItemId, int)> inspect = [&](wxTreeItemId parent, int depth) {
        wxTreeItemIdValue cookie;
        for (auto item = tree->GetFirstChild(parent, cookie); item.IsOk(); item = tree->GetNextChild(parent, cookie))
        {
            const wxString label = tree->GetItemText(item);
            if (label.EndsWith(wxT("（待实现）"))) hasPlaceholder = true;
            if (depth == 0)
            {
                if (label == wxT("复用器") || label == wxT("运算器")) hasRemovedCategory = true;
                if (label == wxT("线路")) wireCategory = item;
                if (label == wxT("存储")) storageCategory = item;
            }
            if (label == wxT("或门")) orItem = item;
            if (label == wxT("常量")) constantItem = item;
            if (label == wxT("计数器")) counterItem = item;
            if (label == wxT("导线")) wireItem = item;
            if (label == wxT("时钟")) clockItem = item;
            inspect(item, depth + 1);
        }
    };
    inspect(tree->GetRootItem(), 0);
    require(!hasPlaceholder, "no placeholder entries are shown in the toolbox tree");
    require(!hasRemovedCategory, "mux and arithmetic categories are removed");
    require(wireCategory.IsOk() && storageCategory.IsOk(), "wire and storage categories exist");
    wxTreeItemIdValue wireCookie;
    const auto firstWireChild = tree->GetFirstChild(wireCategory, wireCookie);
    require(firstWireChild.IsOk() && tree->GetItemText(firstWireChild) == wxT("导线"),
        "wire category contains the wire tool");
    const auto secondWireChild = tree->GetNextChild(wireCategory, wireCookie);
    require(secondWireChild.IsOk() && tree->GetItemText(secondWireChild) == wxT("时钟"),
        "wire category also contains the clock tool");
    require(!tree->GetNextChild(wireCategory, wireCookie).IsOk(), "wire category has no other tools");
    require(wireItem.IsOk() && clockItem.IsOk(), "wire and clock entries exist");
    require(constantItem.IsOk(), "constant is listed in the storage category");
    require(counterItem.IsOk(), "counter is listed in the storage category");

    for (const auto& definition : ComponentRegistry::All())
    {
        const auto type = wxString::FromUTF8(definition.type.data(), definition.type.size());
        require(toolbox->IsToolAvailable(type) == definition.IsAvailable(), "UI availability agrees with factory");
        if (!definition.IsAvailable())
        {
            toolbox->SelectTool(type);
            require(toolbox->GetSelectedType() == "AND" && tree->GetSelection() == activeItem,
                "programmatic selection of unavailable component preserves active tool");
        }
    }
    toolbox->SelectTool("UNKNOWN");
    require(toolbox->GetSelectedType() == "AND", "unknown tool cannot become active");
    require(orItem.IsOk(), "registered OR entry exists");
    tree->SelectItem(orItem);
    require(toolbox->GetSelectedType() == "OR" && g_selectedType == "OR" && callbacks == 1 && selected == "OR",
        "available tree selection updates active tool exactly once");
    tree->SelectItem(constantItem);
    require(toolbox->GetSelectedType() == "CONSTANT" && g_selectedType == "CONSTANT" && callbacks == 2,
        "constant selection in storage category activates the constant tool");
    tree->SelectItem(clockItem);
    require(toolbox->GetSelectedType() == "CLOCK" && g_selectedType == "CLOCK" && callbacks == 3,
        "clock selection in wire category activates the clock tool");
    tree->SelectItem(counterItem);
    require(toolbox->GetSelectedType() == "COUNTER" && g_selectedType == "COUNTER" && callbacks == 4,
        "counter selection in storage category activates the counter tool");
}
