#include "TutorialManager.h"

#include "MainFrame.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "ComponentRegistry.h"
#include "SelectionState.h"
#include <algorithm>
#include <array>
#include <iterator>

namespace
{
    constexpr int AutoStepDelayMs = 2400;
    constexpr int AutoFinishDelayMs = 3000;
}

TutorialManager::TutorialManager(MainFrame* mainFrame)
    : m_mainFrame(mainFrame), m_autoTimer(this)
{
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { AdvanceAutoDemo(); }, m_autoTimer.GetId());
    m_overlay = new TutorialOverlay(mainFrame);

    m_overlay->SetPreviousCallback([this]()
        {
            PreviousStep();
        });

    m_overlay->SetNextCallback([this]()
        {
            NextStep();
        });

    m_overlay->SetCancelCallback([this]()
        {
            CancelTutorial();
        });
}

TutorialManager::~TutorialManager()
{
    m_autoTimer.Stop();
    m_lifetime.reset();
    CancelTutorial();
    if (auto* overlay = m_overlay.get())
    {
        overlay->ClearCallbacks();
        overlay->Destroy();
    }
    m_overlay = nullptr;
}

void TutorialManager::StartAutoTutorial(const wxString& tutorialName)
{
    CancelTutorial();
    m_currentTutorialName = tutorialName;
    BuildSteps();
    Canvas* canvas = m_mainFrame ? m_mainFrame->GetCanvas() : nullptr;
    if (!canvas || m_steps.empty()) return;

    canvas->ClearDocument();
    m_autoDocument = canvas->ExportDocument();
    m_autoPlacements.clear();
    std::copy_if(m_steps.begin(), m_steps.end(), std::back_inserter(m_autoPlacements),
        [](const TutorialStep& step) { return step.placement; });
    if (m_autoPlacements.empty()) return;
    m_autoLabels.clear();
    m_expectedWires.clear();
    m_autoPlacementIndex = m_autoWireIndex = 0;
    m_autoFrameRevision = canvas->GetDocumentRevision();
    m_autoDemo = m_active = true;
    m_autoPaused = false;
    if (auto* overlay = m_overlay.get())
    {
        overlay->SetAutoDemoMode(true);
        overlay->SetStep(m_currentTutorialName + wxT(" - 自动演示"),
            wxT("即将自动放置元件并逐条连线。观察画布中每一步的变化；可随时暂停或结束演示。"),
            wxString::Format(wxT("演示进度：0 / %d"),
                static_cast<int>(m_autoPlacements.size()) + m_wireTarget),
            canvas->GetScreenRect());
    }
    m_autoTimer.StartOnce(AutoStepDelayMs);
}

wxPoint TutorialManager::AutoPlacementPosition(std::size_t index) const
{
    static const std::array<wxPoint, 6> half{{
        {60, 140}, {60, 320}, {270, 120}, {270, 340}, {540, 170}, {540, 380}}};
    static const std::array<wxPoint, 10> full{{
        {40, 120}, {40, 290}, {40, 460}, {240, 100}, {470, 160},
        {240, 370}, {470, 430}, {690, 400}, {750, 160}, {900, 450}}};
    const bool halfAdder = m_currentTutorialName == wxT("半加器 (Half Adder)");
    const wxPoint base = halfAdder ? half.at(index) : full.at(index);
    const wxSize size = m_mainFrame->GetCanvas()->GetClientSize();
    const double xScale = std::min(1.0, std::max(0.5,
        static_cast<double>(size.x - 100) / (halfAdder ? 650.0 : 1020.0)));
    const double yScale = std::min(1.0, std::max(0.6,
        static_cast<double>(size.y - 100) / 590.0));
    return wxPoint(static_cast<int>(base.x * xScale), static_cast<int>(base.y * yScale));
}

