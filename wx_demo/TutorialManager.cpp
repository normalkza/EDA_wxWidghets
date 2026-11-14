#include "TutorialManager.h"

#include "MainFrame.h"
#include "Toolbox.h"
#include "Canvas.h"
#include "SelectionState.h"

TutorialManager::TutorialManager(MainFrame* mainFrame)
    : m_mainFrame(mainFrame)
{
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

void TutorialManager::StartTutorial(const wxString& tutorialName)
{
    CancelTutorial();

    m_currentTutorialName = tutorialName;

    BuildSteps();

    if (m_steps.empty())
        return;

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
    if (!m_active || m_waitingForWires)
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
    if (!m_active || m_waitingForWires)
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

    wxTheApp->CallAfter([this]()
        {
            if (!m_active || !m_currentStepCompleted)
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
    if (!m_active || m_waitingForWires)
        return;

    if (m_stepIndex > 0)
    {
        --m_stepIndex;
        ShowCurrentStep();
    }
}

void TutorialManager::NextStep()
{
    // 教程步骤自动推进，不需要手动点击下一步。
}

void TutorialManager::NotifyWireCreated(const CircuitWireData& wire)
{
    if (!m_active || !m_waitingForWires)
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
    m_active = false;
    m_waitingForWires = false;

    if (m_overlay)
        m_overlay->HideOverlay();

    wxMessageBox(
        wxT("恭喜！你已完成 ") +
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
    m_active = false;
    m_waitingForWires = false;
    m_currentStepCompleted = false;

    m_stepIndex = 0;
    m_wireCount = 0;
    m_wireStepIndex = 0;

    m_expectedWires.clear();

    if (m_overlay)
        m_overlay->HideOverlay();
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
