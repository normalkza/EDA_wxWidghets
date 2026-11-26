#pragma once

#include "CircuitFile.h"
#include "CounterComponent.h"
#include "DigitalSimulator.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <stdexcept>
#include <string>

namespace CounterRegression
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(std::string("counter: ") + message);
}

inline CircuitDocument Base(int maximum, int initial)
{
    CircuitDocument document;
    document.formatVersion = 3;
    document.simulation.stopTimeNs = 500;
    document.components = {{1, "CLOCK", 0, 0, 0, false},
        {2, "COUNTER", 200, 0, 9, false}};
    document.components[0].clockPeriodNs = 100;
    document.components[0].clockHighNs = 50;
    document.components[0].clockStartNs = 10;
    document.components[1].counterMax = maximum;
    document.components[1].counterInitial = initial;
    document.wires = {{1, 0, 2, CounterComponent::PinClk}};
    return document;
}

inline int Value(const DigitalSimulator& simulator, int id)
{
    int value = 0;
    for (std::size_t bit = 0; bit < CounterComponent::QBitCount; ++bit)
        if (simulator.GetValue(id, PinType::Output, bit) == LogicValue::High)
            value |= 1 << bit;
    return value;
}

inline LogicValue Carry(const DigitalSimulator& simulator, int id)
{
    return *simulator.GetValue(id, PinType::Output, CounterComponent::PinCarry);
}

inline void CountAndWrap()
{
    DigitalSimulator simulator;
    wxString error;
    Require(simulator.Compile(Base(2, 0), 1, error), "4-bit counter compiles");
    Require(Value(simulator, 2) == 0 && Carry(simulator, 2) == LogicValue::Low,
        "counter starts at its configured initial value");
    Require(simulator.AdvanceTo(10, error) && Value(simulator, 2) == 1,
        "first rising clock edge increments the counter");
    Require(simulator.AdvanceTo(110, error) && Value(simulator, 2) == 2 &&
        Carry(simulator, 2) == LogicValue::High,
        "counter reaches the configured maximum and asserts carry");
    Require(simulator.AdvanceTo(210, error) && Value(simulator, 2) == 0 &&
        Carry(simulator, 2) == LogicValue::Low,
        "counter wraps to zero and clears carry");
    Require(simulator.Reset(error) && Value(simulator, 2) == 0,
        "reset replays the counter initial value");
    Require(simulator.AdvanceTo(10, error) && Value(simulator, 2) == 1,
        "reset restores the same clock waveform");
}

inline void LoadAndClear()
{
    auto document = Base(15, 0);
    document.simulation.stopTimeNs = 400;
    document.components.push_back({3, "CONSTANT", 0, 180, 0, true});
    document.components.push_back({4, "CONSTANT", 0, 260, 0, false});
    document.components.push_back({5, "INPUT", 0, 340, 0, false});
    document.components.push_back({6, "CONSTANT", 0, 420, 0, true});
    document.components.push_back({7, "CONSTANT", 0, 500, 0, true});
    document.components.push_back({8, "CONSTANT", 0, 580, 0, false});
    document.components.push_back({9, "CONSTANT", 0, 660, 0, false});
    document.wires.push_back({3, 0, 2, CounterComponent::PinLoad});
    document.wires.push_back({4, 0, 2, CounterComponent::PinDirection});
    document.wires.push_back({5, 0, 2, CounterComponent::PinClear});
    for (std::size_t bit = 0; bit < CounterComponent::QBitCount; ++bit)
        document.wires.push_back({static_cast<int>(6 + bit), 0, 2,
            static_cast<int>(CounterComponent::DataPin(bit))});

    DigitalSimulator simulator;
    wxString error;
    Require(simulator.Compile(document, 2, error), "counter with load and clear compiles");
    Require(simulator.AdvanceTo(10, error) && Value(simulator, 2) == 3,
        "LOAD samples the parallel data pins at the active edge");
    Require(simulator.SetInput(5, LogicValue::High, error) && Value(simulator, 2) == 0,
        "CLR clears the counter asynchronously without a clock edge");
    Require(simulator.SetInput(5, LogicValue::Low, error) &&
        simulator.AdvanceTo(110, error) && Value(simulator, 2) == 3,
        "releasing CLR lets the next active edge load again");
}

inline void CountDown()
{
    auto document = Base(3, 1);
    document.components.push_back({3, "CONSTANT", 0, 200, 0, false});
    document.wires.push_back({3, 0, 2, CounterComponent::PinDirection});
    DigitalSimulator simulator;
    wxString error;
    Require(simulator.Compile(document, 3, error), "down counter compiles");
    Require(simulator.AdvanceTo(10, error) && Value(simulator, 2) == 0 &&
        Carry(simulator, 2) == LogicValue::High,
        "down counter reaches zero and asserts carry");
    Require(simulator.AdvanceTo(110, error) && Value(simulator, 2) == 3 &&
        Carry(simulator, 2) == LogicValue::Low,
        "down counter wraps to the configured maximum");
}

inline void CascadeClock()
{
    auto document = Base(1, 0);
    document.simulation.stopTimeNs = 300;
    document.components.push_back({3, "COUNTER", 420, 0, 9, false});
    document.components[2].counterMax = 3;
    document.components[2].counterInitial = 0;
    document.components[2].fallingEdge = true;
    document.wires.push_back({2, CounterComponent::PinCarry, 3, CounterComponent::PinClk});

    DigitalSimulator simulator;
    wxString error;
    Require(simulator.Compile(document, 4, error),
        "a counter carry output may clock another counter");
    Require(simulator.AdvanceTo(10, error) && Value(simulator, 2) == 1 &&
        Value(simulator, 3) == 0,
        "first counter reaches maximum before its carry can clock the second");
    Require(simulator.AdvanceTo(110, error) && Value(simulator, 2) == 0 &&
        Value(simulator, 3) == 1,
        "falling carry edge increments the cascaded counter");
}

inline void PersistAndValidate()
{
    wxString path = wxFileName::CreateTempFileName(wxS("wx_demo_counter_regression_"));
    Require(!path.empty(), "create isolated counter fixture");
    struct Cleanup
    {
        wxString path;
        ~Cleanup() { wxRemoveFile(path); }
    } cleanup{path};

    const auto document = Base(9, 4);
    wxString error;
    Require(CircuitFile::Save(path, document, error), "counter design saves");
    CircuitDocument loaded;
    Require(CircuitFile::Load(path, loaded, error), "counter design loads");
    Require(loaded.components.size() == 2 && loaded.components[1].type == "COUNTER" &&
        loaded.components[1].counterMax == 9 && loaded.components[1].counterInitial == 4 &&
        loaded.components[1].inputCount == 9,
        "counter parameters survive save and load");

    auto invalidMaximum = document;
    invalidMaximum.components[1].counterMax = 16;
    Require(!CircuitFile::Validate(invalidMaximum, error), "counter maximum above 15 is rejected");
    auto invalidInitial = document;
    invalidInitial.components[1].counterInitial = 10;
    Require(!CircuitFile::Validate(invalidInitial, error), "counter initial above maximum is rejected");
    auto wrongPinCount = document;
    wrongPinCount.components[1].inputCount = 1;
    Require(!CircuitFile::Validate(wrongPinCount, error), "counter pin count is enforced");
}

inline void Run()
{
    CountAndWrap();
    LoadAndClear();
    CountDown();
    CascadeClock();
    PersistAndValidate();
}
}