void TutorialManager::AdvanceAutoDemo()
{
    if (!m_active || !m_autoDemo || m_autoPaused || !m_mainFrame) return;
    Canvas* canvas = m_mainFrame->GetCanvas();
    if (!canvas || canvas->GetDocumentRevision() != m_autoFrameRevision)
    {
        CancelTutorial();
        return;
    }
    if (m_autoPlacementIndex == m_autoPlacements.size() &&
        m_autoWireIndex == m_expectedWires.size() && !m_expectedWires.empty())
    {
        FinishTutorial();
        return;
    }

    wxString message;
    if (m_autoPlacementIndex < m_autoPlacements.size())
    {
        const auto& step = m_autoPlacements[m_autoPlacementIndex];
        const auto type = step.type.ToStdString(wxConvUTF8);
        const auto* definition = ComponentRegistry::Find(type);
        if (!definition || !definition->IsAvailable()) { CancelTutorial(); return; }
        const wxPoint position = AutoPlacementPosition(m_autoPlacementIndex);
        CircuitComponentData component;
        component.id = static_cast<int>(m_autoPlacementIndex) + 1;
        component.type = type;
        component.x = position.x;
        component.y = position.y;
        component.inputCount = definition->defaultInputCount;
        m_autoDocument.components.push_back(component);
        message = step.message + wxT("\n\n已自动") + step.name + wxT("。");
        if (!step.label.IsEmpty()) m_autoLabels.emplace_back(component.id, step.label);
        ++m_autoPlacementIndex;
    }
    else
    {
        const auto& expected = m_expectedWires[m_autoWireIndex];
        auto wire = expected.wire;
        if (!canvas->RouteTutorialWire(wire))
        {
            CancelTutorial();
            wxMessageBox(wxT("自动连线无法避开元件，请调整画布大小后重试。"),
                wxT("自动教程无法继续"), wxOK | wxICON_ERROR, m_mainFrame);
            return;
        }
        m_autoDocument.wires.push_back(std::move(wire));
        message = wxT("请将 ") + expected.sourceName + wxT(" 的 ") + expected.sourcePinName +
            wxT(" 连接到 ") + expected.targetName + wxT(" 的 ") + expected.targetPinName +
            wxT("。\n\n已自动完成这条连线。");
        ++m_autoWireIndex;
    }

    wxString error;
    if (!canvas->ShowTutorialFrame(m_autoDocument, error))
    {
        CancelTutorial();
        wxMessageBox(error, wxT("自动教程无法继续"), wxOK | wxICON_ERROR, m_mainFrame);
        return;
    }
    m_autoFrameRevision = canvas->GetDocumentRevision();
    for (const auto& [id, label] : m_autoLabels) canvas->SetTutorialLabel(id, label);
    if (m_autoPlacementIndex == m_autoPlacements.size() && m_expectedWires.empty())
    {
        BuildExpectedWires();
        if (m_expectedWires.size() != static_cast<std::size_t>(m_wireTarget))
        {
            CancelTutorial();
            return;
        }
    }
    if (auto* overlay = m_overlay.get())
        overlay->SetStep(m_currentTutorialName + wxT(" - 自动演示"), message,
            wxString::Format(wxT("演示进度：%d / %d"),
                static_cast<int>(m_autoPlacementIndex + m_autoWireIndex),
                static_cast<int>(m_autoPlacements.size() + m_expectedWires.size())),
            canvas->GetScreenRect());
    m_autoTimer.StartOnce(m_autoPlacementIndex == m_autoPlacements.size() &&
        m_autoWireIndex == m_expectedWires.size() ? AutoFinishDelayMs : AutoStepDelayMs);
}

void TutorialManager::StartTutorial(const wxString& tutorialName)
{
    CancelTutorial();

    m_currentTutorialName = tutorialName;

    BuildSteps();

    if (m_steps.empty())
        return;

    if (m_mainFrame && m_mainFrame->GetCanvas())
        m_mainFrame->GetCanvas()->ClearDocument();

    m_active = true;
    m_stepIndex = 0;
    m_wireCount = 0;
    m_wireStepIndex = 0;

    m_currentStepCompleted = false;
    m_waitingForWires = false;

    m_expectedWires.clear();

    ShowCurrentStep();
}

