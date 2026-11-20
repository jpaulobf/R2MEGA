#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace R2MEGA::Core;

struct Bus : M68KBus
{
    std::array<std::uint16_t, 32> words{};
    std::vector<std::uint32_t> reads;
    std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte"); }
    std::uint16_t readWord(std::uint32_t address) override
    {
        reads.push_back(address);
        return words.at(address / 2);
    }
    void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("write"); }
    void writeWord(std::uint32_t, std::uint16_t) override { throw std::logic_error("write"); }
};

static void require(bool value)
{
    if (!value)
        throw std::runtime_error("Falha no teste de Bcc/DBcc");
}

static void tick(Moto68000 &cpu, unsigned cycles)
{
    while (cycles--)
        cpu.clock();
}

int main()
{
    // Valores esperados com C=V=0 para N, Z e resultado positivo não nulo.
    constexpr std::array<unsigned, 3> conditions{
        0xa954, // N=1
        0x9598, // Z=1
        0x5554  // N=Z=0
    };
    for (unsigned flags = 0; flags < 3; ++flags)
        for (unsigned condition = 2; condition < 16; ++condition)
            for (bool word : {false, true})
            {
                Bus bus;
                bus.words[3] = 8;
                bus.words[4] = flags == 0 ? 0x70ff : flags == 1 ? 0x7000
                                                                : 0x7001;
                bus.words[5] = std::uint16_t(0x6000 | (condition << 8) | (word ? 0 : 4));
                bus.words[6] = word ? 6 : 0x7201;
                bus.words[7] = word ? 0x7201 : 0x4e71;
                bus.words[8] = word ? 0x4e71 : 0x7202;
                bus.words[9] = word ? 0x7202 : 0x4e71;
                bus.words[10] = 0x4e71;
                bus.words[11] = 0x4e71;
                Moto68000 cpu(bus);
                cpu.reset();
                tick(cpu, 44); // Reset e MOVEQ que define as flags.
                const bool taken = (conditions[flags] & (1u << condition)) != 0;
                const auto before = bus.reads.size();
                const unsigned duration = taken ? 10 : word ? 12
                                                            : 8;
                tick(cpu, duration - 1);
                require(cpu.state().pc == 10 && cpu.state().instructions == 1);
                tick(cpu, 1);
                const unsigned target = taken ? (word ? 18 : 16) : (word ? 14 : 12);
                require(cpu.state().pc == target && cpu.state().cycles == 44 + duration);
                require(bus.reads.size() - before == (taken || word ? 2u : 1u));
                require(bus.reads[before] == (taken ? target : word ? 14u
                                                                    : 14u));
                require(cpu.state().prefetch[0] == (taken ? 0x7202 : 0x7201));
                tick(cpu, 4);
                require(cpu.state().data[1] == (taken ? 2u : 1u));
            }

    // DBcc não modifica CCR e decrementa somente a palavra baixa de Dn.
    for (unsigned scenario = 0; scenario < 4; ++scenario)
    {
        Bus bus;
        bus.words[3] = 8;
        bus.words[4] = scenario == 0 ? 0x7201 : scenario == 1 ? 0x7200
                                            : scenario == 2   ? 0x7200
                                                              : 0x72ff;
        bus.words[5] = scenario == 2 ? 0x57c9 : 0x51c9; // DBEQ ou DBF D1.
        bus.words[6] = 8;                               // Destino em $14.
        bus.words[7] = 0x7401;
        bus.words[8] = 0x4e71;
        bus.words[9] = 0x4e71;
        bus.words[10] = 0x7402;
        bus.words[11] = 0x4e71;
        Moto68000 cpu(bus);
        cpu.reset();
        tick(cpu, 44);
        const auto flagsBefore = cpu.state().sr;
        const auto readsBefore = bus.reads.size();
        const unsigned duration = scenario == 0 || scenario == 3 ? 10 : scenario == 1 ? 14
                                                                                      : 12;
        tick(cpu, duration - 1);
        require(cpu.state().pc == 10 && cpu.state().instructions == 1);
        tick(cpu, 1);
        require(cpu.state().cycles == 44 + duration && cpu.state().instructions == 2);
        require(cpu.state().pc == (scenario == 0 || scenario == 3 ? 20u : 14u));
        require(cpu.state().sr == flagsBefore);
        require(bus.reads.size() - readsBefore == (scenario == 1 ? 3u : 2u));
        require(cpu.state().prefetch[0] == (scenario == 0 || scenario == 3 ? 0x7402 : 0x7401));
        require(cpu.state().data[1] == (scenario == 0 ? 0u : scenario == 1 ? 0x0000ffffu
                                                         : scenario == 2   ? 0u
                                                                           : 0xfffffffeu));
    }
}