#include "Core/CPU/Moto68000/Moto68000.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint8_t> bytes;
        std::vector<std::uint32_t> reads;
        std::vector<std::pair<std::uint32_t, unsigned>> readCodes;

        void putWord(std::uint32_t address, std::uint16_t value)
        {
            bytes[address] = static_cast<std::uint8_t>(value >> 8);
            bytes[address + 1] = static_cast<std::uint8_t>(value);
        }
        std::uint16_t word(std::uint32_t address) const
        {
            const auto high = bytes.find(address), low = bytes.find(address + 1);
            return (std::uint16_t(high == bytes.end() ? 0 : high->second) << 8) |
                   (low == bytes.end() ? 0 : low->second);
        }
        std::uint8_t readByte(std::uint32_t) override
        {
            throw std::logic_error("leitura de byte inesperada");
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            return word(address);
        }
        void writeByte(std::uint32_t, std::uint8_t) override
        {
            throw std::logic_error("escrita de byte inesperada");
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            putWord(address, value);
        }
        void observe(const M68KPins &pins) override
        {
            if (pins.active && !pins.write && pins.phase == 2 && !pins.waiting)
                readCodes.emplace_back(pins.address, pins.functionCode);
        }
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(8 * 4, 0);
            putWord(8 * 4 + 2, 0x200);
            putWord(9 * 4, 0);
            putWord(9 * 4 + 2, 0x240);
            for (unsigned address = 0x100; address < 0x130; address += 2)
                putWord(address, 0x4e71);
            for (unsigned address : {0x200u, 0x202u, 0x240u, 0x242u})
                putWord(address, 0x4e71);
        }
    };

    void check(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void boot(Moto68000 &cpu)
    {
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
    }
    unsigned execute(Moto68000 &cpu)
    {
        const auto before = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == before && cycles < 100)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == before + 1, "instrução não terminou");
        return cycles;
    }
    void runUntilPc(Moto68000 &cpu, std::uint32_t pc)
    {
        unsigned cycles = 0;
        while (cpu.state().pc != pc && cycles++ < 100)
            cpu.clock();
        check(cpu.state().pc == pc, "tratador de exceção não foi carregado");
    }

    void sixForms()
    {
        struct Case
        {
            std::uint16_t opcode, immediate, expected;
        };
        constexpr std::array<Case, 6> cases{{
            {0x003c, 0x00a2, 0x2717}, // ORI em CCR ignora os bits reservados.
            {0x023c, 0xfff8, 0x2710}, // ANDI em CCR preserva o byte superior.
            {0x0a3c, 0xff07, 0x2712}, // EORI em CCR ignora o byte alto.
            {0x007c, 0xd8e2, 0xa717}, // ORI em SR altera T e V e ignora os bits reservados.
            {0x027c, 0x20ff, 0x2015}, // ANDI em SR limpa a máscara de interrupção.
            {0x0a7c, 0x5fe4, 0x2011}  // EORI em SR altera máscara e Z e ignora os bits reservados.
        }};
        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x70ff); // MOVEQ #-1,D0.
            bus.putWord(0x102, 0x5200); // ADDQ.B #1,D0: CCR = $15.
            bus.putWord(0x104, test.opcode);
            bus.putWord(0x106, test.immediate);
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            execute(cpu);
            check(cpu.state().sr == 0x2715, "preparo do SR");
            bus.reads.clear();
            bus.readCodes.clear();
            const std::string label = "operação em CCR/SR " +
                                      std::to_string(test.opcode);
            check(execute(cpu) == 20, label + ": duração");
            check(cpu.state().sr == test.expected && cpu.state().pc == 0x108 &&
                      cpu.state().data[0] == 0xffffff00u,
                  label + ": SR, PC e D0");
            check(cpu.state().prefetch[0] == bus.word(0x108) &&
                      cpu.state().prefetch[1] == bus.word(0x10a),
                  label + ": fila de pré-busca");
            check(bus.reads == std::vector<std::uint32_t>({0x108, 0x108, 0x10a}),
                  label + ": três leituras de programa");
            check(bus.readCodes ==
                      std::vector<std::pair<std::uint32_t, unsigned>>({{0x108, 6}, {0x108, 6}, {0x10a, 6}}),
                  label + ": código de função de supervisor");
        }
    }

    void userTransitionAndPrivilege()
    {
        constexpr std::array<std::pair<std::uint16_t, std::uint16_t>, 2> transitions{{
            {0x0a7c, 0x78e0}, // EORI remove S e ignora os bits reservados.
            {0x027c, 0xdfff}  // ANDI remove S.
        }};
        for (const auto [transition, mask] : transitions)
            for (const std::uint16_t privileged :
                 {std::uint16_t(0x007c), std::uint16_t(0x027c),
                  std::uint16_t(0x0a7c)})
            {
                Bus bus;
                bus.setup();
                bus.putWord(0x100, transition);
                bus.putWord(0x102, mask);
                bus.putWord(0x104, 0x003c);
                bus.putWord(0x106, 0x0011); // ORI #$11,CCR.
                bus.putWord(0x108, privileged);
                bus.putWord(0x10a, 0x2000);
                Moto68000 cpu(bus);
                boot(cpu);
                bus.reads.clear();
                bus.readCodes.clear();
                check(execute(cpu) == 20 && cpu.state().sr == 0x0700 &&
                          cpu.state().address[7] == 0 && cpu.state().pc == 0x104,
                      "ANDI/EORI em SR trocam SSP pelo USP");
                check(bus.reads ==
                              std::vector<std::uint32_t>({0x104, 0x104, 0x106}) &&
                          bus.readCodes ==
                              std::vector<std::pair<std::uint32_t, unsigned>>({{0x104, 6}, {0x104, 2}, {0x106, 2}}),
                      "mudança de S refaz a pré-busca no espaço de usuário");

                bus.reads.clear();
                bus.readCodes.clear();
                check(execute(cpu) == 20 && cpu.state().sr == 0x0711 &&
                          cpu.state().address[7] == 0 && cpu.state().pc == 0x108,
                      "ORI em CCR é permitido no modo usuário");
                check(bus.readCodes ==
                          std::vector<std::pair<std::uint32_t, unsigned>>({{0x108, 2}, {0x108, 2}, {0x10a, 2}}),
                      "pré-busca de CCR em modo usuário usa FC2");

                bus.reads.clear();
                bus.readCodes.clear();
                runUntilPc(cpu, 0x200);
                check(cpu.state().instructions == 2 &&
                          cpu.state().address[7] == 0x17fa &&
                          cpu.state().sr == 0x2711 && bus.word(0x17fa) == 0x0711 &&
                          bus.word(0x17fe) == 0x108,
                      "ORI/ANDI/EORI em SR de usuário geram violação de privilégio");
                for (std::uint32_t address : bus.reads)
                    check(address != 0x10c,
                          "violação de privilégio ocorre antes da extensão seguinte");
            }
    }

    void traceAfterStatusChange()
    {
        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x007c);
        bus.putWord(0x102, 0x8000); // ORI #$8000,SR.
        Moto68000 cpu(bus);
        boot(cpu);
        check(execute(cpu) == 20 && cpu.state().sr == 0xa700 &&
                  cpu.state().pc == 0x104,
              "ORI em SR habilita T");
        check(execute(cpu) == 4 && cpu.state().pc == 0x106,
              "a instrução que habilita T não gera rastreamento imediato");
        runUntilPc(cpu, 0x240);
        check(bus.word(0x17fa) == 0xa700 && bus.word(0x17fe) == 0x106,
              "rastreamento salva o SR e o PC após a instrução seguinte");
    }
}

int main()
{
    try
    {
        sixForms();
        userTransitionAndPrivilege();
        traceAfterStatusChange();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}