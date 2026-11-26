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
        std::vector<M68KPins> trace;

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
        void observe(const M68KPins &pins) override { trace.push_back(pins); }
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

    void pcRelativeReads()
    {
        // d16(PC) e d8(PC,D1.W), ambos com base no endereço da extensão.
        for (bool indexed : {false, true})
            for (bool longOperand : {false, true})
                for (bool supervisor : {false, true})
                {
                    const auto opcode = static_cast<std::uint16_t>(
                        0x4cba | (indexed ? 1 : 0) | (longOperand ? 0x40 : 0));
                    const unsigned fc = supervisor ? 6 : 2;
                    Moto68000::State initial{};
                    initial.pc = 0x100;
                    initial.sr = supervisor ? 0x2719 : 0x0019;
                    initial.data[0] = 0xdeadbeef;
                    initial.data[1] = 2;
                    initial.prefetch = {opcode, 1}; // Máscara D0.

                    Bus bus;
                    bus.setup();
                    bus.words[0x104] = indexed ? 0x103a : 0x003c;
                    bus.putLong(0x140, 0x81234567);
                    bus.words[0x144] = 0xabcd;
                    Moto68000 cpu(bus);
                    cpu.restoreInstructionBoundary(initial, 0x1600, 0x1800);
                    check(execute(cpu) == 20u + (longOperand ? 4 : 0) + (indexed ? 2 : 0) &&
                              cpu.state().data[0] == (longOperand ? 0x81234567u : 0xffff8123u),
                          "MOVEM relativo ao PC carrega D0 a partir da extensão");
                    std::vector<std::uint32_t> reads;
                    for (const auto &pins : bus.trace)
                        if (pins.active && pins.phase == 3 && pins.address >= 0x140)
                        {
                            check(!pins.write && pins.functionCode == fc,
                                  "MOVEM relativo ao PC usa FC de programa, inclusive na leitura adicional");
                            reads.push_back(pins.address);
                        }
                    const auto expected = longOperand ? std::vector<std::uint32_t>{0x140, 0x142, 0x144}
                                                      : std::vector<std::uint32_t>{0x140, 0x142};
                    check(reads == expected, "MOVEM faz todas as leituras relativas ao PC");

                    Bus odd;
                    odd.setup();
                    odd.words[0x104] = indexed ? 0x103b : 0x003d;
                    odd.putLong(12, 0x200);
                    odd.words[0x200] = odd.words[0x202] = 0x4e71;
                    Moto68000 oddCpu(odd);
                    oddCpu.restoreInstructionBoundary(initial, 0x1600, 0x1800);
                    const unsigned beforeFault = indexed ? 10 : 8;
                    for (unsigned cycle = 0; cycle < beforeFault + 58; ++cycle)
                        oddCpu.clock();
                    check(oddCpu.atInstructionBoundary() && oddCpu.state().pc == 0x200 &&
                              oddCpu.lastException().vector == 3 && oddCpu.lastException().address == 0x141 &&
                              oddCpu.state().data[0] == initial.data[0] &&
                              oddCpu.supervisorStackPointer() == 0x17f2 && oddCpu.userStackPointer() == 0x1600,
                          "MOVEM relativo ao PC aborta no vetor 3 antes de carregar D0");
                    check(odd.words.at(0x17f2) == ((opcode & 0xffe0) | 0x10 | fc) &&
                              odd.words.at(0x17f4) == 0 && odd.words.at(0x17f6) == 0x141 &&
                              odd.words.at(0x17f8) == opcode && odd.words.at(0x17fa) == initial.sr &&
                              odd.words.at(0x17fc) == 0 && odd.words.at(0x17fe) == 0x108,
                          "MOVEM relativo ao PC empilha endereço ímpar, FC de programa e PC após a pré-busca");
                    check(odd.reads == std::vector<std::uint32_t>({0x104, 0x106, 12, 14, 0x200, 0x202}),
                          "A tentativa de leitura ímpar não acessa a memória");
                    for (unsigned phase = 0; phase < 4; ++phase)
                    {
                        const auto &pins = odd.trace.at(beforeFault + phase);
                        check(pins.addressError && !pins.addressStrobe && !pins.write &&
                                  !pins.upperDataStrobe && !pins.lowerDataStrobe &&
                                  pins.address == 0x140 && pins.phase == phase && pins.functionCode == fc,
                              "MOVEM apresenta o endereço ímpar por quatro ciclos com AS desativado");
                    }
                }
    }
}

int main()
{
    pcRelativeReads();
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

    Bus indexed;
    indexed.setup();
    indexed.words[0x104] = 0x1002;
    indexed.putLong(0x1104, 0x12345678);
    indexed.words[0x1108] = 0xabcd;
    Moto68000 indexedCpu(indexed);
    Moto68000::State initial{};
    initial.pc = 0x100;
    initial.sr = 0x2700;
    initial.address[0] = 0x1100;
    initial.data[1] = 2;
    initial.prefetch = {0x4cf0, 1}; // MOVEM.L (2,A0,D1.W),D0.
    indexedCpu.restoreInstructionBoundary(initial, 0, 0x1800);
    check(execute(indexedCpu) == 26 && indexedCpu.state().data[0] == 0x12345678 &&
              indexed.reads == std::vector<std::uint32_t>({0x104, 0x106, 0x1104, 0x1106, 0x1108, 0x108}),
          "MOVEM.L indexado carrega a extensão e faz a leitura adicional");
    check(indexed.trace[3].active && indexed.trace[3].address == 0x104 &&
              !indexed.trace[4].active && !indexed.trace[5].active &&
              indexed.trace[6].active && indexed.trace[6].address == 0x106,
          "MOVEM calcula o índice entre a extensão e a próxima palavra de instrução");

    Bus odd;
    odd.setup();
    odd.putLong(12, 0x200);
    odd.words[0x200] = odd.words[0x202] = 0x4e71;
    Moto68000 oddCpu(odd);
    initial.address[0] = 0x1105;
    initial.prefetch = {0x48e0, 0x8000}; // MOVEM.L D0,-(A0).
    oddCpu.restoreInstructionBoundary(initial, 0, 0x1800);
    for (unsigned cycle = 0; cycle < 62; ++cycle)
        oddCpu.clock();
    check(oddCpu.atInstructionBoundary() && oddCpu.state().pc == 0x200 &&
              oddCpu.state().instructions == 0 && oddCpu.state().address[0] == 0x1105 &&
              odd.words.at(0x17f6) == 0x1103 && odd.words.at(0x17fe) == 0x106,
          "MOVEM.L com destino ímpar empilha a palavra baixa e preserva a base");
    for (unsigned phase = 0; phase < 4; ++phase)
    {
        const auto &pins = odd.trace.at(4 + phase);
        check(pins.addressError && pins.write && !pins.addressStrobe &&
                  pins.address == 0x1102 && pins.phase == phase,
              "MOVEM apresenta a tentativa de escrita ímpar sem ativar AS");
    }

    Bus empty;
    empty.setup();
    Moto68000 emptyCpu(empty);
    initial.prefetch = {0x4890, 0}; // MOVEM.W com máscara vazia para (A0).
    emptyCpu.restoreInstructionBoundary(initial, 0, 0x1800);
    check(execute(emptyCpu) == 8 && empty.writes.empty() &&
              empty.reads == std::vector<std::uint32_t>({0x104, 0x106}),
          "MOVEM com máscara vazia consome apenas os ciclos de pré-busca");
}