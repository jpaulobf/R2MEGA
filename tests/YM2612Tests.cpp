#include "Core/Audio/YM2612/YM2612.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using R2MEGA::Core::YM2612;

namespace
{
    constexpr unsigned clocksPerSample = 144;

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void write(YM2612 &chip, unsigned reg, std::uint8_t value)
    {
        const unsigned port = (reg >> 8) * 2;
        chip.writePort(port, static_cast<std::uint8_t>(reg));
        chip.writePort(port + 1, value);
    }

    void clock(YM2612 &chip, unsigned clocks)
    {
        while (clocks--)
            chip.clock();
    }

    void testStatus()
    {
        YM2612 chip;
        chip.writePort(0, 0x22);
        check(chip.readPort(0) == 0, "Escrever endereço deve deixar BUSY limpo");
        chip.writePort(3, 0xff);
        check(chip.readPort(0) == 0 && chip.registerValue(1, 0x22) == 0,
              "Escrita de dados pela porta do outro banco deve ser ignorada");
        chip.writePort(1, 0x08);
        clock(chip, 191);
        check(chip.readPort(0) == 0x80 && chip.readPort(1) == 0 && chip.registerValue(0, 0x22) == 8,
              "BUSY deve ficar ativo durante os primeiros 191 clocks na porta de status");
        clock(chip, 1);
        check(chip.readPort(0) == 0, "BUSY deve baixar no clock 192");

        chip.reset();
        write(chip, 0x24, 0xff);
        write(chip, 0x25, 0x03);
        write(chip, 0x27, 0x05);
        clock(chip, clocksPerSample - 1);
        check((chip.readPort(0) & 1) == 0, "Timer A disparou antes de completar o período");
        clock(chip, 1);
        check((chip.readPort(0) & 1) != 0, "Timer A deve disparar no clock 144");
        write(chip, 0x27, 0x15); // Limpa a flag com o contador ainda ativo.
        clock(chip, clocksPerSample - 1);
        check((chip.readPort(0) & 1) == 0, "Timer A deve limpar a flag até o próximo período");
        clock(chip, 1);
        check((chip.readPort(0) & 1) != 0, "Timer A deve recarregar após disparar");
        write(chip, 0x27, 0x10);
        clock(chip, clocksPerSample * 2);
        check(chip.readPort(0) == 0, "Desligar e limpar Timer A deve cancelar o disparo");

        chip.reset();
        write(chip, 0x26, 0xff);
        write(chip, 0x27, 0x0a);
        clock(chip, clocksPerSample * 16 - 1);
        check(chip.readPort(0) == 0, "Timer B disparou antes de completar o período");
        clock(chip, 1);
        check(chip.readPort(0) == 2, "Timer B deve disparar após 16 clocks de síntese");
        chip.reset();
        check(chip.readPort(0) == 0, "Reset deve limpar os temporizadores");
    }

    void testSynthesis()
    {
        YM2612 chip;
        clock(chip, clocksPerSample * 2);
        check(chip.sample()[0] == 0 && chip.sample()[1] == 0, "O YM2612 deve iniciar em silêncio");

        write(chip, 0xb0, 0x07); // Algoritmo 7, quatro portadoras independentes.
        write(chip, 0xb4, 0x80); // Canal 1 à esquerda.
        write(chip, 0x3c, 0x01); // Slot 4 com multiplicador 1.
        write(chip, 0x4c, 0x00);
        write(chip, 0x5c, 0x1f);
        write(chip, 0x8c, 0x0f);
        write(chip, 0xa4, 0x24);
        write(chip, 0xa0, 0x3b);
        write(chip, 0x28, 0x80); // Key-on do slot 4.
        std::int32_t minimum = 0, maximum = 0;
        for (unsigned sample = 0; sample < 512; ++sample)
        {
            clock(chip, clocksPerSample);
            const auto [left, right] = chip.sample();
            check(right == 0, "O panorama do canal 1 deve preservar o canal direito silencioso");
            minimum = std::min(minimum, left);
            maximum = std::max(maximum, left);
        }
        check(minimum < -1000 && maximum > 1000, "O canal FM deve oscilar após key-on");

        chip.reset();
        write(chip, 0x1b6, 0x40); // Canal 6 à direita, no banco superior.
        write(chip, 0x2a, 0xe0);
        write(chip, 0x2b, 0x80);
        clock(chip, clocksPerSample);
        check(chip.sample()[0] == 0 && chip.sample()[1] > 1000,
              "O DAC deve substituir o canal 6 e usar seu panorama");
        write(chip, 0x2a, 0x20);
        clock(chip, clocksPerSample);
        check(chip.sample()[0] == 0 && chip.sample()[1] < -1000, "A amostra do DAC deve atualizar sua saída");
        write(chip, 0x2b, 0);
        clock(chip, clocksPerSample);
        check(chip.sample()[0] == 0 && chip.sample()[1] == 0, "Desligar o DAC deve devolver o canal 6 ao FM");
        write(chip, 0x2b, 0x80);
        chip.reset();
        clock(chip, clocksPerSample);
        check(chip.sample()[0] == 0 && chip.sample()[1] == 0 && chip.registerValue(0, 0x2b) == 0,
              "Reset deve apagar o latch de DAC e silenciar a saída");
    }
}

int main()
{
    try
    {
        testStatus();
        testSynthesis();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}