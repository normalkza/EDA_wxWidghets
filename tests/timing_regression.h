#pragma once

#include "DigitalSimulator.h"
#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace TimingRegression
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("digital timing: ") + message);
}

inline CircuitDocument Chain()
{
    CircuitDocument document;
    document.formatVersion = 3;
    document.simulation.stopTimeNs = 100;
    document.components = {{60, "PROBE", 600, 100, 1, false},
        {50, "DFF", 400, 100, 3, false}, {40, "DFF", 200, 100, 3, false},
        {30, "INPUT", 10, 200, 0, false}, {20, "INPUT", 10, 100, 0, true},
        {10, "CLOCK", 10, 20, 0, false}};
    document.components[0].probeLabel = "链输出,\"Q2\"";
    document.wires = {{10, 0, 40, 1}, {10, 0, 50, 1}, {20, 0, 40, 0},
        {40, 0, 50, 0}, {30, 0, 40, 2}, {30, 0, 50, 2}, {50, 0, 60, 0}};
    return document;
}

inline CircuitDocument Divider()
{
    CircuitDocument document;
    document.formatVersion = 3;
    document.simulation.stopTimeNs = 100;
    document.components = {{1, "CLOCK", 0, 0, 0, false}, {2, "DFF", 200, 0, 3, false},
        {3, "NOT", 300, 0, 1, false}, {4, "PROBE", 400, 0, 1, false}};
    document.wires = {{1, 0, 2, 1}, {2, 0, 3, 0}, {3, 0, 2, 0}, {2, 0, 4, 0}};
    return document;
}

