#include "Canvas.h"

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
    bool SameDocument(const CircuitDocument& first, const CircuitDocument& second)
    {
        if (first.components.size() != second.components.size() ||
            first.wires.size() != second.wires.size() || first.texts.size() != second.texts.size())
            return false;
        for (std::size_t i = 0; i < first.components.size(); ++i)
        {
            const auto& a = first.components[i];
            const auto& b = second.components[i];
            if (a.id != b.id || a.type != b.type || a.x != b.x || a.y != b.y ||
                a.inputCount != b.inputCount || a.high != b.high) return false;
        }
        for (std::size_t i = 0; i < first.wires.size(); ++i)
        {
            const auto& a = first.wires[i];
            const auto& b = second.wires[i];
            if (a.sourceId != b.sourceId || a.sourcePin != b.sourcePin ||
                a.targetId != b.targetId || a.targetPin != b.targetPin) return false;
        }
        for (std::size_t i = 0; i < first.texts.size(); ++i)
        {
            const auto& a = first.texts[i];
            const auto& b = second.texts[i];
            if (a.rect != b.rect || a.pointSize != b.pointSize || a.text != b.text) return false;
        }
        return true;
    }
}

void Canvas::RecordUndo()
{
    if (!m_loadingDocument)
    {
        FinishTextEditing();
        RecordUndo(ExportDocument());
    }
}

void Canvas::RecordUndo(CircuitDocument document)
{
    if (m_loadingDocument) return;
    // 连续产生同一个编辑前状态时，不占用额外的撤销步数。
    if (!m_undoHistory.empty() && SameDocument(m_undoHistory.back(), document)) return;
    if (m_undoHistory.size() == m_undoLimit) m_undoHistory.erase(m_undoHistory.begin());
    m_undoHistory.push_back(std::move(document));
}

bool Canvas::Undo()
{
    const bool cancelTextTransform = m_textTransformIndex >= 0 &&
        m_textTransformIndex < static_cast<int>(m_textBoxes.size()) &&
        m_textBoxes[m_textTransformIndex].rect != m_textTransformRect;
    CancelMouseInteraction(true);
    if (cancelTextTransform)
    {
        ReportStatus(wxT("已撤销文本框移动或缩放"));
        SetFocusIgnoringChildren();
        return true;
    }
    FinishTextEditing();
    if (m_undoHistory.empty()) return false;
    wxString error;
    // ReplaceDocument 先完整构建新模型；失败时当前画布和历史均保留。
    if (!ReplaceDocument(m_undoHistory.back(), error, false))
    {
        ReportStatus(error);
        return false;
    }
    m_undoHistory.pop_back();
    RefreshModifiedState();
    ReportStatus(wxT("已撤销上一步操作"));
    SetFocusIgnoringChildren();
    return true;
}

bool Canvas::CanUndo() const
{
    if (IsEditingText())
        return static_cast<wxTextCtrl*>(wxWindow::FindFocus())->CanUndo();
    if (m_textTransformIndex >= 0 && m_textTransformIndex < static_cast<int>(m_textBoxes.size()) &&
        m_textBoxes[m_textTransformIndex].rect != m_textTransformRect) return true;
    if (m_dragUndoDocument && m_pressedIndex >= 0 &&
        m_pressedIndex < static_cast<int>(m_components.size()))
    {
        const auto& before = m_dragUndoDocument->components[m_pressedIndex];
        const auto& current = m_components[m_pressedIndex];
        if (before.x != current.x || before.y != current.y) return true;
    }
    return !m_undoHistory.empty();
}

void Canvas::UndoText(wxTextCtrl& editor)
{
    if (!editor.CanUndo()) return;
    m_nativeTextUndo = true;
    editor.Undo();
    m_nativeTextUndo = false;
    for (auto& box : m_textBoxes)
        if (box.editor == &editor) box.lastValue = editor.GetValue();
    const auto current = ExportDocument();
    // 原生控件可能一次撤销一整段输入，消费对应的画布快照，避免退出编辑后反向恢复文字。
    for (std::size_t i = m_undoHistory.size(); i > 0; --i)
    {
        if (!SameDocument(m_undoHistory[i - 1], current)) continue;
        m_undoHistory.erase(m_undoHistory.begin() + i - 1, m_undoHistory.end());
        m_textEditUndoDocument = current;
        break;
    }
    RefreshModifiedState();
    UpdateTextInput();
    Refresh(false);
}

