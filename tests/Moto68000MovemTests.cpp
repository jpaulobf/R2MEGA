#include "Core/CPU/Moto68000/Moto68000.h"
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint16_t> words;
        std::vector<std::uint32_t> reads, writes;

        void putLong(std::uint32_t address, std::uint32_t value)
        {
            words[address] = static_cast<std::uint16_t>(value >> 16);
            words[address + 2] = static_cast<std::uint16_t>(value);
        }
        void setup()
        {
            putLong(0, 0x1800);
            putLong(4, 0x100);
            for (unsigned address = 0x100; address < 0x130; address += 2)
                words[address] = 0x4e71;
        }
        std::uint8_t readByte(std::uint32_t) override
        {
            throw std::logic_error("Leitura de byte inesperada");
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            return words.at(address);
        }
        void writeByte(std::uint32_t, std::uint8_t) override
        {
            throw std::logic_error("Escrita de byte inesperada");
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            words[address] = value;
        }
    };

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    unsigned execute(Moto68000 &cpu)
    {
        const auto previous = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == previous && cycles < 200)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == previous + 1, "MOVEM não terminou");
        return cycles;
    }

    void boot(Moto68000 &cpu)
    {
        cpu.reset();
        for (unsigned i = 0; i < 40; ++i)
            cpu.clock();
        check(cpu.resetComplete(), "Reset incompleto");
    }
}

int main()
{
    Bus read;
    read.setup();
    read.words[0x100] = 0x4bf8; // LEA $1100.W,A5.
    read.words[0x102] = 0x1100;
    read.words[0x104] = 0x4c9d; // MOVEM.W (A5)+,D5-D7.
    read.words[0x106] = 0x00e0;
    read.words[0x108] = 0x4cdd; // MOVEM.L (A5)+,A0-A4.
    read.words[0x10a] = 0x1f00;
    read.words[0x1100] = 0x8001;
    read.words[0x1102] = 0x002a;
    read.words[0x1104] = 0xffff;
    for (unsigned reg = 0; reg < 5; ++reg)
        read.putLong(0x1106 + reg * 4, 0x12340000u + reg);
    read.words[0x111a] = 0x5678; // Leitura final, fora da lista de registradores.
    Moto68000 readCpu(read);
    boot(readCpu);
    check(execute(readCpu) == 8, "LEA inicial");
    read.reads.clear();
    check(execute(readCpu) == 24 &&
              readCpu.state().data[5] == 0xffff8001u &&
              readCpu.state().data[6] == 42 &&
              readCpu.state().data[7] == 0xffffffffu &&
              readCpu.state().address[5] == 0x1106 &&
              readCpu.state().sr == 0x2700 &&
              read.reads == std::vector<std::uint32_t>({0x108, 0x1100, 0x1102,
                                                        0x1104, 0x1106, 0x10a}),
          "MOVEM.W leu D5-D7, avançou A5 e fez a leitura final");
    read.reads.clear();
    check(execute(readCpu) == 52 && readCpu.state().address[5] == 0x111a &&
              readCpu.state().pc == 0x10c && readCpu.state().sr == 0x2700,
          "MOVEM.L leu cinco registradores em 52 ciclos");
    for (unsigned reg = 0; reg < 5; ++reg)
        check(readCpu.state().address[reg] == 0x12340000u + reg,
              "MOVEM.L carregou registrador de endereço incorreto");
    check(read.reads.front() == 0x10c &&
              read.reads[read.reads.size() - 2] == 0x111a &&
              read.reads.back() == 0x10e,
          "MOVEM.L preservou a ordem das leituras e da pré-busca");

    Bus write;
    write.setup();
    write.words[0x100] = 0x45f8; // LEA $1200.W,A2.
    write.words[0x102] = 0x1200;
    write.words[0x104] = 0x7012; // MOVEQ #$12,D0.
    write.words[0x106] = 0x48e2; // MOVEM.L D0/A2,-(A2).
    write.words[0x108] = 0x8020;
    Moto68000 writeCpu(write);
    boot(writeCpu);
    execute(writeCpu);
    execute(writeCpu);
    check(execute(writeCpu) == 24 &&
              writeCpu.state().address[2] == 0x11f8 &&
              write.words.at(0x11f8) == 0 && write.words.at(0x11fa) == 0x12 &&
              write.words.at(0x11fc) == 0 && write.words.at(0x11fe) == 0x1200 &&
              write.writes == std::vector<std::uint32_t>({0x11fe, 0x11fc, 0x11fa, 0x11f8}),
          "MOVEM.L pré-decrementa e grava a base original na ordem correta");
}