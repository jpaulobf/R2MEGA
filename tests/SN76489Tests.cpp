#include "Core/Audio/SN76489/SN76489.h"

#include <stdexcept>

using R2MEGA::Core::SN76489;

static void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("Falha no teste do PSG");
}

int main()
{
    SN76489 psg;
    psg.reset();
    require(psg.sample() == 0);

    psg.write(0x85); // Seleciona o tom 0 e escreve os quatro bits baixos.
    psg.write(0x12); // Completa o período de dez bits.
    require(psg.registerValue(0) == 0x125);
    psg.write(0x90); // Volume máximo do tom 0.
    require(psg.registerValue(1) == 0);
    require(psg.sample() < 0);

    psg.write(0x82); // Período curto para verificar a alternância.
    psg.write(0x00);
    for (unsigned i = 0; i < 16; ++i)
        psg.clock();
    require(psg.sample() > 0);
    for (unsigned i = 0; i < 32; ++i)
        psg.clock();
    require(psg.sample() < 0);

    psg.write(0xe4); // Ruído branco, frequência independente dos tons.
    require(psg.registerValue(6) == 4 && psg.noiseShiftRegister() == 0x8000);
    for (unsigned i = 0; i < 16 * 16; ++i)
        psg.clock();
    require(psg.noiseShiftRegister() != 0x8000);
    psg.write(0xff); // O quarto canal pode ser silenciado.
    require(psg.registerValue(7) == 15);

    psg.reset();
    require(psg.sample() == 0 && psg.noiseShiftRegister() == 0x8000);
}