void Canvas::FinishTextEditing()
{
    if (!m_textEditUndoDocument) return;
    auto before = std::move(*m_textEditUndoDocument);
    m_textEditUndoDocument.reset();
    if (!m_loadingDocument && !SameDocument(before, ExportDocument()))
        RecordUndo(std::move(before));
    if (!m_loadingDocument) RefreshModifiedState();
}

void Canvas::RefreshModifiedState()
{
    const bool modified = !SameDocument(ExportDocument(), m_savedDocument);
    if (m_modified == modified) return;
    m_modified = modified;
    if (m_modifiedCallback) m_modifiedCallback();
}

void Canvas::MarkModified(bool textTransform)
{
    if (m_loadingDocument) return;
    // 取消文本框拖动时，只撤销这次几何变更，保留其他编辑产生的脏状态。
    if (!textTransform && m_textTransformIndex >= 0) m_textTransformWasModified = true;
    if (m_modified) return;
    m_modified = true;
    if (m_modifiedCallback) m_modifiedCallback();
}

void Canvas::MarkSaved()
{
    if (m_loadingDocument) return;
    m_savedDocument = ExportDocument();
    if (m_textTransformIndex >= 0)
        m_textTransformWasModified = m_textBoxes[m_textTransformIndex].rect != m_textTransformRect;
    if (!m_modified) return;
    m_modified = false;
    if (m_modifiedCallback) m_modifiedCallback();
}

int Canvas::NextComponentId() const
{
    std::unordered_set<int> used;
    for (const auto& component : m_components) used.insert(component.id);
    int id = 1;
    while (used.contains(id)) ++id;
    return id;
}

CircuitDocument Canvas::ExportDocument() const
{
    CircuitDocument document;
    document.components.reserve(m_components.size());
    document.texts.reserve(m_textBoxes.size());
    document.wires.reserve(m_circuit.GetConnections().size());
    std::unordered_set<int> usedIds;
    for (const auto& pc : m_components)
        if (pc.id > 0) usedIds.insert(pc.id);
    std::unordered_map<const Component*, int> ids;
    int nextId = 1;
    for (const auto& pc : m_components)
    {
        int id = pc.id;
        // 兼容没有持久化 ID 的原有画布对象。
        if (id <= 0)
        {
            while (usedIds.contains(nextId)) ++nextId;
            id = nextId++;
            usedIds.insert(id);
        }
        ids.emplace(pc.comp, id);
        CircuitComponentData component;
        component.id = id;
        component.type = pc.comp->name;
        component.x = pc.x;
        component.y = pc.y;
        component.inputCount = static_cast<int>(pc.comp->inputs.size());
        component.high = pc.comp->name == "INPUT" && !pc.comp->outputs.empty() &&
            pc.comp->outputs[0].value == LogicValue::High;
        document.components.push_back(std::move(component));
    }
    for (const auto& connection : m_circuit.GetConnections())
    {
        document.wires.push_back({ ids.at(connection.output.component),
            static_cast<int>(connection.output.index), ids.at(connection.input.component),
            static_cast<int>(connection.input.index) });
    }
    for (const auto& box : m_textBoxes)
        document.texts.push_back({ box.rect, box.pointSize, box.editor->GetValue() });
    return document;
}