void TutorialManager::BuildSteps()
{
    m_steps.clear();

    auto addTool = [this](
        const wxString& type,
        const wxString& name,
        const wxString& message)
        {
            m_steps.push_back({
                type,
                name,
                message,
                false,
                false
                });
        };

    int inputLabelIndex = 0;
    int outputLabelIndex = 0;
    const std::vector<wxString> inputLabels = m_currentTutorialName == wxT("半加器 (Half Adder)")
        ? std::vector<wxString>{ wxT("A"), wxT("B") }
    : std::vector<wxString>{ wxT("A"), wxT("B"), wxT("Cin") };
    const std::vector<wxString> outputLabels = m_currentTutorialName == wxT("半加器 (Half Adder)")
        ? std::vector<wxString>{ wxT("SUM"), wxT("CARRY") }
    : std::vector<wxString>{ wxT("SUM"), wxT("COUT") };

    auto addPlacement = [this, &inputLabelIndex, &outputLabelIndex, &inputLabels, &outputLabels](
        const wxString& type,
        const wxString& name,
        const wxString& message)
        {
            wxString label;
            if (type == wxT("INPUT") && inputLabelIndex < static_cast<int>(inputLabels.size()))
                label = inputLabels[inputLabelIndex++];
            else if (type == wxT("OUTPUT") && outputLabelIndex < static_cast<int>(outputLabels.size()))
                label = outputLabels[outputLabelIndex++];
            m_steps.push_back({ type, name, message, true, false, label });
        };


    if (m_currentTutorialName == wxT("半加器 (Half Adder)"))
    {
        m_wireTarget = 6;

        addTool(
            wxT("INPUT"),
            wxT("选择输入引脚"),
            wxT("请在左侧元件库中找到并点击「输入引脚」。")
        );

        addPlacement(
            wxT("INPUT"),
            wxT("放置输入 A"),
            wxT("请在右侧画布中点击一次，放置第一个输入引脚，并将它作为输入 A。")
        );

        addTool(
            wxT("INPUT"),
            wxT("再次选择输入引脚"),
            wxT("请再次点击左侧元件库中的「输入引脚」，准备放置输入 B。")
        );

        addPlacement(
            wxT("INPUT"),
            wxT("放置输入 B"),
            wxT("请在画布中放置第二个输入引脚，作为输入 B。")
        );

        addTool(
            wxT("XOR"),
            wxT("选择异或门"),
            wxT("请在左侧元件库中找到并点击异或门 XOR。")
        );

        addPlacement(
            wxT("XOR"),
            wxT("放置异或门"),
            wxT("请在画布中放置异或门，用于计算两个输入的和。")
        );

        addTool(
            wxT("AND"),
            wxT("选择与门"),
            wxT("请在左侧元件库中找到并点击与门 AND。")
        );

        addPlacement(
            wxT("AND"),
            wxT("放置与门"),
            wxT("请在画布中放置与门，用于计算进位。")
        );

        addTool(
            wxT("OUTPUT"),
            wxT("选择输出引脚"),
            wxT("请在左侧元件库中找到并点击输出引脚。")
        );

        addPlacement(
            wxT("OUTPUT"),
            wxT("放置求和输出"),
            wxT("请放置第一个输出引脚，作为 SUM。")
        );

        addTool(
            wxT("OUTPUT"),
            wxT("再次选择输出引脚"),
            wxT("请再次点击输出引脚。")
        );

        addPlacement(
            wxT("OUTPUT"),
            wxT("放置进位输出"),
            wxT("请放置第二个输出引脚，作为 CARRY。")
        );
    }


    else if (m_currentTutorialName == wxT("全加器 (Full Adder)"))
    {
        m_wireTarget = 12;

        const wxString inputNames[] = {
            wxT("输入 A"),
            wxT("输入 B"),
            wxT("进位输入 Cin")
        };

        for (int i = 0; i < 3; ++i)
        {
            addTool(
                wxT("INPUT"),
                wxT("选择输入引脚"),
                wxT("请在左侧元件库中点击输入引脚。")
            );

            addPlacement(
                wxT("INPUT"),
                wxT("放置 ") + inputNames[i],
                wxT("请在画布中放置 ") + inputNames[i] + wxT("。")
            );
        }

        const wxString types[] = {
            wxT("XOR"),
            wxT("XOR"),
            wxT("AND"),
            wxT("AND"),
            wxT("OR")
        };

        const wxString names[] = {
            wxT("第一个异或门"),
            wxT("第二个异或门"),
            wxT("第一个与门"),
            wxT("第二个与门"),
            wxT("或门")
        };

        for (int i = 0; i < 5; ++i)
        {
            addTool(
                types[i],
                wxT("选择") + names[i],
                wxT("请在左侧元件库中选择") + names[i] + wxT("。")
            );

            addPlacement(
                types[i],
                wxT("放置") + names[i],
                wxT("请在画布中放置") + names[i] + wxT("。")
            );
        }

        const wxString outputNames[] = {
            wxT("求和输出 SUM"),
            wxT("进位输出 COUT")
        };

        for (int i = 0; i < 2; ++i)
        {
            addTool(
                wxT("OUTPUT"),
                wxT("选择输出引脚"),
                wxT("请在左侧元件库中选择输出引脚。")
            );

            addPlacement(
                wxT("OUTPUT"),
                wxT("放置输出引脚"),
                wxT("请在画布中放置") + outputNames[i] + wxT("。")
            );
        }
    }
}

