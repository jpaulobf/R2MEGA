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
        std::uint32_t faultAddress = 0xffffffffu;
        bool faultWrite = false;
        unsigned readWait = 0, writeWait = 0;

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
            if (!faultWrite && address == faultAddress)
                throw M68KBusError("falha de leitura de byte");
            transfers.emplace_back('r', address);
            return byte(address);
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            if (!faultWrite && address == faultAddress)
                throw M68KBusError("falha de leitura de word");
            transfers.emplace_back('R', address);
            return word(address);
        }
        bool tryReadByte(std::uint32_t address, std::uint8_t &value) override
        {
            if (address == waitAddress && readWait)
            {
                --readWait;
                return false;
            }
            value = readByte(address);
            return true;
        }
        bool tryReadWord(std::uint32_t address, std::uint16_t &value) override
        {
            if (address == waitAddress && readWait)
            {
                --readWait;
                return false;
            }
            value = readWord(address);
            return true;
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            if (faultWrite && address == faultAddress)
                throw M68KBusError("falha de escrita de byte");
            transfers.emplace_back('w', address);
            bytes[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            if (faultWrite && address == faultAddress)
                throw M68KBusError("falha de escrita de word");
            transfers.emplace_back('W', address);
            putWord(address, value);
        }
        bool tryWriteByte(std::uint32_t address, std::uint8_t value) override
        {
            if (address == waitAddress && writeWait)
            {
                --writeWait;
                return false;
            }
            writeByte(address, value);
            return true;
        }
        bool tryWriteWord(std::uint32_t address, std::uint16_t value) override
        {
            if (address == waitAddress && writeWait)
            {
                --writeWait;
                return false;
            }
            writeWord(address, value);
            return true;
        }
        void setup(std::uint16_t stack = 0x1800)
        {
            putWord(0, 0);
            putWord(2, stack);
            putWord(4, 0);
            putWord(6, 0x100);
            putWord(8, 0);
            putWord(10, 0x300); // Vetor de erro de barramento.
            putWord(12, 0);
            putWord(14, 0x200); // Vetor de erro de endereço.
            putWord(16, 0);
            putWord(18, 0x400); // Vetor de instrução inválida.
            for (unsigned address = 0x100; address < 0x130; address += 2)
                putWord(address, 0x4e71);
            putWord(0x200, 0x4e71);
            putWord(0x202, 0x4e71);
            putWord(0x300, 0x4e71);
            putWord(0x302, 0x4e71);
            putWord(0x400, 0x4e71);
            putWord(0x402, 0x4e71);
        }
    };

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    unsigned execute(Moto68000 &cpu)
    {
        const auto before = cpu.state().instructions;
        unsigned cycles = 0;
        while (cpu.state().instructions == before && cycles < 200)
        {
            cpu.clock();
            ++cycles;
        }
        check(cpu.state().instructions == before + 1, "instrução não terminou");
        return cycles;
    }

    void boot(Moto68000 &cpu)
    {
        cpu.reset();
        for (unsigned cycle = 0; cycle < 40; ++cycle)
            cpu.clock();
        check(cpu.resetComplete(), "vetores de reset incompletos");
    }

    void runUntilPc(Moto68000 &cpu, std::uint32_t target)
    {
        unsigned cycles = 0;
        while (cpu.state().pc != target && cycles++ < 100)
            cpu.clock();
        check(cpu.state().pc == target, "tratador de exceção não foi carregado");
    }

    void checkCase(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void checkAddressingMatrix()
    {
        struct Addressing
        {
            const char *name;
            unsigned mode, reg, words, byteWordCycles, longCycles;
        };
        // Custos do endereço efetivo na tabela 8-1 do manual do MC68000.
        const std::array<Addressing, 10> addressing = {{{"(A0)", 2, 0, 0, 4, 8},
                                                        {"(A0)+", 3, 0, 0, 4, 8},
                                                        {"-(A0)", 4, 0, 0, 6, 10},
                                                        {"(4,A0)", 5, 0, 1, 8, 12},
                                                        {"(4,A0,D0.W)", 6, 0, 1, 10, 14},
                                                        {"$1100.W", 7, 0, 1, 8, 12},
                                                        {"$00001100.L", 7, 1, 2, 12, 16},
                                                        {"(24,PC)", 7, 2, 1, 8, 12},
                                                        {"(24,PC,D0.W)", 7, 3, 1, 10, 14},
                                                        {"#1", 7, 4, 1, 4, 8}}};
        const std::array<const char *, 3> names = {{"TST", "CLR", "CMP"}};

        for (unsigned kind = 0; kind < names.size(); ++kind)
        {
            for (unsigned sizeCode = 0; sizeCode < 3; ++sizeCode)
            {
                const unsigned size = sizeCode == 0 ? 1 : sizeCode == 1 ? 2
                                                                        : 4;
                for (const auto &ea : addressing)
                {
                    if (kind != 2 && ea.mode == 7 && ea.reg >= 2)
                        continue;
                    Bus bus;
                    bus.setup();
                    bus.putWord(0x100, 0x41f8);
                    bus.putWord(0x102, 0x1100);
                    bus.putWord(0x104, 0x7000); // D0 = 0, também para o índice.
                    const std::uint16_t base = kind == 0 ? 0x4a00 : kind == 1 ? 0x4200
                                                                              : 0xb000;
                    bus.putWord(0x106, base | (sizeCode << 6) | (ea.mode << 3) | ea.reg);
                    unsigned extensions = ea.words;
                    if (ea.mode == 5 || ea.mode == 6)
                        bus.putWord(0x108, 4);
                    else if (ea.mode == 7 && ea.reg == 0)
                        bus.putWord(0x108, 0x1100);
                    else if (ea.mode == 7 && ea.reg == 1)
                    {
                        bus.putWord(0x108, 0);
                        bus.putWord(0x10a, 0x1100);
                    }
                    else if (ea.mode == 7 && (ea.reg == 2 || ea.reg == 3))
                        bus.putWord(0x108, 0x18);
                    else if (ea.mode == 7 && ea.reg == 4)
                    {
                        if (size == 4)
                        {
                            bus.putWord(0x108, 0);
                            bus.putWord(0x10a, 1);
                            extensions = 2;
                        }
                        else
                            bus.putWord(0x108, 1);
                    }

                    std::uint32_t target = 0x1100;
                    if (ea.mode == 4)
                        target -= size;
                    else if (ea.mode == 5 || ea.mode == 6)
                        target += 4;
                    else if (ea.mode == 7 && (ea.reg == 2 || ea.reg == 3))
                        target = 0x120;
                    if (!(ea.mode == 7 && ea.reg == 4))
                    {
                        bus.putWord(target, kind == 2 ? (size == 1 ? 0x0100 : size == 2 ? 1
                                                                                        : 0)
                                                      : 0x8080);
                        if (size == 4)
                            bus.putWord(target + 2, 1);
                    }

                    Moto68000 cpu(bus);
                    boot(cpu);
                    checkCase(execute(cpu) == 8 && execute(cpu) == 4,
                              "preparação da matriz");
                    bus.transfers.clear();
                    const unsigned eaCycles = size == 4 ? ea.longCycles : ea.byteWordCycles;
                    const unsigned baseCycles = kind == 1 ? (size == 4 ? 12 : 8) : kind == 2 && size == 4 ? 6
                                                                                                          : 4;
                    const std::string label = std::string(names[kind]) + "." +
                                              (size == 1 ? "B " : size == 2 ? "W "
                                                                            : "L ") +
                                              ea.name;
                    checkCase(execute(cpu) == baseCycles + eaCycles,
                              label + ": ciclos");
                    checkCase(cpu.state().pc == 0x108 + extensions * 2 &&
                                  (cpu.state().sr & 0x0f) == (kind == 0 ? 8 : kind == 1 ? 4
                                                                                        : 9),
                              label + ": PC ou flags");
                    const int step = ea.mode == 4 ? -static_cast<int>(size) : ea.mode == 3 ? static_cast<int>(size)
                                                                                           : 0;
                    checkCase(cpu.state().address[0] == 0x1100 + step,
                              label + ": atualização de A0");
                    if (kind == 1)
                        checkCase(bus.byte(target) == 0 &&
                                      (size == 1 || bus.byte(target + 1) == 0) &&
                                      (size != 4 || bus.byte(target + 3) == 0),
                                  label + ": resultado na memória");
                    else
                        checkCase(cpu.state().data[0] == 0, label + ": D0 preservado");

                    std::vector<std::pair<char, std::uint32_t>> expected;
                    for (unsigned word = 0; word < extensions; ++word)
                        expected.emplace_back('R', 0x10a + word * 2);
                    if (!(ea.mode == 7 && ea.reg == 4))
                    {
                        expected.emplace_back(size == 1 ? 'r' : 'R', target);
                        if (size == 4)
                            expected.emplace_back('R', target + 2);
                    }
                    expected.emplace_back('R', 0x10a + extensions * 2);
                    if (kind == 1)
                    {
                        expected.emplace_back(size == 1 ? 'w' : 'W', target);
                        if (size == 4)
                            expected.emplace_back('W', target + 2);
                    }
                    checkCase(bus.transfers == expected, label + ": ordem do barramento");
                }
            }
        }
    }

    void checkCompareMemory()
    {
        struct Case
        {
            unsigned size, cycles, flags;
            std::uint32_t source, target;
        };
        constexpr std::array<Case, 4> cases{{{1, 12, 0x19, 1, 0},
                                             {2, 12, 0x12, 0x7fff, 0x8000},
                                             {4, 20, 0x11, 0xffffffffu, 0},
                                             {2, 12, 0x14, 0, 0}}};
        for (const auto &test : cases)
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, 0x41f8);
            bus.putWord(0x102, 0x1100); // LEA origem,A0.
            bus.putWord(0x104, 0x43f8);
            bus.putWord(0x106, 0x1200); // LEA destino,A1.
            bus.putWord(0x108, 0x70ff); // MOVEQ #-1,D0.
            bus.putWord(0x10a, 0x5200); // ADDQ.B #1,D0: X e Z ligados.
            const unsigned sizeCode = test.size == 1 ? 0 : test.size == 2 ? 1
                                                                          : 2;
            bus.putWord(0x10c, static_cast<std::uint16_t>(0xb308 | (sizeCode << 6)));
            if (test.size == 1)
            {
                bus.bytes[0x1100] = static_cast<std::uint8_t>(test.source);
                bus.bytes[0x1200] = static_cast<std::uint8_t>(test.target);
            }
            else
            {
                bus.putWord(0x1100, static_cast<std::uint16_t>(
                                        test.size == 4 ? test.source >> 16 : test.source));
                bus.putWord(0x1200, static_cast<std::uint16_t>(
                                        test.size == 4 ? test.target >> 16 : test.target));
                if (test.size == 4)
                {
                    bus.putWord(0x1102, static_cast<std::uint16_t>(test.source));
                    bus.putWord(0x1202, static_cast<std::uint16_t>(test.target));
                }
            }
            Moto68000 cpu(bus);
            boot(cpu);
            for (unsigned i = 0; i < 4; ++i)
                execute(cpu);
            check((cpu.state().sr & 0x1f) == 0x15, "preparo de X e Z para CMPM");
            bus.transfers.clear();
            const std::string label = "CMPM tamanho " + std::to_string(test.size);
            checkCase(execute(cpu) == test.cycles, label + ": ciclos");
            checkCase((cpu.state().sr & 0x1f) == test.flags,
                      label + ": flags obtidas " +
                          std::to_string(cpu.state().sr & 0x1f));
            checkCase(cpu.state().address[0] == 0x1100 + test.size &&
                          cpu.state().address[1] == 0x1200 + test.size,
                      label + ": pós-incremento");
            checkCase(cpu.state().data[0] == 0xffffff00u, label + ": D0 preservado");
            std::vector<std::pair<char, std::uint32_t>> expected;
            for (unsigned i = 0; i < (test.size == 4 ? 2u : 1u); ++i)
                expected.emplace_back(test.size == 1 ? 'r' : 'R', 0x1100 + i * 2);
            for (unsigned i = 0; i < (test.size == 4 ? 2u : 1u); ++i)
                expected.emplace_back(test.size == 1 ? 'r' : 'R', 0x1200 + i * 2);
            expected.emplace_back('R', 0x110);
            checkCase(bus.transfers == expected, label + ": ordem das leituras");
        }

        for (unsigned reg : {0u, 7u})
        {
            Bus bus;
            bus.setup();
            bus.putWord(0x100, reg == 7 ? 0x4ff8 : 0x41f8);
            bus.putWord(0x102, 0x1100);
            bus.putWord(0x104, static_cast<std::uint16_t>(
                                   0xb108 | (reg << 9) | reg)); // CMPM.B (An)+,(An)+.
            bus.bytes[0x1100] = 1;
            bus.bytes[0x1100 + (reg == 7 ? 2 : 1)] = 2;
            Moto68000 cpu(bus);
            boot(cpu);
            execute(cpu);
            bus.transfers.clear();
            check(execute(cpu) == 12, "CMPM.B com registrador repetido: ciclos");
            check(cpu.state().address[reg] == 0x1100 + (reg == 7 ? 4 : 2) &&
                      (cpu.state().sr & 0x1f) == 0,
                  "CMPM.B com registrador repetido: dois pós-incrementos");
            check(bus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1100}, {'r', 0x1100 + (reg == 7 ? 2u : 1u)}, {'R', 0x108}}),
                  "CMPM.B com registrador repetido: endereços das leituras");
        }

        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x1100);
        wait.putWord(0x104, 0x43f8);
        wait.putWord(0x106, 0x1200);
        wait.putWord(0x108, 0xb308); // CMPM.B (A0)+,(A1)+.
        wait.bytes[0x1100] = 1;
        wait.bytes[0x1200] = 2;
        Moto68000 waitCpu(wait);
        boot(waitCpu);
        execute(waitCpu);
        execute(waitCpu);
        wait.transfers.clear();
        wait.waitAddress = 0x1200;
        wait.readWait = 2;
        for (unsigned i = 0; i < 8; ++i)
            waitCpu.clock();
        check(waitCpu.state().address[0] == 0x1101 &&
                  waitCpu.state().address[1] == 0x1200 && waitCpu.pins().waiting,
              "CMPM conserva o destino durante espera da segunda leitura");
        check(execute(waitCpu) == 6 && waitCpu.state().address[1] == 0x1201 &&
                  wait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1100}, {'r', 0x1200}, {'R', 0x10c}}),
              "CMPM retoma a leitura sem duplicar acessos");

        Bus fault;
        fault.setup();
        fault.putWord(0x100, 0x41f8);
        fault.putWord(0x102, 0x1100);
        fault.putWord(0x104, 0x43f8);
        fault.putWord(0x106, 0x1200);
        fault.putWord(0x108, 0xb388); // CMPM.L (A0)+,(A1)+.
        fault.putWord(0x1100, 0);
        fault.putWord(0x1102, 1);
        fault.putWord(0x1200, 0);
        fault.putWord(0x1202, 2);
        Moto68000 faultCpu(fault);
        boot(faultCpu);
        execute(faultCpu);
        execute(faultCpu);
        fault.faultAddress = 0x1202;
        runUntilPc(faultCpu, 0x300);
        check(faultCpu.state().instructions == 2 &&
                  faultCpu.state().address[0] == 0x1104 &&
                  faultCpu.state().address[1] == 0x1200 &&
                  fault.word(0x17f8) == 0xb388 && fault.word(0x17fc) == 0x1202,
              "CMPM.L com falha na segunda leitura mantém o destino pendente");

        // A origem de CMPM avança antes da falha; o destino avança após a leitura.
        for (const bool oddSource : {true, false})
        {
            Bus odd;
            odd.setup();
            odd.putWord(0x100, 0x41f8);
            odd.putWord(0x102, oddSource ? 0x1101 : 0x1100);
            odd.putWord(0x104, 0x43f8);
            odd.putWord(0x106, oddSource ? 0x1200 : 0x1201);
            odd.putWord(0x108, 0xb348); // CMPM.W (A0)+,(A1)+.
            Moto68000 cpu(odd);
            boot(cpu);
            execute(cpu);
            execute(cpu);
            runUntilPc(cpu, 0x200);
            check(cpu.state().address[0] == (oddSource ? 0x1103u : 0x1102u) &&
                      cpu.state().address[1] == (oddSource ? 0x1200u : 0x1201u) &&
                      odd.word(0x17fe) == 0x10c && odd.word(0x17f8) == 0xb348 &&
                      odd.word(0x17f6) == (oddSource ? 0x1101 : 0x1201) &&
                      odd.word(0x17f2) == 0xb355,
                  "CMPM.W com endereço ímpar salvou estado incorreto");
        }

        Bus oddImmediate;
        oddImmediate.setup();
        oddImmediate.putWord(0x100, 0x41f8);
        oddImmediate.putWord(0x102, 0x1105);
        oddImmediate.putWord(0x104, 0x0ca0); // CMPI.L #1,-(A0).
        oddImmediate.putWord(0x106, 0);
        oddImmediate.putWord(0x108, 1);
        Moto68000 oddImmediateCpu(oddImmediate);
        boot(oddImmediateCpu);
        execute(oddImmediateCpu);
        runUntilPc(oddImmediateCpu, 0x200);
        check(oddImmediateCpu.state().address[0] == 0x1101 &&
                  oddImmediate.word(0x17fe) == 0x10a &&
                  oddImmediate.word(0x17f8) == 0x0ca0 &&
                  oddImmediate.word(0x17f6) == 0x1101,
              "CMPI.L desalinhado salvou PC ou endereço incorreto");
    }
}