void Canvas::ResetDocument()
{
    m_dragTimer.Stop();
    m_dragUndoDocument.reset();
    m_textTransformUndoDocument.reset();
    m_textEditUndoDocument.reset();
    m_pendingWire.reset();
    m_selectedIndex = m_selectedTextIndex = m_selectedWireIndex = -1;
    m_pressedIndex = -1;
    m_dragging = m_togglePending = m_creatingText = false;
    m_textTransformIndex = m_textTransformHandle = -1;
    m_textTransformMoved = false;
    m_textTransformWasModified = false;
    if (HasCapture()) ReleaseMouse();
    // 属性面板可能持有对象指针，必须在释放对象前清空。
    if (m_selectionCallback) m_selectionCallback(nullptr, 0, 0);
    if (m_wireSelectionCallback) m_wireSelectionCallback(nullptr);
    const bool wasEditingText = IsEditingText();
    auto oldTextBoxes = std::move(m_textBoxes);
    m_textBoxes.clear();
    if (wasEditingText) SetFocusIgnoringChildren();
    m_circuit = Circuit();
    for (const auto& pc : m_components) delete pc.comp;
    m_components.clear();
    for (const auto& box : oldTextBoxes) box.editor->Destroy();
    m_lastPastePayload.clear();
    m_pasteCount = 0;
    m_textStart = m_textEnd = wxPoint();
    m_dragFrame = 0;
}

void Canvas::ClearDocument()
{
    ResetDocument();
    m_undoHistory.clear();
    MarkSaved();
    Refresh(false);
}

bool Canvas::ImportDocument(const CircuitDocument& document, wxString& error)
{
    return ReplaceDocument(document, error, true);
}

bool Canvas::ReplaceDocument(const CircuitDocument& document, wxString& error, bool resetHistory)
{
    if (!CircuitFile::Validate(document, error)) return false;

    std::vector<std::unique_ptr<Component>> ownedComponents;
    std::vector<PlacedComponent> components;
    std::unordered_map<int, Component*> ids;
    std::vector<Component*> nodes;
    Circuit circuit;
    std::vector<TextBox> textBoxes;
    const std::size_t oldTextCount = m_textBoxes.size();
    try
    {
        ownedComponents.reserve(document.components.size());
        components.reserve(document.components.size());
        nodes.reserve(document.components.size());
        for (const auto& data : document.components)
        {
            auto component = CircuitFile::CreateComponent(data);
            if (!component) throw std::runtime_error("Unsupported component");
            Component* pointer = component.get();
            ownedComponents.push_back(std::move(component));
            components.push_back({ pointer, data.x, data.y, data.id });
            nodes.push_back(pointer);
            ids.emplace(data.id, pointer);
        }
        for (const auto& wire : document.wires)
        {
            if (circuit.Connect({ ids.at(wire.sourceId), PinType::Output,
                    static_cast<std::size_t>(wire.sourcePin) },
                { ids.at(wire.targetId), PinType::Input,
                    static_cast<std::size_t>(wire.targetPin) }) != ConnectionResult::Added)
                throw std::runtime_error("Invalid wire connection");
        }
        if (!circuit.Evaluate(nodes)) throw std::runtime_error("Invalid circuit");

        // 先创建所有离屏编辑器，任何构建错误均不替换原来的电路与文本。
        textBoxes.reserve(document.texts.size());
        m_textBoxes.reserve(oldTextCount + document.texts.size());
        m_loadingDocument = true;
        for (const auto& data : document.texts)
        {
            CreateTextBox(data.rect);
            auto& box = m_textBoxes.back();
            box.pointSize = data.pointSize;
            box.editor->SetFont(wxFontInfo(data.pointSize).Family(wxFONTFAMILY_SWISS));
            box.editor->ChangeValue(data.text);
            box.lastValue = data.text;
        }
        for (std::size_t i = oldTextCount; i < m_textBoxes.size(); ++i)
            textBoxes.push_back(m_textBoxes[i]);
        m_textBoxes.resize(oldTextCount);
    }
    catch (const std::exception& exception)
    {
        // Destroy 可派发焦点事件；先从模型移除这些控件。
        while (m_textBoxes.size() > oldTextCount)
        {
            auto* editor = m_textBoxes.back().editor;
            m_textBoxes.pop_back();
            editor->Destroy();
        }
        m_loadingDocument = false;
        error = wxString::Format(wxT("无法载入电路：%s"), wxString::FromUTF8(exception.what()));
        return false;
    }

    ResetDocument();
    m_components = std::move(components);
    m_circuit = std::move(circuit);
    m_textBoxes = std::move(textBoxes);
    for (auto& component : ownedComponents) component.release();
    m_loadingDocument = false;
    if (resetHistory)
    {
        m_undoHistory.clear();
        MarkSaved();
    }
    error.clear();
    RecomputeCircuit();
    return true;
}