inline void Run()
{
    DigitalSimulator simulator;
    wxString error;
    Require(!simulator.Step(error) && !error.empty(), "uncompiled simulator rejects stepping");
    auto design = Chain();
    Require(simulator.Compile(design, 71, error), "compile DFF chain");
    Require(simulator.GetTime() == 0 && simulator.GetRevision() == 71, "initial time and revision");
    Require(simulator.GetValue(40, PinType::Output, 0) == LogicValue::Low &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "initialization is not an edge");
    Require(simulator.GetTraces().front().samples.size() == 1, "baseline recorded before first t=0 event");
    Require(simulator.Step(error) && simulator.GetTime() == 0, "explicit first t=0 clock event");
    Require(simulator.GetValue(40, PinType::Output, 0) == LogicValue::High &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "same-edge chain advances one stage");
    Require(simulator.Step(error) && simulator.GetTime() == 10 &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "falling edge does not sample");
    Require(simulator.Step(error) && simulator.GetTime() == 20 &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::High, "second rising edge advances second stage");
    Require(simulator.QueueInput(30, LogicValue::High, 21, error) &&
        simulator.QueueInput(30, LogicValue::Low, 22, error) && simulator.AdvanceTo(22, error),
        "queue asynchronous reset assertion and release");
    Require(simulator.GetValue(40, PinType::Output, 0) == LogicValue::Low &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "reset without edge and release without sample");
    Require(design.components[4].high && !design.components[3].high, "runtime stimuli leave design values intact");

    std::ostringstream csv, vcd;
    Require(simulator.ExportCsv(csv, error) && simulator.ExportVcd(vcd, error), "export CSV and VCD");
    Require(csv.str().find("time_ns,delta,component_id") != std::string::npos &&
        csv.str().find("\"链输出,\"\"Q2\"\"\"") != std::string::npos,
        "CSV carries integer times, delta and quoted UTF-8 labels");
    Require(vcd.str().find("$timescale 1ns $end") != std::string::npos &&
        vcd.str().find("#22") != std::string::npos && vcd.str().find("$comment delta 2") != std::string::npos,
        "VCD carries nanosecond times, end horizon and delta order");
    Require(simulator.Reset(error) && simulator.Step(error) &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "reset repeats original baseline and first edge");

    // Inverting placement order does not change event ordering or sample values.
    DigitalSimulator reordered;
    auto reversed = design;
    std::reverse(reversed.components.begin(), reversed.components.end());
    std::reverse(reversed.wires.begin(), reversed.wires.end());
    Require(reordered.Compile(reversed, 72, error) && reordered.Step(error), "compile reversed design");
    Require(reordered.GetTraces().size() == simulator.GetTraces().size(), "same trace identities");
    for (std::size_t index = 0; index < simulator.GetTraces().size(); ++index)
        Require(reordered.GetTraces()[index].samples == simulator.GetTraces()[index].samples,
            "deterministic samples independent of document traversal order");

    auto invalid = design;
    invalid.components.back().clockHighNs = invalid.components.back().clockPeriodNs;
    Require(!simulator.Compile(invalid, 99, error) && simulator.GetRevision() == 71,
        "failed compile retains running snapshot");
    invalid = Divider();
    invalid.wires.push_back({3, 0, 2, 2});
    Require(!reordered.Compile(invalid, 99, error), "reject asynchronous reset feedback");
    invalid = Divider();
    invalid.wires[0].sourceId = 3;
    Require(!reordered.Compile(invalid, 99, error), "reject combinational generated clock");

    auto divider = Divider();
    Require(simulator.Compile(divider, 80, error) && simulator.Step(error), "legal DFF inverted feedback");
    Require(simulator.GetValue(2, PinType::Output, 0) == LogicValue::High, "divider first toggle");
    Require(simulator.StepClockPeriod(1, error) && simulator.GetTime() == 20 &&
        simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low, "selected-clock cycle step");
    Require(!simulator.StepClockPeriod(2, error), "cycle reference must be an actual clock");
    Require(!simulator.QueueInput(1, LogicValue::High, 20, error), "clock cannot be manually stimulated");
    Require(!simulator.AdvanceTo(19, error) && !simulator.AdvanceTo(101, error), "reject invalid target time");
    Require(simulator.AdvanceTo(100, error) && simulator.IsFinished() && !simulator.HasFailed(), "deterministic stop horizon");

    // Explicit initial clock High is not an edge, its first event is falling.
    divider.components[0].high = true;
    divider.components[0].clockStartNs = 5;
    divider.components[0].clockHighNs = 7;
    Require(simulator.Compile(divider, 81, error) && simulator.Step(error) && simulator.GetTime() == 5 &&
        simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low, "initial high and first falling edge");
    Require(simulator.Step(error) && simulator.GetTime() == 18 &&
        simulator.GetValue(2, PinType::Output, 0) == LogicValue::High, "non-50-percent duty low interval");

    // Simultaneous D/CLK are batched before the common sampling phase.
    auto manual = Chain();
    manual.components.back().type = "INPUT";
    manual.components[4].high = false;
    Require(simulator.Compile(manual, 82, error) &&
        simulator.QueueInput(20, LogicValue::High, 5, error) &&
        simulator.QueueInput(10, LogicValue::High, 5, error) && simulator.AdvanceTo(5, error), "same-time data and clock stimuli");
    Require(simulator.GetValue(40, PinType::Output, 0) == LogicValue::High &&
        simulator.GetValue(50, PinType::Output, 0) == LogicValue::Low, "batch then sample semantic");
    Require(simulator.SetInput(30, LogicValue::High, error) &&
        simulator.GetValue(40, PinType::Output, 0) == LogicValue::Low, "interactive reset at paused current time");
    Require(simulator.SetInput(30, LogicValue::Low, error) &&
        simulator.GetValue(40, PinType::Output, 0) == LogicValue::Low, "interactive release does not resample held clock");

    auto multiClock = Chain();
    multiClock.components.push_back({11, "CLOCK", 10, 300, 0, false});
    multiClock.components.back().clockPeriodNs = 30;
    multiClock.components.back().clockHighNs = 5;
    multiClock.wires[1].sourceId = 11;
    Require(simulator.Compile(multiClock, 83, error) && simulator.Step(error) && simulator.Step(error) &&
        simulator.GetTime() == 5, "independent clocks merge on integer event time");
    Require(simulator.GetClockIds() == std::vector<int>({10, 11}), "stable clock selection IDs");

    auto limited = Divider();
    limited.simulation.maxEvents = 1;
    Require(simulator.Compile(limited, 84, error) && simulator.Step(error) && !simulator.Step(error) &&
        simulator.HasFailed() && !error.empty(), "event cap halts with explicit diagnostic");
    limited = Divider();
    limited.simulation.maxDeltaCycles = 1;
    Require(simulator.Compile(limited, 85, error) && !simulator.Step(error) && simulator.HasFailed(),
        "delta cap halts with explicit diagnostic");
    limited = Divider();
    limited.components[0].high = true;
    limited.simulation.maxDeltaCycles = 2;
    Require(simulator.Compile(limited, 851, error) && simulator.Step(error), "prepare completed falling-edge time slot");
    const auto stableCount = simulator.GetEventCount();
    Require(!simulator.Step(error) && simulator.GetTime() == 0 && simulator.GetRecordedUntil() == 0 &&
        simulator.GetEventCount() == stableCount && simulator.GetValue(1, PinType::Output, 0) == LogicValue::Low &&
        simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low &&
        simulator.GetValue(4, PinType::Input, 0) == LogicValue::Low,
        "failed post-commit delta rolls back time, Q, combinational values and counters");
    for (const auto& trace : simulator.GetTraces())
        Require(trace.samples.back().timeNs == 0, "failed time slot leaves no partial trace samples");
    limited = Divider();
    limited.simulation.maxTraceSamples = 4;
    Require(simulator.Compile(limited, 86, error) && !simulator.Step(error) && simulator.HasFailed(),
        "trace cap stops without evicting baseline");
    for (const auto& trace : simulator.GetTraces())
        Require(!trace.samples.empty() && trace.samples.front().timeNs == 0, "capacity stop preserves initial samples");
    Require(simulator.Reset(error) && !simulator.HasFailed(), "reset clears resource-limit failure");
    limited = Divider();
    limited.components[0].high = true;
    limited.simulation.maxTraceSamples = 6;
    Require(simulator.Compile(limited, 861, error) && simulator.Step(error) && !simulator.Step(error) &&
        simulator.GetTime() == 0 && simulator.GetRecordedUntil() == 0 &&
        simulator.GetValue(2, PinType::Output, 0) == LogicValue::Low,
        "cache exhaustion during a later commit restores preceding complete time slot");
    std::ostringstream rollbackVcd;
    Require(simulator.ExportVcd(rollbackVcd, error) && rollbackVcd.str().find("#10") == std::string::npos,
        "failed time slot is never exported as complete");

    auto extreme = Divider();
    extreme.simulation.stopTimeNs = std::numeric_limits<std::int64_t>::max() - 1;
    extreme.components[0].clockStartNs = std::numeric_limits<std::int64_t>::max() - 21;
    Require(simulator.Compile(extreme, 87, error) && simulator.Step(error) && simulator.Step(error) &&
        simulator.Step(error) && simulator.GetTime() == std::numeric_limits<std::int64_t>::max() - 1 && simulator.IsFinished(),
        "clock scheduling at int64 boundary does not overflow");

    CircuitDocument empty;
    Require(simulator.Compile(empty, 88, error) && simulator.Step(error) && simulator.IsFinished() &&
        simulator.GetTraces().empty(), "empty circuit finishes without signals");
    auto zeroTime = Divider();
    zeroTime.simulation.stopTimeNs = 0;
    Require(simulator.Compile(zeroTime, 89, error) && !simulator.IsFinished() && simulator.Step(error) &&
        simulator.IsFinished() && simulator.GetValue(2, PinType::Output, 0) == LogicValue::High,
        "zero-length horizon still consumes explicitly scheduled t=0 events");
}
}
