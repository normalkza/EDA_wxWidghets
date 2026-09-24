#include <iostream>

#include "AndGate.h"
#include "OrGate.h"
#include "NotGate.h"

int main()
{
    AndGate andGate;
    OrGate orGate;
    NotGate notGate;

    andGate.inputs[0].value = LogicValue::High;
    andGate.inputs[1].value = LogicValue::High;
    andGate.Evaluate();

    std::cout << "AND: "
        << static_cast<int>(andGate.outputs[0].value)
        << std::endl;

    orGate.inputs[0].value = LogicValue::Low;
    orGate.inputs[1].value = LogicValue::High;
    orGate.Evaluate();

    std::cout << "OR: "
        << static_cast<int>(orGate.outputs[0].value)
        << std::endl;

    notGate.inputs[0].value = LogicValue::High;
    notGate.Evaluate();

    std::cout << "NOT: "
        << static_cast<int>(notGate.outputs[0].value)
        << std::endl;

    return 0;
}