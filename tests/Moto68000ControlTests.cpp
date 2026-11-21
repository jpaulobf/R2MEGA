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
        std::uint32_t waitAddress = 0xffffffff;
        unsigned readWait = 0, writeWait = 0;

        std::uint8_t readByte(std::uint32_t) override { throw std::logic_error("byte inesperado"); }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            return words.count(address) ? words.at(address) : 0x4e71;
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
        void writeByte(std::uint32_t, std::uint8_t) override { throw std::logic_error("byte inesperado"); }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            words[address] = value;
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
        void observe(const M68KPins &pins) override { trace.push_back(pins); }
    };

    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void run(Moto68000 &cpu, unsigned cycles)
    {
        while (cycles--)
            cpu.clock();
    }

    void setup(Bus &bus)
    {
        bus.words[0] = 0;
        bus.words[2] = 0x1000;
        bus.words[4] = 0;
        bus.words[6] = 0x0100;
        bus.words[12] = 0;
        bus.words[14] = 0x0300; // Erro de endereço.
        bus.words[0x100] = bus.words[0x102] = bus.words[0x104] = 0x4e71;
        bus.words[0x200] = bus.words[0x202] = 0x4e71;
        bus.words[0x300] = bus.words[0x302] = 0x4e71;
    }

    void checkReturn(const Bus &bus, std::uint16_t address)
    {
        check(bus.words.at(0xffc) == 0 && bus.words.at(0xffe) == address,
              "endereço de retorno na pilha");
        check(bus.writes == std::vector<std::uint32_t>({0xffe, 0xffc}),
              "ordem das duas escritas do retorno");
    }
}

