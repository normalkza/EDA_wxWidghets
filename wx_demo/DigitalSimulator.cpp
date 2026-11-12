#include "DigitalSimulator.h"

#include "CircuitValidation.h"
#include "ComponentRegistry.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <queue>
#include <utility>

namespace
{
bool IsInteractiveSource(const std::string& type)
{
    return type == "INPUT" || type == "BUTTON";
}

bool IsClockSource(const std::string& type)
{
    return type == "CLOCK" || IsInteractiveSource(type);
}
}

bool DigitalSimulator::Compile(const CircuitDocument& document, std::uint64_t revision, wxString& error)
{
    error.clear();
    if (!CircuitValidation::Validate(document, error)) return false;
    DigitalSimulator candidate;
    candidate.m_config = document.simulation;
    candidate.m_revision = revision;
    if (candidate.m_config.stopTimeNs < 0 || candidate.m_config.maxEvents == 0 ||
        candidate.m_config.maxDeltaCycles == 0 || candidate.m_config.maxTraceSamples == 0)
    {
        error = wxS("仿真停止时间或资源上限无效。");
        return false;
    }
    try
    {
        std::vector<const CircuitComponentData*> ordered;
        for (const auto& data : document.components) ordered.push_back(&data);
        std::sort(ordered.begin(), ordered.end(), [](const auto* first, const auto* second) {
            return first->id < second->id;
        });
        candidate.m_nodes.reserve(ordered.size());
        for (const auto* data : ordered)
        {
            auto component = ComponentRegistry::Create(data->type, data->inputCount, data->high);
            if (!component)
            {
                error = wxString::Format(wxS("无法编译元件 %d。"), data->id);
                return false;
            }
            if (data->type == "CLOCK" && (data->clockPeriodNs < 2 || data->clockHighNs < 1 ||
                data->clockHighNs >= data->clockPeriodNs || data->clockStartNs < 0))
            {
                error = wxString::Format(wxS("时钟 %d 的高低持续时间必须至少为 1 ns，启动时间不能为负。"), data->id);
                return false;
            }
            Node node;
            node.design = *data;
            node.inputs.resize(component->inputs.size());
            node.component = std::move(component);
            candidate.m_byId.emplace(data->id, candidate.m_nodes.size());
            candidate.m_nodes.push_back(std::move(node));
        }

        std::vector<std::vector<std::size_t>> outgoing(ordered.size());
        std::vector<std::size_t> indegree(ordered.size(), 0);
        for (const auto& wire : document.wires)
        {
            const auto source = candidate.m_byId.at(wire.sourceId);
            const auto target = candidate.m_byId.at(wire.targetId);
            auto& targetNode = candidate.m_nodes[target];
            targetNode.inputs[wire.targetPin] = Driver{source, static_cast<std::size_t>(wire.sourcePin)};
            if (targetNode.design.type == "DFF" && wire.targetPin == 1 &&
                !IsClockSource(candidate.m_nodes[source].design.type))
            {
                error = wxString::Format(wxS("DFF %d 的 CLK 只能由 CLOCK、INPUT 或 BUTTON 直接驱动；当前源为元件 %d。"),
                    wire.targetId, wire.sourceId);
                return false;
            }
            // D and CLK are sampled; only the asynchronous RESET -> Q path
            // participates in the immediate dependency graph.
            if (targetNode.design.type == "DFF" && wire.targetPin != 2) continue;
            outgoing[source].push_back(target);
            ++indegree[target];
        }
        std::queue<std::size_t> ready;
        for (std::size_t index = 0; index < indegree.size(); ++index)
            if (indegree[index] == 0) ready.push(index);
        while (!ready.empty())
        {
            const auto current = ready.front();
            ready.pop();
            candidate.m_order.push_back(current);
            for (const auto target : outgoing[current])
                if (--indegree[target] == 0) ready.push(target);
        }
        if (candidate.m_order.size() != ordered.size())
        {
            error = wxS("电路包含组合或异步复位反馈环路。");
            return false;
        }
        candidate.m_compiled = true;
        if (!candidate.Initialize(error)) return false;
    }
    catch (const std::exception& exception)
    {
        error = wxS("无法编译数字仿真：") + wxString::FromUTF8(exception.what());
        return false;
    }
    *this = std::move(candidate);
    return true;
}

