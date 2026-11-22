#include "Core/IO/ControllerPorts.h"
#include <stdexcept>

using namespace R2MEGA::Core;

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha nas portas dos controles");
}

int main()
{
    ControllerPorts ports;
    ports.reset();
    require(ports.readByte(3) == 0x7f && ports.readByte(5) == 0x7f);
    ports.writeByte(9, 0x40);  // TH da porta 1 como saída.
    ports.writeByte(11, 0x40); // TH da porta 2 como saída.
    ports.setButtons(0, ControllerInput::Up | ControllerInput::Left |
                            ControllerInput::A | ControllerInput::B |
                            ControllerInput::C | ControllerInput::Start);
    ports.setButtons(1, ControllerInput::Right | ControllerInput::C);
    require(ports.readByte(3) == 0x02); // TH baixo: cima, A e Start.
    require(ports.readByte(5) == 0x33);
    ports.writeByte(3, 0x40);
    ports.writeByte(5, 0x40);
    require(ports.readByte(3) == 0x4a); // TH alto: cima, esquerda, B e C.
    require(ports.readByte(5) == 0x57); // P2 é independente.
    ports.writeByte(9, 0);
    require(ports.readByte(3) == 0x4a); // TH em entrada permanece alto.
    ports.reset();
    require(ports.buttons(0) == 0 && ports.buttons(1) == 0 && ports.readByte(3) == 0x7f);
}