int main()
{
    Bus byteCall;
    setup(byteCall);
    byteCall.words[0x100] = 0x6104; // BSR.B para $106.
    byteCall.words[0x102] = 0x7009; // MOVEQ #9,D0 após o retorno.
    byteCall.words[0x106] = 0x7207; // MOVEQ #7,D1 na subrotina.
    byteCall.words[0x108] = 0x4e75; // RTS.
    Moto68000 byteCpu(byteCall);
    byteCpu.reset();
    run(byteCpu, 40 + 18);
    check(byteCpu.state().pc == 0x106 && byteCpu.state().address[7] == 0xffc,
          "BSR.B em 18 ciclos");
    checkReturn(byteCall, 0x0102);
    run(byteCpu, 4 + 16);
    check(byteCpu.state().pc == 0x102 && byteCpu.state().address[7] == 0x1000 &&
              byteCpu.state().data[1] == 7,
          "RTS em 16 ciclos");
    run(byteCpu, 4);
    check(byteCpu.state().pc == 0x104 && byteCpu.state().data[0] == 9,
          "execução após retorno");

    Bus wordCall;
    setup(wordCall);
    wordCall.words[0x100] = 0x6100; // BSR.W com extensão de 16 bits.
    wordCall.words[0x102] = 0x0006;
    wordCall.words[0x108] = 0x4e75;
    Moto68000 wordCpu(wordCall);
    wordCpu.reset();
    run(wordCpu, 40 + 18);
    check(wordCpu.state().pc == 0x108, "BSR.W usa a extensão");
    checkReturn(wordCall, 0x0104);
    run(wordCpu, 16);
    check(wordCpu.state().pc == 0x104 && wordCpu.state().address[7] == 0x1000,
          "RTS de BSR.W volta após a extensão");

    Bus waited;
    setup(waited);
    waited.words[0x100] = 0x6104;
    waited.words[0x106] = 0x4e75;
    waited.waitAddress = 0xffe;
    waited.writeWait = 2;
    Moto68000 waitedCpu(waited);
    waitedCpu.reset();
    run(waitedCpu, 40 + 18 + 2);
    check(waitedCpu.state().pc == 0x106 && waited.writes.size() == 2,
          "espera prolonga BSR sem repetir escrita");
    waited.waitAddress = 0xffc;
    waited.readWait = 2;
    run(waitedCpu, 16 + 2);
    check(waitedCpu.state().pc == 0x102 && waitedCpu.state().address[7] == 0x1000,
          "espera prolonga RTS sem avançar a pilha");

    Bus registerCall;
    setup(registerCall);
    registerCall.words[0x100] = 0x41f8;
    registerCall.words[0x102] = 0x0200; // LEA $200.W,A0
    registerCall.words[0x104] = 0x4e90; // JSR (A0)
    registerCall.words[0x200] = 0x4e75;
    Moto68000 registerCpu(registerCall);
    registerCpu.reset();
    run(registerCpu, 40 + 8 + 16);
    check(registerCpu.state().pc == 0x200 && registerCpu.state().address[0] == 0x200,
          "JSR (An) em 16 ciclos");
    checkReturn(registerCall, 0x0106);
    run(registerCpu, 16);
    check(registerCpu.state().pc == 0x106, "RTS após JSR (An)");

    struct AbsoluteCase
    {
        std::uint16_t opcode, high, low, cycles, returnAddress;
    };
    for (const auto &test : {
             AbsoluteCase{0x4eb8, 0x0200, 0, 18, 0x0104}, // JSR abs.W
             AbsoluteCase{0x4eb9, 0, 0x0200, 20, 0x0106}, // JSR abs.L
             AbsoluteCase{0x4eba, 0x00fe, 0, 18, 0x0104}  // JSR (d16,PC)
         })
    {
        Bus bus;
        setup(bus);
        bus.words[0x100] = test.opcode;
        bus.words[0x102] = test.high;
        if (test.opcode == 0x4eb9)
            bus.words[0x104] = test.low;
        bus.words[0x200] = 0x4e75;
        Moto68000 cpu(bus);
        cpu.reset();
        run(cpu, 40 + test.cycles);
        check(cpu.state().pc == 0x200 && cpu.state().address[7] == 0xffc,
              "JSR absoluto/relativo no tempo previsto");
        checkReturn(bus, test.returnAddress);
        run(cpu, 16);
        check(cpu.state().pc == test.returnAddress,
              "RTS restaura endereço de retorno do JSR");
    }

    Bus displacementCall;
    setup(displacementCall);
    displacementCall.words[0x100] = 0x41f8;
    displacementCall.words[0x102] = 0x01f0;
    displacementCall.words[0x104] = 0x4ea8; // JSR (d16,A0)
    displacementCall.words[0x106] = 0x0010;
    displacementCall.words[0x200] = 0x4e75;
    Moto68000 displacementCpu(displacementCall);
    displacementCpu.reset();
    run(displacementCpu, 40 + 8 + 18);
    check(displacementCpu.state().pc == 0x200, "JSR com deslocamento de An");
    checkReturn(displacementCall, 0x0108);

    Bus indexedCall;
    setup(indexedCall);
    indexedCall.words[0x100] = 0x707f; // MOVEQ #$7F,D0.
    indexedCall.words[0x102] = 0x4ebb; // JSR (d8,PC,D0.W).
    indexedCall.words[0x104] = 0x007d; // $104 + $7F + $7D = $200.
    indexedCall.words[0x200] = 0x4e75;
    Moto68000 indexedCpu(indexedCall);
    indexedCpu.reset();
    run(indexedCpu, 40 + 4 + 22);
    check(indexedCpu.state().pc == 0x200 && indexedCpu.state().address[7] == 0xffc,
          "JSR indexado relativo ao PC em 22 ciclos");
    checkReturn(indexedCall, 0x0106);
    run(indexedCpu, 16);
    check(indexedCpu.state().pc == 0x106,
          "RTS restaura o PC após JSR indexado");

    Bus indexedJump;
    setup(indexedJump);
    indexedJump.words[0x100] = 0x7020; // MOVEQ #$20,D0.
    indexedJump.words[0x102] = 0x41f8; // LEA $01E0.W,A0.
    indexedJump.words[0x104] = 0x01e0;
    indexedJump.words[0x106] = 0x4ef0; // JMP (d8,A0,D0.W).
    indexedJump.words[0x108] = 0x0000;
    Moto68000 indexedJumpCpu(indexedJump);
    indexedJumpCpu.reset();
    run(indexedJumpCpu, 40 + 4 + 8 + 14);
    check(indexedJumpCpu.state().pc == 0x200 && indexedJumpCpu.state().address[7] == 0x1000 &&
              indexedJump.writes.empty(),
          "JMP indexado em 14 ciclos sem alterar a pilha");

    for (const auto &test : {
             AbsoluteCase{0x4ef8, 0x0200, 0, 10, 0}, // JMP abs.W
             AbsoluteCase{0x4ef9, 0, 0x0200, 12, 0}, // JMP abs.L
             AbsoluteCase{0x4efa, 0x00fe, 0, 10, 0}  // JMP (d16,PC)
         })
    {
        Bus bus;
        setup(bus);
        bus.words[0x100] = test.opcode;
        bus.words[0x102] = test.high;
        if (test.opcode == 0x4ef9)
            bus.words[0x104] = test.low;
        Moto68000 cpu(bus);
        cpu.reset();
        run(cpu, 40 + test.cycles);
        check(cpu.state().pc == 0x200 && cpu.state().address[7] == 0x1000 &&
                  bus.writes.empty(),
              "JMP muda PC sem usar a pilha");
    }

    Bus oddTarget;
    setup(oddTarget);
    oddTarget.words[0x100] = 0x6101; // BSR para endereço ímpar.
    Moto68000 oddCpu(oddTarget);
    oddCpu.reset();
    run(oddCpu, 40 + 2 + 8 + 50);
    check(oddCpu.state().pc == 0x300 && !oddCpu.halted() &&
              oddTarget.words.at(0xff4) == 0x6101 && oddTarget.words.at(0xff8) == 0x0103,
          "erro de endereço na busca da subrotina");
}