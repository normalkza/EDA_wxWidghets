#include "TraceRecorder.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <tuple>
#include <utility>

namespace
{
std::string CsvField(const std::string& field)
{
    std::string result = "\"";
    for (const char character : field)
    {
        if (character == '"') result += '"';
        result += character;
    }
    return result + '"';
}

std::string VcdName(const DigitalTrace& trace)
{
    std::string name;
    for (const unsigned char character : trace.name)
        name += ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_') ?
            static_cast<char>(character) : '_';
    return "c" + std::to_string(trace.componentId) + "_" + name + "_" +
        (trace.type == PinType::Input ? "in" : "out") + std::to_string(trace.index);
}

struct SampleReference
{
    std::size_t trace;
    const TraceSample* sample;
};

std::vector<SampleReference> OrderedSamples(const std::vector<DigitalTrace>& traces)
{
    std::vector<SampleReference> result;
    std::size_t count = 0;
    for (const auto& trace : traces) count += trace.samples.size();
    result.reserve(count);
    for (std::size_t trace = 0; trace < traces.size(); ++trace)
        for (const auto& sample : traces[trace].samples) result.push_back({trace, &sample});
    std::stable_sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        return std::tie(first.sample->timeNs, first.sample->delta, first.trace) <
            std::tie(second.sample->timeNs, second.sample->delta, second.trace);
    });
    return result;
}
}

void TraceRecorder::Initialize(std::vector<DigitalTrace> traces, std::size_t capacity)
{
    m_traces = std::move(traces);
    for (auto& trace : m_traces) trace.samples.clear();
    m_capacity = capacity;
    m_sampleCount = 0;
}

TraceRecorder::Checkpoint TraceRecorder::MakeCheckpoint() const
{
    Checkpoint checkpoint;
    checkpoint.sampleCount = m_sampleCount;
    checkpoint.lengths.reserve(m_traces.size());
    for (const auto& trace : m_traces) checkpoint.lengths.push_back(trace.samples.size());
    return checkpoint;
}

void TraceRecorder::Restore(const Checkpoint& checkpoint)
{
    for (std::size_t index = 0; index < m_traces.size(); ++index)
        m_traces[index].samples.resize(checkpoint.lengths[index]);
    m_sampleCount = checkpoint.sampleCount;
}

bool TraceRecorder::Record(std::int64_t timeNs, std::size_t delta,
    const std::vector<LogicValue>& values, std::string& error)
{
    error.clear();
    if (values.size() != m_traces.size())
    {
        error = "Trace signal count does not match the compiled model.";
        return false;
    }
    std::size_t changes = 0;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto& samples = m_traces[index].samples;
        if (!samples.empty() && (timeNs < samples.back().timeNs ||
            (timeNs == samples.back().timeNs && delta < samples.back().delta)))
        {
            error = "Trace samples must have monotonic (time, delta) ordering.";
            return false;
        }
        if (samples.empty() || samples.back().value != values[index]) ++changes;
    }
    if (changes > m_capacity - m_sampleCount)
    {
        error = "Trace sample capacity reached; recording stopped without discarding earlier data.";
        return false;
    }
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        auto& samples = m_traces[index].samples;
        if (samples.empty() || samples.back().value != values[index])
            samples.push_back({timeNs, delta, values[index]});
    }
    m_sampleCount += changes;
    return true;
}

bool TraceRecorder::WriteCsv(std::ostream& output, const std::vector<DigitalTrace>& traces,
    std::int64_t endTimeNs, std::string& error)
{
    error.clear();
    output << "time_ns,delta,component_id,pin_type,pin_index,signal,value\r\n";
    for (const auto& entry : OrderedSamples(traces))
    {
        const auto& trace = traces[entry.trace];
        const auto& sample = *entry.sample;
        if (sample.timeNs > endTimeNs) continue;
        output << sample.timeNs << ',' << sample.delta << ',' << trace.componentId << ','
            << (trace.type == PinType::Input ? "input" : "output") << ',' << trace.index << ','
            << CsvField(trace.name) << ',' << (sample.value == LogicValue::High ? '1' : '0') << "\r\n";
    }
    // Add an explicit final sample for every signal so a CSV viewer can tell
    // how long the last unchanged value remained valid.
    std::size_t terminalDelta = 0;
    for (const auto& trace : traces)
        for (const auto& sample : trace.samples)
            if (sample.timeNs == endTimeNs)
                terminalDelta = std::max(terminalDelta, sample.delta);
    if (terminalDelta < std::numeric_limits<std::size_t>::max()) ++terminalDelta;
    for (const auto& trace : traces)
    {
        const TraceSample* last = nullptr;
        for (const auto& sample : trace.samples)
            if (sample.timeNs <= endTimeNs) last = &sample;
        if (!last) continue;
        output << endTimeNs << ',' << terminalDelta << ',' << trace.componentId << ','
            << (trace.type == PinType::Input ? "input" : "output") << ',' << trace.index << ','
            << CsvField(trace.name) << ',' << (last->value == LogicValue::High ? '1' : '0') << "\r\n";
    }
    if (!output)
    {
        error = "Cannot write CSV waveform data.";
        return false;
    }
    return true;
}

bool TraceRecorder::WriteVcd(std::ostream& output, const std::vector<DigitalTrace>& traces,
    std::int64_t endTimeNs, std::string& error)
{
    error.clear();
    output << "$version wx_demo deterministic digital simulator $end\n"
        << "$timescale 1ns $end\n$scope module wx_demo $end\n";
    for (std::size_t index = 0; index < traces.size(); ++index)
        output << "$var wire 1 s" << index << ' ' << VcdName(traces[index]) << " $end\n";
    output << "$upscope $end\n$enddefinitions $end\n";
    std::int64_t lastTime = -1;
    std::size_t lastDelta = 0;
    for (const auto& entry : OrderedSamples(traces))
    {
        const auto& sample = *entry.sample;
        if (sample.timeNs > endTimeNs) continue;
        if (sample.timeNs != lastTime)
        {
            output << '#' << sample.timeNs << '\n';
            lastTime = sample.timeNs;
            lastDelta = 0;
        }
        if (sample.delta != lastDelta)
        {
            output << "$comment delta " << sample.delta << " $end\n";
            lastDelta = sample.delta;
        }
        output << (sample.value == LogicValue::High ? '1' : '0') << 's' << entry.trace << '\n';
    }
    if (endTimeNs > lastTime) output << '#' << endTimeNs << '\n';
    if (!output)
    {
        error = "Cannot write VCD waveform data.";
        return false;
    }
    return true;
}
