#include "Core/CPU/Moto68000/Moto68000.h"
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
            putWord(10, 0x300);
            putWord(12, 0);
            putWord(14, 0x200);
            for (unsigned address = 0x100; address < 0x130; address += 2)
                putWord(address, 0x4e71);
            putWord(0x200, 0x4e71);
            putWord(0x202, 0x4e71);
            putWord(0x300, 0x4e71);
            putWord(0x302, 0x4e71);
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
        for (unsigned cycle = 0; cycle < 40; ++cycle)
            cpu.clock();
        check(cpu.resetComplete(), "reset incompleto");
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
    void runUntilPc(Moto68000 &cpu, std::uint32_t target)
    {
        unsigned cycles = 0;
        while (cpu.state().pc != target && cycles++ < 100)
            cpu.clock();
        check(cpu.state().pc == target, "vetor de erro não foi carregado");
    }

    void registerCases()
    {
        Bus zero;
        zero.setup();
        zero.putWord(0x100, 0x7000); // MOVEQ #0,D0
        zero.putWord(0x102, 0x7200); // MOVEQ #0,D1
        zero.putWord(0x104, 0xd101); // ADDX.B D1,D0
        zero.putWord(0x106, 0x9101); // SUBX.B D1,D0
        zero.putWord(0x108, 0x7201); // MOVEQ #1,D1: limpa Z
        zero.putWord(0x10a, 0x9301); // SUBX.B D1,D1
        Moto68000 zeroCpu(zero);
        boot(zeroCpu);
        execute(zeroCpu);
        execute(zeroCpu);
        check(execute(zeroCpu) == 4 && (zeroCpu.state().sr & 0x1f) == 4,
              "ADDX.B conserva Z em resultado zero");
        check(execute(zeroCpu) == 4 && (zeroCpu.state().sr & 0x1f) == 4,
              "SUBX.B conserva Z em zero consecutivo");
        execute(zeroCpu);
        check(execute(zeroCpu) == 4 && zeroCpu.state().data[1] == 0 &&
                  (zeroCpu.state().sr & 0x1f) == 0,
              "SUBX.B com registrador repetido não restaura Z");

        Bus extend;
        extend.setup();
        extend.putWord(0x100, 0x7000); // MOVEQ #0,D0
        extend.putWord(0x102, 0x7200); // MOVEQ #0,D1
        extend.putWord(0x104, 0x74ff); // MOVEQ #-1,D2
        extend.putWord(0x106, 0x5202); // ADDQ.B #1,D2: X = 1
        extend.putWord(0x108, 0xd101); // ADDX.B D1,D0
        extend.putWord(0x10a, 0x9101); // SUBX.B D1,D0
        Moto68000 extendCpu(extend);
        boot(extendCpu);
        execute(extendCpu);
        execute(extendCpu);
        execute(extendCpu);
        execute(extendCpu);
        check((extendCpu.state().sr & 0x14) == 0x14, "preparação de X e Z");
        check(execute(extendCpu) == 4 && extendCpu.state().data[0] == 1 &&
                  (extendCpu.state().sr & 0x1f) == 0,
              "ADDX.B soma X e limpa Z após resultado não zero");
        check(execute(extendCpu) == 4 && extendCpu.state().data[0] == 1 &&
                  (extendCpu.state().sr & 0x1f) == 0,
              "SUBX.B observa o novo X");

        Bus borrow;
        borrow.setup();
        borrow.putWord(0x100, 0x7000);
        borrow.putWord(0x102, 0x7200);
        borrow.putWord(0x104, 0x74ff);
        borrow.putWord(0x106, 0x5202); // X = 1
        borrow.putWord(0x108, 0x9101); // SUBX.B D1,D0
        Moto68000 borrowCpu(borrow);
        boot(borrowCpu);
        execute(borrowCpu);
        execute(borrowCpu);
        execute(borrowCpu);
        execute(borrowCpu);
        check(execute(borrowCpu) == 4 && borrowCpu.state().data[0] == 0xff &&
                  (borrowCpu.state().sr & 0x1f) == 0x19,
              "SUBX.B subtrai X e gera empréstimo");

        Bus longWord;
        longWord.setup();
        longWord.putWord(0x100, 0x70ff); // MOVEQ #-1,D0
        longWord.putWord(0x102, 0x7201); // MOVEQ #1,D1
        longWord.putWord(0x104, 0xd141); // ADDX.W D1,D0
        longWord.putWord(0x106, 0xd181); // ADDX.L D1,D0
        longWord.putWord(0x108, 0x9181); // SUBX.L D1,D0
        Moto68000 longCpu(longWord);
        boot(longCpu);
        execute(longCpu);
        execute(longCpu);
        check(execute(longCpu) == 4 && longCpu.state().data[0] == 0xffff0000u &&
                  (longCpu.state().sr & 0x1f) == 0x11,
              "ADDX.W preserva metade alta e gera carry");
        check(execute(longCpu) == 8 && longCpu.state().data[0] == 0xffff0002u &&
                  (longCpu.state().sr & 0x1f) == 8,
              "ADDX.L usa X anterior e consome oito ciclos");
        check(execute(longCpu) == 8 && longCpu.state().data[0] == 0xffff0001u &&
                  (longCpu.state().sr & 0x1f) == 8,
              "SUBX.L consome oito ciclos");

        Bus overflow;
        overflow.setup();
        overflow.putWord(0x100, 0x707f); // MOVEQ #127,D0
        overflow.putWord(0x102, 0x7200); // MOVEQ #0,D1
        overflow.putWord(0x104, 0x74ff);
        overflow.putWord(0x106, 0x5202); // X = 1
        overflow.putWord(0x108, 0xd101); // ADDX.B D1,D0
        Moto68000 overflowCpu(overflow);
        boot(overflowCpu);
        execute(overflowCpu);
        execute(overflowCpu);
        execute(overflowCpu);
        execute(overflowCpu);
        check(execute(overflowCpu) == 4 && overflowCpu.state().data[0] == 0x80 &&
                  (overflowCpu.state().sr & 0x1f) == 0x0a,
              "ADDX.B considera X no overflow");

        Bus negativeOverflow;
        negativeOverflow.setup();
        negativeOverflow.putWord(0x100, 0x7080); // MOVEQ #-128,D0
        negativeOverflow.putWord(0x102, 0x7200);
        negativeOverflow.putWord(0x104, 0x74ff);
        negativeOverflow.putWord(0x106, 0x5202); // X = 1
        negativeOverflow.putWord(0x108, 0x9101); // SUBX.B D1,D0
        Moto68000 negativeCpu(negativeOverflow);
        boot(negativeCpu);
        execute(negativeCpu);
        execute(negativeCpu);
        execute(negativeCpu);
        execute(negativeCpu);
        check(execute(negativeCpu) == 4 && negativeCpu.state().data[0] == 0xffffff7fu &&
                  (negativeCpu.state().sr & 0x1f) == 0x02,
              "SUBX.B considera X no overflow");
    }

    void memoryCases()
    {
        Bus byte;
        byte.setup();
        byte.putWord(0x100, 0x41f8);
        byte.putWord(0x102, 0x1102);
        byte.putWord(0x104, 0x43f8);
        byte.putWord(0x106, 0x1202);
        byte.putWord(0x108, 0x7000); // Z = 1
        byte.putWord(0x10a, 0xd308); // ADDX.B -(A0),-(A1)
        byte.bytes[0x1101] = 0x7f;
        byte.bytes[0x1201] = 1;
        Moto68000 byteCpu(byte);
        boot(byteCpu);
        execute(byteCpu);
        execute(byteCpu);
        execute(byteCpu);
        byte.transfers.clear();
        check(execute(byteCpu) == 18 && byteCpu.state().address[0] == 0x1101 &&
                  byteCpu.state().address[1] == 0x1201 && byte.byte(0x1201) == 0x80 &&
                  (byteCpu.state().sr & 0x1f) == 0x0a,
              "ADDX.B memória para memória consome 18 ciclos");
        check(byte.transfers == std::vector<std::pair<char, std::uint32_t>>({{'r', 0x1101}, {'r', 0x1201}, {'R', 0x10e}, {'w', 0x1201}}),
              "ADDX.B lê origem, lê destino, pré-busca e escreve");

        Bus longBus;
        longBus.setup();
        longBus.putWord(0x100, 0x41f8);
        longBus.putWord(0x102, 0x1104);
        longBus.putWord(0x104, 0x43f8);
        longBus.putWord(0x106, 0x1204);
        longBus.putWord(0x108, 0x7000);
        longBus.putWord(0x10a, 0x9388); // SUBX.L -(A0),-(A1)
        longBus.putWord(0x1100, 0);
        longBus.putWord(0x1102, 1);
        longBus.putWord(0x1200, 0);
        longBus.putWord(0x1202, 0);
        Moto68000 longCpu(longBus);
        boot(longCpu);
        execute(longCpu);
        execute(longCpu);
        execute(longCpu);
        longBus.transfers.clear();
        check(execute(longCpu) == 30 && longCpu.state().address[0] == 0x1100 &&
                  longCpu.state().address[1] == 0x1200 &&
                  longBus.word(0x1200) == 0xffff && longBus.word(0x1202) == 0xffff &&
                  (longCpu.state().sr & 0x1f) == 0x19,
              "SUBX.L memória para memória consome 30 ciclos");
        check(longBus.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x1102}, {'R', 0x1200}, {'R', 0x1202}, {'R', 0x10e}, {'W', 0x1200}, {'W', 0x1202}}),
              "SUBX.L usa cinco leituras e duas escritas");

        Bus same;
        same.setup();
        same.putWord(0x100, 0x41f8);
        same.putWord(0x102, 0x1104);
        same.putWord(0x104, 0x7000);
        same.putWord(0x106, 0xd148); // ADDX.W -(A0),-(A0)
        same.putWord(0x1100, 2);
        same.putWord(0x1102, 1);
        Moto68000 sameCpu(same);
        boot(sameCpu);
        execute(sameCpu);
        execute(sameCpu);
        same.transfers.clear();
        check(execute(sameCpu) == 18 && sameCpu.state().address[0] == 0x1100 &&
                  same.word(0x1100) == 3 && same.word(0x1102) == 1 &&
                  same.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1102}, {'R', 0x1100}, {'R', 0x10a}, {'W', 0x1100}}),
              "ADDX.W com A0 repetido pré-decrementa duas vezes");

        Bus stack;
        stack.setup(0x1804);
        stack.putWord(0x100, 0x7000);
        stack.putWord(0x102, 0xdf0f); // ADDX.B -(A7),-(A7)
        stack.bytes[0x1802] = 1;
        stack.bytes[0x1800] = 2;
        Moto68000 stackCpu(stack);
        boot(stackCpu);
        execute(stackCpu);
        check(execute(stackCpu) == 18 && stackCpu.state().address[7] == 0x1800 &&
                  stack.byte(0x1800) == 3 && stack.byte(0x1802) == 1,
              "ADDX.B usa passo de dois bytes em A7");
    }

    void waitAndFaultCases()
    {
        Bus wait;
        wait.setup();
        wait.putWord(0x100, 0x41f8);
        wait.putWord(0x102, 0x1102);
        wait.putWord(0x104, 0x43f8);
        wait.putWord(0x106, 0x1202);
        wait.putWord(0x108, 0x7000);
        wait.putWord(0x10a, 0xd348); // ADDX.W -(A0),-(A1)
        wait.putWord(0x1100, 1);
        wait.putWord(0x1200, 2);
        Moto68000 waitCpu(wait);
        boot(waitCpu);
        execute(waitCpu);
        execute(waitCpu);
        execute(waitCpu);
        wait.transfers.clear();
        wait.waitAddress = 0x1200;
        wait.readWait = 2;
        wait.writeWait = 3;
        check(execute(waitCpu) == 23 && waitCpu.state().address[0] == 0x1100 &&
                  waitCpu.state().address[1] == 0x1200 && wait.word(0x1200) == 3 &&
                  wait.transfers == std::vector<std::pair<char, std::uint32_t>>({{'R', 0x1100}, {'R', 0x1200}, {'R', 0x10e}, {'W', 0x1200}}),
              "ADDX.W espera leitura e escrita sem repetir pré-decremento");

        Bus odd;
        odd.setup();
        odd.putWord(0x100, 0x41f8);
        odd.putWord(0x102, 0x1103);
        odd.putWord(0x104, 0x43f8);
        odd.putWord(0x106, 0x1202);
        odd.putWord(0x108, 0x9348); // SUBX.W -(A0),-(A1)
        Moto68000 oddCpu(odd);
        boot(oddCpu);
        execute(oddCpu);
        execute(oddCpu);
        runUntilPc(oddCpu, 0x200);
        check(oddCpu.state().instructions == 2 &&
                  oddCpu.state().address[0] == 0x1101 &&
                  oddCpu.state().address[1] == 0x1202 &&
                  odd.word(0x17f8) == 0x9348 && odd.word(0x17fc) == 0x1101 &&
                  odd.word(0x17fe) == 0x15,
              "SUBX.W desalinhado falha na leitura da origem");

        Bus writeFault;
        writeFault.setup();
        writeFault.putWord(0x100, 0x41f8);
        writeFault.putWord(0x102, 0x1102);
        writeFault.putWord(0x104, 0x43f8);
        writeFault.putWord(0x106, 0x1202);
        writeFault.putWord(0x108, 0xd348); // ADDX.W -(A0),-(A1)
        writeFault.putWord(0x1100, 1);
        writeFault.putWord(0x1200, 2);
        Moto68000 faultCpu(writeFault);
        boot(faultCpu);
        execute(faultCpu);
        execute(faultCpu);
        writeFault.transfers.clear();
        writeFault.faultAddress = 0x1200;
        writeFault.faultWrite = true;
        runUntilPc(faultCpu, 0x300);
        check(faultCpu.state().instructions == 2 &&
                  faultCpu.state().address[0] == 0x1100 &&
                  faultCpu.state().address[1] == 0x1200 &&
                  writeFault.word(0x1200) == 2 &&
                  writeFault.word(0x17f8) == 0xd348 &&
                  writeFault.word(0x17fc) == 0x1200 &&
                  writeFault.word(0x17fe) == 0x05 &&
                  writeFault.transfers.size() >= 3 &&
                  writeFault.transfers[0] == std::pair<char, std::uint32_t>('R', 0x1100) &&
                  writeFault.transfers[1] == std::pair<char, std::uint32_t>('R', 0x1200) &&
                  writeFault.transfers[2] == std::pair<char, std::uint32_t>('R', 0x10c),
              "ADDX.W com falha de escrita preserva o destino e o quadro de erro");
    }
}

int main()
{
    try
    {
        registerCases();
        memoryCases();
        waitAndFaultCases();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}