wxRect TutorialManager::GetCurrentTargetRect() const
{
    if (!m_mainFrame)
        return wxRect();

    if (m_stepIndex < 0 ||
        m_stepIndex >= static_cast<int>(m_steps.size()))
        return wxRect();

    const TutorialStep& step = m_steps[m_stepIndex];

    Toolbox* toolbox = m_mainFrame->GetToolbox();
    Canvas* canvas = m_mainFrame->GetCanvas();

    if (step.placement)
    {
        if (canvas)
            return canvas->GetScreenRect();

        return wxRect();
    }

    if (toolbox)
        return toolbox->GetToolRect(step.type);

    return wxRect();
}

void TutorialManager::ShowCurrentStep()
{
    if (!m_active || !m_overlay)
        return;

    Toolbox* toolbox = m_mainFrame->GetToolbox();
    Canvas* canvas = m_mainFrame->GetCanvas();

    // 所有元件放置完成，进入连线教学
    if (m_stepIndex >= static_cast<int>(m_steps.size()))
    {
        m_waitingForWires = true;

        BuildExpectedWires();

        if (m_expectedWires.empty())
        {
            wxMessageBox(
                wxT("无法建立教程连线清单，请检查元件是否全部放置成功。"),
                wxT("教程提示"),
                wxOK | wxICON_WARNING,
                m_mainFrame
            );

            CancelTutorial();
            return;
        }

        ShowCurrentWireStep();
        return;
    }

    const TutorialStep& step = m_steps[m_stepIndex];

    m_currentStepCompleted = false;

    wxRect targetRect;

    if (step.placement)
    {
        if (canvas)
            targetRect = canvas->GetScreenRect();
    }
    else
    {
        if (toolbox)
            targetRect = toolbox->GetToolRect(step.type);
    }

    wxString progress = wxString::Format(
        wxT("步骤：%d / %d"),
        m_stepIndex + 1,
        static_cast<int>(m_steps.size())
    );

    m_overlay->SetStep(
        m_currentTutorialName + wxT(" - ") + step.name,
        step.message,
        progress,
        targetRect
    );

    m_overlay->SetNextEnabled(false);
    m_overlay->UpdateOverlay();
}

void TutorialManager::ShowCurrentWireStep()
{
    if (!m_active || !m_waitingForWires || !m_overlay)
        return;

    if (m_wireStepIndex >= static_cast<int>(m_expectedWires.size()))
        return;

    Toolbox* toolbox = m_mainFrame->GetToolbox();
    Canvas* canvas = m_mainFrame->GetCanvas();

    const ExpectedWire& expected =
        m_expectedWires[m_wireStepIndex];

    wxString message;

    message += wxT("请完成下面这条连线：\n\n");

    message += expected.sourceName;
    message += wxT(" 的 ");
    message += expected.sourcePinName;
    message += wxT("\n\n");

    message += wxT("连接到\n\n");

    message += expected.targetName;
    message += wxT(" 的 ");
    message += expected.targetPinName;
    message += wxT("\n\n");

    message += wxT("操作方法：\n");
    message += wxT("1. 点击左侧导线工具。\n");
    message += wxT("2. 点击起点元件的输出引脚。\n");
    message += wxT("3. 点击终点元件的输入引脚。");

    wxRect wireRect;
    wxRect canvasRect;

    if (toolbox)
        wireRect = toolbox->GetToolRect(wxT("WIRE"));

    if (canvas)
        canvasRect = canvas->GetScreenRect();

    m_overlay->SetStep(
        m_currentTutorialName + wxT(" - 连线教学"),
        message,
        wxString::Format(
            wxT("正确连线进度：%d / %d"),
            m_wireStepIndex,
            m_wireTarget
        ),
        wireRect,
        canvasRect
    );

    m_overlay->SetNextEnabled(false);
    m_overlay->UpdateOverlay();
}