int main()
{
    try
    {
        checkAddressingMatrix();
        checkCompareMemory();
        // TST usa o tamanho do operando e preserva X e os dados.
        Bus test;
        test.setup();
        test.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        test.putWord(0x102, 0x4a00); // TST.B D0
        test.putWord(0x104, 0x4a80); // TST.L D0
        Moto68000 testCpu(test);
        boot(testCpu);
        check(execute(testCpu) == 4, "MOVEQ inicial");
        check(execute(testCpu) == 4 && testCpu.state().data[0] == 0xffffffffu &&
                  (testCpu.state().sr & 0x0f) == 0x08,
              "TST.B em registrador");
        check(execute(testCpu) == 4 && (testCpu.state().sr & 0x0f) == 0x08,
              "TST.L em registrador");

        Bus extend;
        extend.setup();
        extend.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        extend.putWord(0x102, 0x5200); // ADDQ.B #1,D0: gera X
        extend.putWord(0x104, 0x4200); // CLR.B D0
        extend.putWord(0x106, 0x4a00); // TST.B D0
        extend.putWord(0x108, 0xb000); // CMP.B D0,D0
        Moto68000 extendCpu(extend);
        boot(extendCpu);
        execute(extendCpu);
        execute(extendCpu);
        check((extendCpu.state().sr & 0x10) != 0, "ADDQ inicial deve gerar X");
        check(execute(extendCpu) == 4 && extendCpu.state().data[0] == 0xffffff00u &&
                  (extendCpu.state().sr & 0x1f) == 0x14,
              "CLR.B preserva X");
        check(execute(extendCpu) == 4 && (extendCpu.state().sr & 0x1f) == 0x14,
              "TST.B preserva X");
        check(execute(extendCpu) == 4 && (extendCpu.state().sr & 0x1f) == 0x14,
              "CMP.B preserva X");

        Bus registers;
        registers.setup();
        registers.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        registers.putWord(0x102, 0x4280); // CLR.L D0
        registers.putWord(0x104, 0x4a40); // TST.W D0
        registers.putWord(0x106, 0xb080); // CMP.L D0,D0
        Moto68000 registersCpu(registers);
        boot(registersCpu);
        execute(registersCpu);
        check(execute(registersCpu) == 6 && registersCpu.state().data[0] == 0 &&
                  (registersCpu.state().sr & 0x0f) == 4,
              "CLR.L em registrador consome seis ciclos");
        check(execute(registersCpu) == 4 && (registersCpu.state().sr & 0x0f) == 4,
              "TST.W em registrador consome quatro ciclos");
        check(execute(registersCpu) == 6 && (registersCpu.state().sr & 0x0f) == 4,
              "CMP.L entre registradores consome seis ciclos");

        // CLR do MC68000 lê a memória antes da escrita, inclusive em long.
        Bus clear;
        clear.setup();
        clear.putWord(0x100, 0x41f8);
        clear.putWord(0x102, 0x1100); // LEA $1100.W,A0
        clear.putWord(0x104, 0x4290); // CLR.L (A0)
        clear.putWord(0x1100, 0x1234);
        clear.putWord(0x1102, 0x5678);
        Moto68000 clearCpu(clear);
        boot(clearCpu);
        check(execute(clearCpu) == 8, "LEA inicial");
        clear.transfers.clear();
        check(execute(clearCpu) == 20 && clear.byte(0x1100) == 0 &&
                  clear.byte(0x1103) == 0 && (clearCpu.state().sr & 0x0f) == 0x04,
              "CLR.L em memória");
        check(clear.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x1102}, {'R', 0x108}, {'W', 0x1100}, {'W', 0x1102}}),
              "CLR.L deve ler, pré-buscar e então escrever");

        Bus stack;
        stack.setup(0x1802);
        stack.putWord(0x100, 0x4227); // CLR.B -(A7)
        stack.bytes[0x1800] = 0xab;
        stack.waitAddress = 0x1800;
        stack.writeWait = 2;
        Moto68000 stackCpu(stack);
        boot(stackCpu);
        check(execute(stackCpu) == 16 && stackCpu.state().address[7] == 0x1800 &&
                  stack.byte(0x1800) == 0 && stack.transfers.back() == std::pair<char, std::uint32_t>('w', 0x1800),
              "CLR.B pré-decrementa A7 uma vez e espera pela escrita");

        // CMP calcula Dn - origem, sem modificar Dn, e sinaliza empréstimo.
        Bus compare;
        compare.setup();
        compare.putWord(0x100, 0x7000); // MOVEQ #0,D0
        compare.putWord(0x102, 0xb03c); // CMP.B #1,D0
        compare.putWord(0x104, 1);
        Moto68000 compareCpu(compare);
        boot(compareCpu);
        execute(compareCpu);
        check(execute(compareCpu) == 8 && compareCpu.state().data[0] == 0 &&
                  (compareCpu.state().sr & 0x0f) == 0x09 && compareCpu.state().pc == 0x106,
              "CMP.B imediato indica negativo e empréstimo");

        Bus overflow;
        overflow.setup();
        overflow.putWord(0x100, 0x7080); // MOVEQ #-128,D0
        overflow.putWord(0x102, 0xb03c); // CMP.B #1,D0
        overflow.putWord(0x104, 1);
        Moto68000 overflowCpu(overflow);
        boot(overflowCpu);
        execute(overflowCpu);
        check(execute(overflowCpu) == 8 && (overflowCpu.state().sr & 0x0f) == 0x02,
              "CMP.B distingue overflow de empréstimo");

        // CMPA.W estende o sinal da origem e compara os 32 bits de An.
        Bus address;
        address.setup();
        address.putWord(0x100, 0x41f8);
        address.putWord(0x102, 0xffff); // LEA -1.W,A0
        address.putWord(0x104, 0xb0fc);
        address.putWord(0x106, 0xffff); // CMPA.W #-1,A0
        Moto68000 addressCpu(address);
        boot(addressCpu);
        execute(addressCpu);
        check(execute(addressCpu) == 10 && addressCpu.state().address[0] == 0xffffffffu &&
                  (addressCpu.state().sr & 0x0f) == 0x04,
              "CMPA.W estende o sinal da origem");

        Bus addressModes;
        addressModes.setup();
        addressModes.putWord(0x100, 0x41f8);
        addressModes.putWord(0x102, 0xffff);
        addressModes.putWord(0x104, 0x43f8);
        addressModes.putWord(0x106, 0);
        addressModes.putWord(0x108, 0xb2c8); // CMPA.W A0,A1
        addressModes.putWord(0x10a, 0xb3fc); // CMPA.L #$80000000,A1
        addressModes.putWord(0x10c, 0x8000);
        addressModes.putWord(0x10e, 0);
        addressModes.putWord(0x110, 0xb3c8); // CMPA.L A0,A1.
        Moto68000 addressModesCpu(addressModes);
        boot(addressModesCpu);
        execute(addressModesCpu);
        execute(addressModesCpu);
        check(execute(addressModesCpu) == 6 && addressModesCpu.state().address[1] == 0 &&
                  (addressModesCpu.state().sr & 0x0f) == 1,
              "CMPA.W com origem An usa comparação de 32 bits");
        check(execute(addressModesCpu) == 14 && addressModesCpu.state().address[1] == 0 &&
                  (addressModesCpu.state().sr & 0x0f) == 0x0b,
              "CMPA.L imediato sinaliza overflow e empréstimo");
        const auto addressLongCycles = execute(addressModesCpu);
        if (addressLongCycles != 6)
            throw std::runtime_error("CMPA.L com origem An: ciclos=" +
                                     std::to_string(addressLongCycles) +
                                     " PC=" + std::to_string(addressModesCpu.state().pc));
        check(addressModesCpu.state().pc == 0x112,
              "CMPA.L com origem An avança o PC");
        check(addressModesCpu.state().address[1] == 0,
              "CMPA.L com origem An preserva o registrador");
        check((addressModesCpu.state().sr & 0x0f) == 0x01,
              "CMPA.L com origem An calcula os indicadores");

        Bus indexed;
        indexed.setup();
        indexed.putWord(0x100, 0x7000); // MOVEQ #0,D0
        indexed.putWord(0x102, 0xb0bb); // CMP.L (32,PC,D0.W),D0
        indexed.putWord(0x104, 0x0020);
        indexed.putWord(0x124, 0);
        indexed.putWord(0x126, 1);
        Moto68000 indexedCpu(indexed);
        boot(indexedCpu);
        execute(indexedCpu);
        indexed.transfers.clear();
        check(execute(indexedCpu) == 20 && indexedCpu.state().data[0] == 0 &&
                  (indexedCpu.state().sr & 0x0f) == 0x09,
              "CMP.L com índice relativo ao PC");
        check(indexed.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x106}, {'R', 0x124}, {'R', 0x126}, {'R', 0x108}}),
              "CMP.L busca extensão e lê o espaço de programa antes da próxima palavra");

        // Esperas DTACK não repetem o pós-incremento nem a leitura concluída.
        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x1100);
        wait.putWord(0x104, 0x4a18); // TST.B (A0)+
        wait.bytes[0x1100] = 0x80;
        wait.waitAddress = 0x1100;
        wait.readWait = 3;
        Moto68000 waitCpu(wait);
        boot(waitCpu);
        execute(waitCpu);
        check(execute(waitCpu) == 11 && waitCpu.state().address[0] == 0x1101 &&
                  (waitCpu.state().sr & 0x0f) == 0x08,
              "TST.B espera DTACK e incrementa A0 uma vez");

        Bus longWait;
        longWait.setup();
        longWait.putWord(0x100, 0x41f8);
        longWait.putWord(0x102, 0x1100);
        longWait.putWord(0x104, 0x7000); // MOVEQ #0,D0
        longWait.putWord(0x106, 0xb090); // CMP.L (A0),D0
        longWait.putWord(0x1100, 0);
        longWait.putWord(0x1102, 1);
        Moto68000 longWaitCpu(longWait);
        boot(longWaitCpu);
        execute(longWaitCpu);
        execute(longWaitCpu);
        longWait.transfers.clear();
        longWait.waitAddress = 0x1102;
        longWait.readWait = 3;
        check(execute(longWaitCpu) == 17 &&
                  (longWaitCpu.state().sr & 0x0f) == 0x09 &&
                  longWait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x1102}, {'R', 0x10a}}),
              "CMP.L espera na segunda leitura sem repetir a primeira");

        Bus prefetchWait;
        prefetchWait.setup();
        prefetchWait.putWord(0x100, 0x41f8);
        prefetchWait.putWord(0x102, 0x1100);
        prefetchWait.putWord(0x104, 0x4a50); // TST.W (A0)
        prefetchWait.putWord(0x1100, 0x8000);
        Moto68000 prefetchWaitCpu(prefetchWait);
        boot(prefetchWaitCpu);
        execute(prefetchWaitCpu);
        prefetchWait.transfers.clear();
        prefetchWait.waitAddress = 0x108;
        prefetchWait.readWait = 2;
        for (unsigned cycle = 0; cycle < 7; ++cycle)
            prefetchWaitCpu.clock();
        check(prefetchWaitCpu.pins().waiting && prefetchWaitCpu.state().pc == 0x104 &&
                  (prefetchWaitCpu.state().sr & 0x0f) == 0,
              "TST.W mantém flags pendentes durante espera na pré-busca");
        check(execute(prefetchWaitCpu) == 3 &&
                  (prefetchWaitCpu.state().sr & 0x0f) == 8 &&
                  prefetchWait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x108}}),
              "TST.W conclui após a pré-busca sem repetir o operando");

        Bus odd;
        odd.setup();
        odd.putWord(0x100, 0x41f8);
        odd.putWord(0x102, 0x1101);
        odd.putWord(0x104, 0x4a50); // TST.W (A0) ímpar
        Moto68000 oddCpu(odd);
        boot(oddCpu);
        execute(oddCpu);
        odd.transfers.clear();
        runUntilPc(oddCpu, 0x200);
        check(oddCpu.state().instructions == 1 && oddCpu.state().address[7] == 0x17f2 &&
                  odd.word(0x17f8) == 0x4a50 && odd.word(0x17f6) == 0x1101 &&
                  odd.word(0x17fe) == 0x106 && odd.word(0x17f2) == 0x4a55,
              "TST.W desalinhado gera quadro de erro de endereço");
        for (const auto &transfer : odd.transfers)
            check(transfer.second != 0x1101,
                  "acesso desalinhado de TST.W não deve chegar ao barramento");

        Bus readFault;
        readFault.setup();
        readFault.putWord(0x100, 0x41f8);
        readFault.putWord(0x102, 0x1100);
        readFault.putWord(0x104, 0x4290); // CLR.L (A0)
        readFault.putWord(0x1100, 0x1234);
        readFault.putWord(0x1102, 0x5678);
        Moto68000 readFaultCpu(readFault);
        boot(readFaultCpu);
        execute(readFaultCpu);
        readFault.faultAddress = 0x1102;
        runUntilPc(readFaultCpu, 0x300);
        check(readFaultCpu.state().instructions == 1 &&
                  readFault.word(0x1100) == 0x1234 && readFault.word(0x1102) == 0x5678 &&
                  readFault.word(0x17f8) == 0x4290 &&
                  readFault.word(0x17fc) == 0x1102 && readFault.word(0x17fe) == 0x15,
              "CLR.L com erro na segunda leitura preserva a memória");

        Bus writeFault;
        writeFault.setup();
        writeFault.putWord(0x100, 0x41f8);
        writeFault.putWord(0x102, 0x1100);
        writeFault.putWord(0x104, 0x4250); // CLR.W (A0)
        writeFault.putWord(0x1100, 0x1234);
        Moto68000 writeFaultCpu(writeFault);
        boot(writeFaultCpu);
        execute(writeFaultCpu);
        writeFault.transfers.clear();
        writeFault.faultAddress = 0x1100;
        writeFault.faultWrite = true;
        runUntilPc(writeFaultCpu, 0x300);
        check(writeFaultCpu.state().instructions == 1 &&
                  writeFault.word(0x1100) == 0x1234 &&
                  writeFault.word(0x17f8) == 0x4250 &&
                  writeFault.word(0x17fc) == 0x1100 && writeFault.word(0x17fe) == 0x05 &&
                  writeFault.transfers.size() >= 2 &&
                  writeFault.transfers[0] == std::pair<char, std::uint32_t>('R', 0x1100) &&
                  writeFault.transfers[1] == std::pair<char, std::uint32_t>('R', 0x108),
              "CLR.W com erro de escrita lê e pré-busca antes da exceção");

        // Modos inválidos no MC68000 entram pelo vetor 4, sem executar o operando.
        for (const std::uint16_t opcode : std::array<std::uint16_t, 5>{
                 0x427a, // CLR.W (d16,PC)
                 0x4248, // CLR.W A0
                 0x4a7a, // TST.W (d16,PC)
                 0x4a7c, // TST.W #<data>
                 0xb008  // CMP.B A0,D0
             })
        {
            Bus illegal;
            illegal.setup();
            illegal.putWord(0x100, opcode);
            Moto68000 cpu(illegal);
            boot(cpu);
            runUntilPc(cpu, 0x400);
            check(cpu.state().instructions == 0 && cpu.state().address[7] == 0x17fa &&
                      illegal.word(0x17fa) == 0x2700 &&
                      illegal.word(0x17fe) == 0x100,
                  "modo inválido deve empilhar o PC da instrução e usar vetor 4");
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}