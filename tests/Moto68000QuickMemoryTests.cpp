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
        std::vector<std::pair<char, std::uint32_t>> transfers;
        std::uint32_t waitAddress = 0xffffffffu;
        unsigned waits = 0;

        void putWord(std::uint32_t address, std::uint16_t value)
        {
            bytes[address] = static_cast<std::uint8_t>(value >> 8);
            bytes[address + 1] = static_cast<std::uint8_t>(value);
        }
        std::uint8_t byte(std::uint32_t address) const
        {
            const auto found = bytes.find(address);
            return found == bytes.end() ? 0 : found->second;
        }
        std::uint16_t word(std::uint32_t address) const
        {
            return (std::uint16_t(byte(address)) << 8) | byte(address + 1);
        }
        std::uint8_t readByte(std::uint32_t address) override
        {
            transfers.emplace_back('r', address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            transfers.emplace_back('R', address);
            return word(address);
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            transfers.emplace_back('w', address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            transfers.emplace_back('W', address);
            putWord(address, value);
        }
        bool tryReadByte(std::uint32_t address, std::uint8_t &value) override
        {
            if (address == waitAddress && waits)
            {
                --waits;
                return false;
            }
            value = readByte(address);
            return true;
        }
        bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
        {
            if (address == waitAddress && waits)
            {
                --waits;
                return false;
            }
            value = readWord(address);
            return true;
        }
        void setup()
        {
            putWord(0, 0);
            putWord(2, 0x1800);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(16, 0);
            putWord(18, 0x200);
            for (unsigned address = 0x100; address < 0x130; address += 2)
                putWord(address, 0x4e71);
            putWord(0x200, 0x4e71);
            putWord(0x202, 0x4e71);
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
    std::uint16_t opcode(bool subtract, unsigned size, unsigned mode, unsigned reg)
    {
        return static_cast<std::uint16_t>(0x5000 | (subtract ? 0x0100 : 0) |
                                          (subtract ? 0 : 1 << 9) | (size << 6) | (mode << 3) | reg);
    }

    void addressRegisters()
    {
        for (bool subtract : {false, true})
            for (unsigned size : {1u, 2u})
            {
                Bus bus;
                bus.setup();
                bus.putWord(0x100, 0x41f8);
                bus.putWord(0x102, 0xfffc); // LEA -4,A0
                bus.putWord(0x104, 0x70ff); // N = 1, demais flags = 0.
                bus.putWord(0x106, opcode(subtract, size, 1, 0));
                Moto68000 cpu(bus);
                boot(cpu);
                execute(cpu);
                execute(cpu);
                bus.transfers.clear();
                check(execute(cpu) == 8, "ADDQ/SUBQ em An deve consumir oito ciclos");
                check(cpu.state().address[0] == (subtract ? 0xfffffff4u : 0xfffffffdu),
                      "ADDQ/SUBQ em An deve operar com 32 bits");
                check((cpu.state().sr & 0x1f) == 8, "ADDQ/SUBQ em An não altera flags");
                check(bus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x10a}}), "An deve fazer somente a pré-busca");
            }

        Bus invalid;
        invalid.setup();
        invalid.putWord(0x100, 0x5208); // ADDQ.B #1,A0: forma inválida.
        Moto68000 cpu(invalid);
        boot(cpu);
        for (unsigned i = 0; i < 100 && cpu.state().pc != 0x200; ++i)
            cpu.clock();
        check(cpu.state().pc == 0x200, "ADDQ.B em An deve gerar instrução ilegal");
    }

    void memoryModes()
    {
        // Modo, registrador, extensão, base de An e custo adicional de EA.
        struct Mode
        {
            unsigned mode, reg;
            std::uint16_t extension;
            std::uint16_t base;
            unsigned extra;
        };
        constexpr std::array<Mode, 7> modes{{{2, 0, 0, 0x300, 0}, {3, 0, 0, 0x300, 0}, {4, 0, 0, 0x302, 2}, {5, 0, 2, 0x2fe, 4}, {6, 0, 0x1002, 0x2fc, 6}, {7, 0, 0x300, 0x300, 4}, {7, 1, 0, 0x300, 8}}};
        for (const auto &mode : modes)
            for (bool subtract : {false, true})
                for (unsigned size : {0u, 1u, 2u})
                {
                    Bus bus;
                    bus.setup();
                    const unsigned bytes = size == 2 ? 4 : size == 1 ? 2
                                                                     : 1;
                    std::uint32_t base = mode.base;
                    if (mode.mode == 4)
                        base = 0x300 + (bytes == 1 ? 1 : bytes);
                    bus.putWord(0x100, 0x41f8);
                    bus.putWord(0x102, static_cast<std::uint16_t>(base));
                    bus.putWord(0x104, 0x7202); // D1.W = 2 para a indexação.
                    bus.putWord(0x106, 0x70ff); // Flags iniciais conhecidas.
                    bus.putWord(0x108, opcode(subtract, size, mode.mode, mode.reg));
                    unsigned extensions = mode.mode == 7 && mode.reg == 1 ? 2 : mode.mode >= 5 ? 1
                                                                                               : 0;
                    if (extensions)
                    {
                        bus.putWord(0x10a, mode.extension);
                        if (extensions == 2)
                            bus.putWord(0x10c, 0x300);
                    }
                    const std::uint32_t initial = subtract ? 0 : size == 0 ? 0xff
                                                             : size == 1   ? 0x7fff
                                                                           : 0xffffffffu;
                    const std::uint32_t result = subtract    ? size == 0 ? 0xf8 : size == 1 ? 0xfff8
                                                                                            : 0xfffffff8u
                                                 : size == 0 ? 0
                                                 : size == 1 ? 0x8000
                                                             : 0;
                    if (size == 0)
                        bus.bytes[0x300] = static_cast<std::uint8_t>(initial);
                    else if (size == 1)
                        bus.putWord(0x300, static_cast<std::uint16_t>(initial));
                    else
                    {
                        bus.putWord(0x300, static_cast<std::uint16_t>(initial >> 16));
                        bus.putWord(0x302, static_cast<std::uint16_t>(initial));
                    }
                    Moto68000 cpu(bus);
                    boot(cpu);
                    execute(cpu);
                    execute(cpu);
                    execute(cpu);
                    bus.transfers.clear();
                    const unsigned expected = (size == 2 ? 20 : 12) + mode.extra;
                    const std::string label = std::string(subtract ? "SUBQ" : "ADDQ") +
                                              " tamanho " + std::to_string(size) + " modo " +
                                              std::to_string(mode.mode) + "/" + std::to_string(mode.reg);
                    check(execute(cpu) == expected, label + ": duração");
                    const std::uint32_t actual = size == 0 ? bus.byte(0x300) : size == 1 ? bus.word(0x300)
                                                                                         : (std::uint32_t(bus.word(0x300)) << 16) | bus.word(0x302);
                    check(actual == result, label + ": resultado");
                    check((cpu.state().sr & 0x1f) == (subtract ? 0x19 : size == 1 ? 0x0a
                                                                                  : 0x15),
                          label + ": flags");
                    const std::uint32_t updated = mode.mode == 3   ? base +
                                                                         (bytes == 1 && mode.reg == 7 ? 2 : bytes)
                                                  : mode.mode == 4 ? 0x300
                                                                   : base;
                    check(cpu.state().address[0] == updated, label + ": An");
                    std::vector<std::pair<char, std::uint32_t>> expectedTransfers;
                    for (unsigned i = 0; i < extensions; ++i)
                        expectedTransfers.emplace_back('R', 0x10c + i * 2);
                    for (unsigned i = 0; i < (size == 2 ? 2u : 1u); ++i)
                        expectedTransfers.emplace_back(size == 0 ? 'r' : 'R', 0x300 + i * 2);
                    expectedTransfers.emplace_back('R', 0x10c + extensions * 2);
                    for (unsigned i = 0; i < (size == 2 ? 2u : 1u); ++i)
                        expectedTransfers.emplace_back(size == 0 ? 'w' : 'W',
                                                       0x300 + (size == 2 ? 1 - i : i) * 2);
                    check(bus.transfers == expectedTransfers, label + ": ordem do barramento");
                }
    }

    void stackPointerAndWaits()
    {
        for (unsigned mode : {3u, 4u})
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x4ff8); // LEA valor,A7.
            bus.putWord(0x102, mode == 3 ? 0x300 : 0x302);
            bus.putWord(0x104, opcode(false, 0, mode, 7));
            bus.bytes[0x300] = 0xfe;
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            bus.transfers.clear();
            check(execute(cpu) == (mode == 3 ? 12u : 14u),
                  "ADDQ.B em A7: duração");
            check(cpu.state().address[7] == (mode == 3 ? 0x302u : 0x300u) &&
                      bus.byte(0x300) == 0xff,
                  "ADDQ.B em A7 deve avançar em dois bytes");
            check(bus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'r', 0x300}, {'R', 0x108}, {'w', 0x300}}),
                  "ADDQ.B em A7: ordem do barramento");
        }

        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x300);
        wait.putWord(0x104, opcode(false, 1, 2, 0));
        wait.putWord(0x300, 1);
        Moto68000 cpu(wait);
        boot(cpu);
        execute(cpu);
        wait.transfers.clear();
        wait.waitAddress = 0x300;
        wait.waits = 2;
        check(execute(cpu) == 14 && wait.word(0x300) == 2,
              "espera de leitura deve prolongar ADDQ.W sem repetir a escrita");
        check(wait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x300}, {'R', 0x108}, {'W', 0x300}}),
              "espera de leitura não deve duplicar transferências");
    }

    void conditionByteStores()
    {
        Bus postincrement;
        postincrement.setup();
        postincrement.putWord(0x100, 0x41f8); // LEA $0300.W,A0.
        postincrement.putWord(0x102, 0x0300);
        postincrement.putWord(0x104, 0x56d8); // SNE (A0)+.
        postincrement.bytes[0x300] = 0x12;
        Moto68000 postCpu(postincrement);
        boot(postCpu);
        execute(postCpu);
        const auto postFlags = postCpu.state().sr;
        postincrement.transfers.clear();
        postincrement.waitAddress = 0x300;
        postincrement.waits = 2;
        check(execute(postCpu) == 14 &&
                  postincrement.transfers == std::vector<std::pair<char, std::uint32_t>>({{'r', 0x300}, {'R', 0x108}, {'w', 0x300}}),
              "Scc lê o destino, faz a pré-busca e escreve uma vez após a espera");
        check(postincrement.byte(0x300) == 0xff && postCpu.state().address[0] == 0x301 &&
                  postCpu.state().sr == postFlags && postCpu.state().pc == 0x106,
              "SNE (An)+ deve escrever byte e incrementar o endereço sem alterar flags");

        Bus bus;
        bus.setup();
        bus.putWord(0x100, 0x56f8); // SNE $0300.W.
        bus.putWord(0x102, 0x0300);
        bus.putWord(0x104, 0x56c0); // SNE D0.
        bus.putWord(0x106, 0x51c0); // SF D0.
        bus.bytes[0x300] = 0x12;
        Moto68000 cpu(bus);
        boot(cpu);
        const auto flags = cpu.state().sr;
        execute(cpu);
        check(bus.byte(0x300) == 0xff && cpu.state().pc == 0x104 && cpu.state().sr == flags,
              "SNE absoluto curto deve escrever um byte sem alterar os flags");
        check(execute(cpu) == 6 && (cpu.state().data[0] & 0xff) == 0xff && cpu.state().sr == flags,
              "Scc verdadeiro em Dn usa seis ciclos e preserva os flags");
        check(execute(cpu) == 4 && (cpu.state().data[0] & 0xff) == 0 && cpu.state().sr == flags,
              "Scc falso em Dn usa quatro ciclos e preserva os flags");

        Bus equal;
        equal.setup();
        equal.putWord(0x100, 0x7000); // MOVEQ #0,D0 marca Z.
        equal.putWord(0x102, 0x56f8);
        equal.putWord(0x104, 0x0300);
        equal.bytes[0x300] = 0xff;
        Moto68000 equalCpu(equal);
        boot(equalCpu);
        execute(equalCpu);
        const auto equalFlags = equalCpu.state().sr;
        execute(equalCpu);
        check(equal.byte(0x300) == 0 && equalCpu.state().sr == equalFlags,
              "SNE deve escrever zero quando Z está ativo");

        for (bool condition : {false, true})
        {
            Bus absoluteLong;
            absoluteLong.setup();
            const auto destination = std::uint32_t(condition ? 0x00fff3f9 : 0x00fff3f8);
            absoluteLong.putWord(0x100, condition ? 0x50f9 : 0x51f9); // ST/SF $00FFF3F8/9.L.
            absoluteLong.putWord(0x102, 0x00ff);
            absoluteLong.putWord(0x104, std::uint16_t(destination));
            absoluteLong.bytes[destination] = 0x5a;
            Moto68000 longCpu(absoluteLong);
            boot(longCpu);
            const auto longFlags = longCpu.state().sr;
            const auto before = absoluteLong.transfers.size();
            check(execute(longCpu) == 20, "Scc absoluto longo deve usar cinco acessos de barramento");
            check(absoluteLong.byte(destination) == (condition ? 0xff : 0) &&
                      longCpu.state().pc == 0x106 && longCpu.state().sr == longFlags &&
                      longCpu.state().prefetch[0] == 0x4e71,
                  "Scc absoluto longo deve escrever o byte e manter flags e pré-busca");
            check(std::vector<std::pair<char, std::uint32_t>>(
                      absoluteLong.transfers.begin() + before, absoluteLong.transfers.end()) ==
                      std::vector<std::pair<char, std::uint32_t>>({{'R', 0x104}, {'R', 0x106}, {'r', destination}, {'R', 0x108}, {'w', destination}}),
                  "Scc absoluto longo deve buscar as extensões e escrever na ordem correta");
        }
    }
}

int main()
{
    try
    {
        addressRegisters();
        memoryModes();
        stackPointerAndWaits();
        conditionByteStores();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}