bool DigitalSimulator::Initialize(wxString& error)
{
    m_stimuli.clear();
    m_timeNs = m_recordedUntil = 0;
    m_eventCount = 0;
    m_nextDelta = 1;
    m_halted = false;
    m_lastError.clear();
    for (auto& node : m_nodes)
    {
        for (auto& pin : node.component->inputs) pin.value = LogicValue::Low;
        for (auto& pin : node.component->outputs) pin.value = LogicValue::Low;
        if (ComponentRegistry::SupportsInitialLevel(node.design.type))
            node.component->outputs[0].value = node.design.high ? LogicValue::High : LogicValue::Low;
        node.nextClock = node.design.type == "CLOCK" ?
            std::optional<std::int64_t>(node.design.clockStartNs) : std::nullopt;
    }
    Settle();
    for (auto& node : m_nodes)
        if (node.design.type == "DFF") node.previousClock = node.component->inputs[1].value;

    std::vector<DigitalTrace> traces;
    m_tracePins.clear();
    m_traceDirections.clear();
    for (std::size_t index = 0; index < m_nodes.size(); ++index)
    {
        const auto& node = m_nodes[index];
        const auto& component = *node.component;
        for (std::size_t pin = 0; pin < component.outputs.size(); ++pin)
        {
            traces.push_back({node.design.id, PinType::Output, pin,
                node.design.type + " " + std::to_string(node.design.id) + "." + component.outputs[pin].name});
            m_tracePins.push_back({index, pin});
            m_traceDirections.push_back(PinType::Output);
        }
        if (node.design.type == "PROBE" || node.design.type == "OUTPUT" || node.design.type == "LED")
        {
            traces.push_back({node.design.id, PinType::Input, 0,
                node.design.probeLabel.empty() ? node.design.type + " " + std::to_string(node.design.id) : node.design.probeLabel});
            m_tracePins.push_back({index, 0});
            m_traceDirections.push_back(PinType::Input);
        }
    }
    m_recorder.Initialize(std::move(traces), m_config.maxTraceSamples);
    return Record(0, 0, error);
}

bool DigitalSimulator::Reset(wxString& error)
{
    error.clear();
    if (!m_compiled)
    {
        error = wxS("请先编译仿真电路。");
        return false;
    }
    return Initialize(error);
}

void DigitalSimulator::RefreshInputs(Node& node)
{
    for (std::size_t input = 0; input < node.inputs.size(); ++input)
    {
        const auto& driver = node.inputs[input];
        node.component->inputs[input].value = driver ?
            m_nodes[driver->node].component->outputs[driver->pin].value : LogicValue::Low;
    }
}

void DigitalSimulator::Settle()
{
    // The topological order includes async RESET dependencies, so one sweep
    // reaches the unique combinational fixed point without traversal races.
    for (const auto index : m_order)
    {
        auto& node = m_nodes[index];
        RefreshInputs(node);
        node.component->Evaluate();
    }
    // D and CLK may come from a later node because they are not immediate
    // dependencies. Refresh them after all output values have settled.
    for (auto& node : m_nodes) RefreshInputs(node);
}

bool DigitalSimulator::Record(std::int64_t timeNs, std::size_t delta, wxString& error)
{
    std::vector<LogicValue> values;
    values.reserve(m_tracePins.size());
    for (std::size_t index = 0; index < m_tracePins.size(); ++index)
    {
        const auto& pin = m_tracePins[index];
        const auto& component = *m_nodes[pin.node].component;
        values.push_back(m_traceDirections[index] == PinType::Input ?
            component.inputs[pin.pin].value : component.outputs[pin.pin].value);
    }
    std::string recordingError;
    if (!m_recorder.Record(timeNs, delta, values, recordingError))
        return Fail(wxString(wxS("波形缓存已达上限；停止运行，已有记录未被丢弃。 ")) +
            wxString::FromUTF8(recordingError), error);
    m_recordedUntil = timeNs;
    return true;
}