void TutorialManager::NotifyToolSelected(const wxString& componentType)
{
    if (!m_active || m_autoDemo || m_waitingForWires)
        return;

    if (m_stepIndex >= static_cast<int>(m_steps.size()))
        return;

    const TutorialStep& step = m_steps[m_stepIndex];

    if (step.placement)
        return;

    if (componentType == step.type)
    {
        CompleteCurrentStep();
    }
}

void TutorialManager::NotifyComponentPlaced(const wxString& componentType, int componentId)
{
    if (!m_active || m_autoDemo || m_waitingForWires)
        return;

    if (m_stepIndex >= static_cast<int>(m_steps.size()))
        return;

    const TutorialStep& step = m_steps[m_stepIndex];

    if (!step.placement)
        return;

    if (componentType == step.type)
    {
        if (!step.label.IsEmpty() && m_mainFrame && m_mainFrame->GetCanvas())
            m_mainFrame->GetCanvas()->SetTutorialLabel(componentId, step.label);
        CompleteCurrentStep();
    }
}

void TutorialManager::CompleteCurrentStep()
{
    if (!m_active || m_currentStepCompleted)
        return;

    m_currentStepCompleted = true;

    const std::weak_ptr<int> lifetime = m_lifetime;
    const auto generation = m_generation;
    const int stepIndex = m_stepIndex;
    wxTheApp->CallAfter([this, lifetime, generation, stepIndex]()
        {
            const auto alive = lifetime.lock();
            if (!alive)
                return;
            if (generation != m_generation || stepIndex != m_stepIndex ||
                !m_active || !m_currentStepCompleted)
                return;

            ++m_stepIndex;

            if (m_stepIndex >= static_cast<int>(m_steps.size()))
            {
                m_waitingForWires = true;
            }

            ShowCurrentStep();
        });
}

void TutorialManager::PreviousStep()
{
    if (!m_active || m_autoDemo || m_waitingForWires)
        return;

    if (m_stepIndex > 0)
    {
        --m_stepIndex;
        ShowCurrentStep();
    }
}

void TutorialManager::NextStep()
{
    if (!m_active || !m_autoDemo) return;
    m_autoPaused = !m_autoPaused;
    if (m_autoPaused) m_autoTimer.Stop();
    else m_autoTimer.StartOnce(AutoStepDelayMs);
    if (auto* overlay = m_overlay.get()) overlay->SetAutoDemoMode(true, m_autoPaused);
}

