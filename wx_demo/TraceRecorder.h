#pragma once

#include "Pin.h"
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

struct TraceSample
{
    std::int64_t timeNs = 0;
    std::size_t delta = 0;
    LogicValue value = LogicValue::Low;
    bool operator==(const TraceSample&) const = default;
};

struct DigitalTrace
{
    int componentId = 0;
    PinType type = PinType::Output;
    std::size_t index = 0;
    std::string name;
    std::vector<TraceSample> samples;
};

// Changes are retained in full. Capacity exhaustion stops the simulation;
// old samples are never silently evicted from an exported trace.
class TraceRecorder
{
public:
    struct Checkpoint
    {
        std::vector<std::size_t> lengths;
        std::size_t sampleCount = 0;
    };
    void Initialize(std::vector<DigitalTrace> traces, std::size_t capacity);
    bool Record(std::int64_t timeNs, std::size_t delta,
        const std::vector<LogicValue>& values, std::string& error);
    const std::vector<DigitalTrace>& GetTraces() const { return m_traces; }
    std::size_t GetSampleCount() const { return m_sampleCount; }
    Checkpoint MakeCheckpoint() const;
    void Restore(const Checkpoint& checkpoint);

    static bool WriteCsv(std::ostream& output, const std::vector<DigitalTrace>& traces,
        std::int64_t endTimeNs, std::string& error);
    static bool WriteVcd(std::ostream& output, const std::vector<DigitalTrace>& traces,
        std::int64_t endTimeNs, std::string& error);

private:
    std::vector<DigitalTrace> m_traces;
    std::size_t m_capacity = 0;
    std::size_t m_sampleCount = 0;
};