bool DigitalSimulator::Fail(const wxString& message, wxString& error)
{
    m_halted = true;
    m_lastError = error = message;
    return false;
}

bool DigitalSimulator::CheckReady(wxString& error) const
{
    error.clear();
    if (!m_compiled) error = wxS("请先编译仿真电路。");
    else if (m_halted) error = m_lastError;
    return error.empty();
}

std::optional<std::int64_t> DigitalSimulator::GetNextEventTime() const
{
    if (!m_compiled || m_halted) return std::nullopt;
    std::optional<std::int64_t> next;
    if (!m_stimuli.empty()) next = m_stimuli.begin()->first;
    for (const auto& node : m_nodes)
        if (node.nextClock && (!next || *node.nextClock < *next)) next = node.nextClock;
    if (next && *next > m_config.stopTimeNs) return std::nullopt;
    return next;
}

bool DigitalSimulator::ProcessTime(std::int64_t timeNs, wxString& error)
{
    // Roll back a failed time slot to its preceding complete, stable state.
    // Checkpoints retain sample lengths, not copies of the waveform history.
    struct NodeState
    {
        std::vector<LogicValue> outputs;
        LogicValue previousClock;
        std::optional<std::int64_t> nextClock;
    };
    std::vector<NodeState> states;
    states.reserve(m_nodes.size());
    for (const auto& node : m_nodes)
    {
        NodeState state;
        for (const auto& pin : node.component->outputs) state.outputs.push_back(pin.value);
        state.previousClock = node.previousClock;
        state.nextClock = node.nextClock;
        states.push_back(std::move(state));
    }
    const auto checkpoint = m_recorder.MakeCheckpoint();
    const auto oldTime = m_timeNs;
    const auto oldRecordedUntil = m_recordedUntil;
    const auto oldEventCount = m_eventCount;
    const auto oldNextDelta = m_nextDelta;
    const auto stimulus = m_stimuli.find(timeNs);
    const auto stimuli = stimulus == m_stimuli.end() ?
        std::optional<std::vector<InputEvent>>() : std::optional<std::vector<InputEvent>>(stimulus->second);
    if (ProcessTimeUnchecked(timeNs, error)) return true;
    for (std::size_t index = 0; index < m_nodes.size(); ++index)
    {
        auto& node = m_nodes[index];
        for (std::size_t pin = 0; pin < states[index].outputs.size(); ++pin)
            node.component->outputs[pin].value = states[index].outputs[pin];
        node.previousClock = states[index].previousClock;
        node.nextClock = states[index].nextClock;
    }
    if (stimuli) m_stimuli[timeNs] = *stimuli;
    else m_stimuli.erase(timeNs);
    m_recorder.Restore(checkpoint);
    m_timeNs = oldTime;
    m_recordedUntil = oldRecordedUntil;
    m_eventCount = oldEventCount;
    m_nextDelta = oldNextDelta;
    Settle();
    return false;
}