void TutorialManager::NotifyWireCreated(const CircuitWireData& wire)
{
    if (!m_active || m_autoDemo || !m_waitingForWires || !m_overlay)
        return;

    if (m_wireStepIndex >= static_cast<int>(m_expectedWires.size()))
        return;

    const ExpectedWire& expected =
        m_expectedWires[m_wireStepIndex];

    const CircuitWireData& correctWire = expected.wire;

    const bool correct =
        wire.sourceId == correctWire.sourceId &&
        wire.sourcePin == correctWire.sourcePin &&
        wire.targetId == correctWire.targetId &&
        wire.targetPin == correctWire.targetPin;

    if (!correct)
    {
        wxString message;

        message += wxT("这条连线不符合当前步骤要求。\n\n");

        message += wxT("当前需要连接：\n");
        message += expected.sourceName;
        message += wxT(" 的 ");
        message += expected.sourcePinName;
        message += wxT(" → ");
        message += expected.targetName;
        message += wxT(" 的 ");
        message += expected.targetPinName;

        message += wxT("\n\n请检查连线起点和终点，然后重新尝试。");

        Toolbox* toolbox = m_mainFrame->GetToolbox();
        Canvas* canvas = m_mainFrame->GetCanvas();

        wxRect wireRect;
        wxRect canvasRect;

        if (toolbox)
            wireRect = toolbox->GetToolRect(wxT("WIRE"));

        if (canvas)
            canvasRect = canvas->GetScreenRect();

        m_overlay->SetStep(
            m_currentTutorialName + wxT(" - 连线错误"),
            message,
            wxString::Format(
                wxT("正确连线进度：%d / %d"),
                m_wireStepIndex,
                m_wireTarget
            ),
            wireRect,
            canvasRect
        );

        m_overlay->SetNextEnabled(false);
        m_overlay->UpdateOverlay();

        // 错误连接不推进教程
        return;
    }


    ++m_wireStepIndex;
    m_wireCount = m_wireStepIndex;

    if (m_wireStepIndex >= static_cast<int>(m_expectedWires.size()))
    {
        FinishTutorial();
        return;
    }

    ShowCurrentWireStep();
}


void TutorialManager::FinishTutorial()
{
    const bool wasAutoDemo = m_autoDemo;
    m_autoTimer.Stop();
    m_autoDemo = m_autoPaused = false;
    ++m_generation;
    m_active = false;
    m_waitingForWires = false;

    if (m_overlay)
    {
        m_overlay->SetAutoDemoMode(false);
        m_overlay->HideOverlay();
    }

    wxMessageBox(
        (wasAutoDemo ? wxT("自动演示完成：") : wxT("恭喜！你已完成 ")) +
        m_currentTutorialName +
        wxT(" 教程。"),
        wxT("教程完成"),
        wxOK | wxICON_INFORMATION,
        m_mainFrame
    );
}

// =====================================================
// 退出教程
// =====================================================
void TutorialManager::CancelTutorial()
{
    m_autoTimer.Stop();
    m_autoDemo = m_autoPaused = false;
    m_autoPlacements.clear();
    m_autoLabels.clear();
    m_autoDocument = CircuitDocument{};
    m_autoPlacementIndex = m_autoWireIndex = 0;
    ++m_generation;
    m_active = false;
    m_waitingForWires = false;
    m_currentStepCompleted = false;

    m_stepIndex = 0;
    m_wireCount = 0;
    m_wireStepIndex = 0;

    m_expectedWires.clear();

    if (m_overlay)
    {
        m_overlay->SetAutoDemoMode(false);
        m_overlay->HideOverlay();
    }
}

