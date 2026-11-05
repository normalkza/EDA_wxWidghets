#pragma once

#include "SimulationSnapshot.h"
#include "TraceRecorder.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Deterministic, integer-nanosecond digital simulation. This owns its model and
// never retains a pointer to a Canvas or a wxWidgets control.
class DigitalSimulator
{
public:
    bool Compile(const CircuitDocument& document, std::uint64_t revision, wxString& error);
    bool Reset(wxString& error);
    bool Step(wxString& error);
    bool AdvanceTo(std::int64_t timeNs, wxString& error);
    bool StepClockPeriod(int clockId, wxString& error);
    bool SetInput(int componentId, LogicValue value, wxString& error);
    // Queue all same-time stimuli before advancing to obtain batch semantics.
    bool QueueInput(int componentId, LogicValue value, std::int64_t timeNs, wxString& error);

    bool IsCompiled() const { return m_compiled; }
    bool IsFinished() const
    {
        return m_compiled && (m_halted || (m_timeNs >= m_config.stopTimeNs && !GetNextEventTime()));
    }
    bool HasFailed() const { return m_halted; }
    std::int64_t GetTime() const { return m_timeNs; }
    std::int64_t GetRecordedUntil() const { return m_recordedUntil; }
    std::uint64_t GetRevision() const { return m_revision; }
    std::size_t GetEventCount() const { return m_eventCount; }
    const SimulationConfig& GetConfig() const { return m_config; }
    const wxString& GetLastError() const { return m_lastError; }
    std::optional<std::int64_t> GetNextEventTime() const;
    std::vector<int> GetClockIds() const;
    std::optional<std::int64_t> GetClockPeriod(int clockId) const;
    std::optional<LogicValue> GetValue(int componentId, PinType type, std::size_t index) const;
    SimulationResult GetResult() const;
    const std::vector<DigitalTrace>& GetTraces() const { return m_recorder.GetTraces(); }
    bool ExportCsv(std::ostream& output, wxString& error) const;
    bool ExportVcd(std::ostream& output, wxString& error) const;

private:
    struct Driver { std::size_t node = 0; std::size_t pin = 0; };
    struct Node
    {
        CircuitComponentData design;
        std::unique_ptr<Component> component;
        std::vector<std::optional<Driver>> inputs;
        LogicValue previousClock = LogicValue::Low;
        std::optional<std::int64_t> nextClock;
    };
    struct InputEvent { std::size_t node; LogicValue value; };

    bool Initialize(wxString& error);
    void Settle();
    bool ProcessTime(std::int64_t timeNs, wxString& error);
    bool ProcessTimeUnchecked(std::int64_t timeNs, wxString& error);
    bool Record(std::int64_t timeNs, std::size_t delta, wxString& error);
    bool Fail(const wxString& message, wxString& error);
    bool CheckReady(wxString& error) const;
    void RefreshInputs(Node& node);

    std::vector<Node> m_nodes;
    std::unordered_map<int, std::size_t> m_byId;
    std::vector<std::size_t> m_order;
    std::vector<Driver> m_tracePins;
    std::vector<PinType> m_traceDirections;
    std::map<std::int64_t, std::vector<InputEvent>> m_stimuli;
    TraceRecorder m_recorder;
    SimulationConfig m_config;
    std::uint64_t m_revision = 0;
    std::int64_t m_timeNs = 0;
    std::int64_t m_recordedUntil = 0;
    std::size_t m_eventCount = 0;
    std::size_t m_nextDelta = 1;
    bool m_compiled = false;
    bool m_halted = false;
    wxString m_lastError;
};