bool DigitalSimulator::ProcessTimeUnchecked(std::int64_t timeNs, wxString& error)
{
    const auto stimulus = m_stimuli.find(timeNs);
    std::size_t eventCount = stimulus == m_stimuli.end() ? 0 : stimulus->second.size();
    for (const auto& node : m_nodes)
        if (node.nextClock && *node.nextClock == timeNs) ++eventCount;
    if (eventCount > m_config.maxEvents - m_eventCount)
        return Fail(wxS("仿真事件数量已达上限；请提高上限或缩短停止时间后复位重跑。"), error);
    if (timeNs != m_timeNs) m_nextDelta = 1;
    std::size_t delta = m_nextDelta;
    if (delta > m_config.maxDeltaCycles)
        return Fail(wxS("同一时刻的 delta 轮次数量已达上限。"), error);
    m_timeNs = timeNs;
    m_eventCount += eventCount;
    // All external stimuli at this integer time are applied together.
    if (stimulus != m_stimuli.end())
    {
        for (const auto& event : stimulus->second)
            m_nodes[event.node].component->outputs[0].value = event.value;
        m_stimuli.erase(stimulus);
    }
    for (auto& node : m_nodes)
    {
        if (!node.nextClock || *node.nextClock != timeNs) continue;
        auto& value = node.component->outputs[0].value;
        value = value == LogicValue::High ? LogicValue::Low : LogicValue::High;
        const auto duration = value == LogicValue::High ? node.design.clockHighNs :
            node.design.clockPeriodNs - node.design.clockHighNs;
        // A transition beyond the configured horizon need not be scheduled.
        node.nextClock = duration <= m_config.stopTimeNs - timeNs ?
            std::optional<std::int64_t>(timeNs + duration) : std::nullopt;
    }
    Settle();
    if (!Record(timeNs, delta, error)) return false;

    std::vector<std::pair<std::size_t, LogicValue>> commits;
    for (std::size_t index = 0; index < m_nodes.size(); ++index)
    {
        auto& node = m_nodes[index];
        if (node.design.type != "DFF") continue;
        const auto& inputs = node.component->inputs;
        const bool rising = node.previousClock == LogicValue::Low && inputs[1].value == LogicValue::High;
        const bool falling = node.previousClock == LogicValue::High && inputs[1].value == LogicValue::Low;
        const bool activeEdge = node.design.fallingEdge ? falling : rising;
        const auto next = inputs[2].value == LogicValue::High ? LogicValue::Low :
            activeEdge ? inputs[0].value : node.component->outputs[0].value;
        node.previousClock = inputs[1].value;
        if (next != node.component->outputs[0].value) commits.emplace_back(index, next);
    }
    if (!commits.empty())
    {
        if (++delta > m_config.maxDeltaCycles)
            return Fail(wxS("同一时刻的 delta 轮次数量已达上限。"), error);
        // Every DFF reads the same pre-commit snapshot. A chain advances one
        // stage per edge, regardless of its component IDs or placement order.
        for (const auto& [index, next] : commits) m_nodes[index].component->outputs[0].value = next;
        if (!Record(timeNs, delta, error)) return false;
        if (++delta > m_config.maxDeltaCycles)
            return Fail(wxS("同一时刻的 delta 轮次数量已达上限。"), error);
        Settle();
        if (!Record(timeNs, delta, error)) return false;
    }
    m_nextDelta = delta + 1;
    return true;
}

bool DigitalSimulator::Step(wxString& error)
{
    if (!CheckReady(error)) return false;
    if (const auto next = GetNextEventTime()) return ProcessTime(*next, error);
    m_timeNs = m_recordedUntil = m_config.stopTimeNs;
    return true;
}

bool DigitalSimulator::AdvanceTo(std::int64_t timeNs, wxString& error)
{
    if (!CheckReady(error)) return false;
    if (timeNs < m_timeNs || timeNs > m_config.stopTimeNs)
    {
        error = wxS("目标时间必须在当前时刻与仿真停止时间之间。");
        return false;
    }
    while (true)
    {
        const auto next = GetNextEventTime();
        if (!next || *next > timeNs) break;
        if (!ProcessTime(*next, error)) return false;
    }
    if (timeNs != m_timeNs) m_nextDelta = 1;
    m_timeNs = m_recordedUntil = timeNs;
    return true;
}

bool DigitalSimulator::StepClockPeriod(int clockId, wxString& error)
{
    if (!CheckReady(error)) return false;
    const auto period = GetClockPeriod(clockId);
    if (!period)
    {
        error = wxS("请选择一个 CLOCK 作为周期步进参考。");
        return false;
    }
    const auto remaining = m_config.stopTimeNs - m_timeNs;
    return AdvanceTo(m_timeNs + std::min(*period, remaining), error);
}