void TutorialManager::BuildExpectedWires()
{
    m_expectedWires.clear();
    m_wireStepIndex = 0;

    if (!m_mainFrame || !m_mainFrame->GetCanvas())
        return;

    const CircuitDocument document =
        m_mainFrame->GetCanvas()->ExportDocument();

    std::vector<int> inputs;
    std::vector<int> xors;
    std::vector<int> ands;
    std::vector<int> ors;
    std::vector<int> outputs;

    // 根据元件类型和元件 ID 分类
    for (const auto& component : document.components)
    {
        if (component.type == "INPUT")
            inputs.push_back(component.id);

        else if (component.type == "XOR")
            xors.push_back(component.id);

        else if (component.type == "AND")
            ands.push_back(component.id);

        else if (component.type == "OR")
            ors.push_back(component.id);

        else if (component.type == "OUTPUT")
            outputs.push_back(component.id);
    }

    // 添加一条预期连线
    auto add = [this](
        int source,
        int sourcePin,
        int target,
        int targetPin,
        const wxString& sourceName,
        const wxString& targetName,
        const wxString& sourcePinName,
        const wxString& targetPinName)
        {
            ExpectedWire expected;

            expected.wire.sourceId = source;
            expected.wire.sourcePin = sourcePin;

            expected.wire.targetId = target;
            expected.wire.targetPin = targetPin;

            expected.sourceName = sourceName;
            expected.targetName = targetName;

            expected.sourcePinName = sourcePinName;
            expected.targetPinName = targetPinName;

            m_expectedWires.push_back(expected);
        };

    if (m_currentTutorialName == wxT("半加器 (Half Adder)"))
    {
        if (inputs.size() < 2 ||
            xors.empty() ||
            ands.empty() ||
            outputs.size() < 2)
        {
            return;
        }

        // 1. 输入 A -> XOR 门 A
        add(
            inputs[0], 0,
            xors[0], 0,
            wxT("输入 A"),
            wxT("异或门 XOR"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 2. 输入 B -> XOR 门 B
        add(
            inputs[1], 0,
            xors[0], 1,
            wxT("输入 B"),
            wxT("异或门 XOR"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 3. 输入 A -> AND 门 A
        add(
            inputs[0], 0,
            ands[0], 0,
            wxT("输入 A"),
            wxT("与门 AND"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 4. 输入 B -> AND 门 B
        add(
            inputs[1], 0,
            ands[0], 1,
            wxT("输入 B"),
            wxT("与门 AND"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 5. XOR -> SUM
        add(
            xors[0], 0,
            outputs[0], 0,
            wxT("异或门 XOR"),
            wxT("求和输出 SUM"),
            wxT("输出引脚"),
            wxT("输入引脚")
        );

        // 6. AND -> CARRY
        add(
            ands[0], 0,
            outputs[1], 0,
            wxT("与门 AND"),
            wxT("进位输出 CARRY"),
            wxT("输出引脚"),
            wxT("输入引脚")
        );
    }

    else if (m_currentTutorialName == wxT("全加器 (Full Adder)"))
    {
        if (inputs.size() < 3 ||
            xors.size() < 2 ||
            ands.size() < 2 ||
            ors.empty() ||
            outputs.size() < 2)
        {
            return;
        }

        // 1. A -> XOR1
        add(
            inputs[0], 0,
            xors[0], 0,
            wxT("输入 A"),
            wxT("第一个异或门 XOR1"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 2. B -> XOR1
        add(
            inputs[1], 0,
            xors[0], 1,
            wxT("输入 B"),
            wxT("第一个异或门 XOR1"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 3. XOR1 -> XOR2
        add(
            xors[0], 0,
            xors[1], 0,
            wxT("第一个异或门 XOR1"),
            wxT("第二个异或门 XOR2"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 4. Cin -> XOR2
        add(
            inputs[2], 0,
            xors[1], 1,
            wxT("进位输入 Cin"),
            wxT("第二个异或门 XOR2"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 5. A -> AND1
        add(
            inputs[0], 0,
            ands[0], 0,
            wxT("输入 A"),
            wxT("第一个与门 AND1"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 6. B -> AND1
        add(
            inputs[1], 0,
            ands[0], 1,
            wxT("输入 B"),
            wxT("第一个与门 AND1"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 7. XOR1 -> AND2
        add(
            xors[0], 0,
            ands[1], 0,
            wxT("第一个异或门 XOR1"),
            wxT("第二个与门 AND2"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 8. Cin -> AND2
        add(
            inputs[2], 0,
            ands[1], 1,
            wxT("进位输入 Cin"),
            wxT("第二个与门 AND2"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 9. AND1 -> OR
        add(
            ands[0], 0,
            ors[0], 0,
            wxT("第一个与门 AND1"),
            wxT("或门 OR"),
            wxT("输出引脚"),
            wxT("A 输入引脚")
        );

        // 10. AND2 -> OR
        add(
            ands[1], 0,
            ors[0], 1,
            wxT("第二个与门 AND2"),
            wxT("或门 OR"),
            wxT("输出引脚"),
            wxT("B 输入引脚")
        );

        // 11. XOR2 -> SUM
        add(
            xors[1], 0,
            outputs[0], 0,
            wxT("第二个异或门 XOR2"),
            wxT("求和输出 SUM"),
            wxT("输出引脚"),
            wxT("输入引脚")
        );

        // 12. OR -> COUT
        add(
            ors[0], 0,
            outputs[1], 0,
            wxT("或门 OR"),
            wxT("进位输出 COUT"),
            wxT("输出引脚"),
            wxT("输入引脚")
        );
    }

    m_wireTarget = static_cast<int>(m_expectedWires.size());
}