bool DigitalSimulator::QueueInput(int componentId, LogicValue value, std::int64_t timeNs, wxString& error)
{
    if (!CheckReady(error)) return false;
    const auto node = m_byId.find(componentId);
    if (node == m_byId.end() || !IsInteractiveSource(m_nodes[node->second].design.type) ||
        (value != LogicValue::Low && value != LogicValue::High))
    {
        error = wxS("运行激励只能修改 INPUT 或 BUTTON 的二态电平。");
        return false;
    }
    if (timeNs < m_timeNs || timeNs > m_config.stopTimeNs)
    {
        error = wxS("激励时间必须在当前时刻与仿真停止时间之间。");
        return false;
    }
    std::size_t queued = 0;
    for (const auto& [time, events] : m_stimuli) queued += events.size();
    if (queued >= m_config.maxEvents - m_eventCount)
    {
        error = wxS("待执行激励超过事件数量上限。");
        return false;
    }
    m_stimuli[timeNs].push_back({node->second, value});
    return true;
}

bool DigitalSimulator::SetInput(int componentId, LogicValue value, wxString& error)
{
    if (!QueueInput(componentId, value, m_timeNs, error)) return false;
    return ProcessTime(m_timeNs, error);
}

std::vector<int> DigitalSimulator::GetClockIds() const
{
    std::vector<int> ids;
    if (m_compiled)
        for (const auto& node : m_nodes)
            if (node.design.type == "CLOCK") ids.push_back(node.design.id);
    return ids;
}

std::optional<std::int64_t> DigitalSimulator::GetClockPeriod(int clockId) const
{
    const auto found = m_byId.find(clockId);
    if (!m_compiled || found == m_byId.end() || m_nodes[found->second].design.type != "CLOCK") return std::nullopt;
    return m_nodes[found->second].design.clockPeriodNs;
}

std::optional<LogicValue> DigitalSimulator::GetValue(int componentId, PinType type, std::size_t index) const
{
    const auto found = m_byId.find(componentId);
    if (!m_compiled || found == m_byId.end() || (type != PinType::Input && type != PinType::Output)) return std::nullopt;
    const auto& component = *m_nodes[found->second].component;
    const auto& pins = type == PinType::Input ? component.inputs : component.outputs;
    return index < pins.size() ? std::optional<LogicValue>(pins[index].value) : std::nullopt;
}

SimulationResult DigitalSimulator::GetResult() const
{
    SimulationResult result;
    result.revision = m_revision;
    if (!m_compiled) return result;
    for (const auto& node : m_nodes)
    {
        for (std::size_t pin = 0; pin < node.component->inputs.size(); ++pin)
            result.values.push_back({node.design.id, PinType::Input, pin, node.component->inputs[pin].value});
        for (std::size_t pin = 0; pin < node.component->outputs.size(); ++pin)
            result.values.push_back({node.design.id, PinType::Output, pin, node.component->outputs[pin].value});
    }
    return result;
}

bool DigitalSimulator::ExportCsv(std::ostream& output, wxString& error) const
{
    error.clear();
    if (!m_compiled) { error = wxS("尚无可导出的仿真记录。"); return false; }
    std::string exportError;
    if (TraceRecorder::WriteCsv(output, GetTraces(), m_recordedUntil, exportError)) return true;
    error = wxString::FromUTF8(exportError);
    return false;
}

bool DigitalSimulator::ExportVcd(std::ostream& output, wxString& error) const
{
    error.clear();
    if (!m_compiled) { error = wxS("尚无可导出的仿真记录。"); return false; }
    std::string exportError;
    if (TraceRecorder::WriteVcd(output, GetTraces(), m_recordedUntil, exportError)) return true;
    error = wxString::FromUTF8(exportError);
    return false;
}
