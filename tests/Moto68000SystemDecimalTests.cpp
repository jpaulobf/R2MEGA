#include "Core/CPU/Moto68000/Moto68000.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    struct Bus : M68KBus
    {
        std::unordered_map<std::uint32_t, std::uint8_t> ram;
        std::vector<std::uint32_t> reads, writes;
        std::vector<M68KPins> trace;
        unsigned waits = 0;

        std::uint8_t readByte(std::uint32_t address) override
        {
            reads.push_back(address);
            return ram[address];
        }
        std::uint16_t word(std::uint32_t address)
        {
            return (std::uint16_t(ram[address]) << 8) | ram[address + 1];
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            reads.push_back(address);
            return word(address);
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            writes.push_back(address);
            ram[address] = value;
        }
        bool tryWriteByte(std::uint32_t address, std::uint8_t value) override
        {
            if (waits)
            {
                --waits;
                return false;
            }
            writeByte(address, value);
            return true;
        }
        void setWord(std::uint32_t address, std::uint16_t value)
        {
            ram[address] = static_cast<std::uint8_t>(value >> 8);
            ram[address + 1] = static_cast<std::uint8_t>(value);
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writes.push_back(address);
            setWord(address, value);
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

    Moto68000::State initial(std::uint16_t opcode, std::uint16_t extension = 0x4e71)
    {
        Moto68000::State state;
        state.pc = 0x100;
        state.prefetch = {opcode, extension};
        return state;
    }

    void vector(Bus &bus, unsigned number)
    {
        bus.setWord(number * 4 + 2, 0x200);
        bus.setWord(0x200, 0x4e73); // RTE
        bus.setWord(0x202, 0x4e71);
    }

    void decimal()
    {
        Bus bus;
        Moto68000 cpu(bus);
        auto state = initial(0x4800); // NBCD D0
        state.data[0] = 0x12340003;
        state.sr = 0x2714;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 6);
        check(cpu.atInstructionBoundary() && cpu.state().data[0] == 0x12340096 &&
                  cpu.state().sr == 0x2719 && bus.reads == std::vector<std::uint32_t>{0x104},
              "NBCD subtrai byte e X e limpa Z para resultado não nulo");

        bus = {};
        state = initial(0x4830, 0x0001); // NBCD (1,A0,D0.W)
        state.address[0] = 0x400;
        state.sr = 0x2704;
        bus.ram[0x401] = 0;
        bus.waits = 2;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 20);
        check(cpu.atInstructionBoundary() && cpu.state().pc == 0x104 &&
                  cpu.state().sr == 0x2704 && bus.ram[0x401] == 0 &&
                  bus.reads == std::vector<std::uint32_t>{0x104, 0x401, 0x106} &&
                  bus.writes == std::vector<std::uint32_t>{0x401},
              "NBCD indexado preserva Z acumulado e escreve uma vez sob espera");
        check(!bus.trace[0].active && !bus.trace[1].active && bus.trace[2].active,
              "NBCD indexado calcula endereço antes da pré-busca");

        bus = {};
        state = initial(0x8300); // SBCD D0,D1
        state.data[0] = 0x09;
        state.data[1] = 0x11223310;
        state.sr = 0x2714;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 6);
        check(cpu.atInstructionBoundary() && cpu.state().data[1] == 0x11223300 &&
                  cpu.state().sr == 0x2704 && bus.trace[0].active && !bus.trace[4].active,
              "SBCD em registradores preserva Z e usa pré-busca antes dos ciclos internos");

        bus = {};
        state = initial(0x8f0f); // SBCD -(A7),-(A7)
        bus.ram[0xffe] = 0x24;
        bus.ram[0xffc] = 0x11;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 18);
        check(cpu.atInstructionBoundary() && cpu.state().address[7] == 0xffc &&
                  bus.ram[0xffc] == 0x87 && cpu.state().sr == 0x2719 &&
                  bus.reads == std::vector<std::uint32_t>{0xffe, 0xffc, 0x104} &&
                  bus.writes == std::vector<std::uint32_t>{0xffc},
              "SBCD pré-decrementa A7 duas vezes em passos de dois bytes");
    }

    void boundsAndOverflow()
    {
        Bus bus;
        Moto68000 cpu(bus);
        auto state = initial(0x41bc, 3); // CHK #3,D0
        state.data[0] = 0x98760003;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 14);
        check(cpu.atInstructionBoundary() && cpu.state().pc == 0x104 &&
                  cpu.state().data[0] == 0x98760003 && bus.writes.empty(),
              "CHK.W aceita valor igual ao limite e usa somente a palavra baixa");

        for (const auto value : {4u, 0xffffu})
        {
            bus = {};
            vector(bus, 6);
            state = initial(0x41bc, 3);
            state.data[0] = value;
            state.sr = 0x0010;
            cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
            run(cpu, value == 4 ? 42 : 44);
            check(cpu.atInstructionBoundary() && cpu.state().pc == 0x200 &&
                      cpu.userStackPointer() == 0x800 && bus.word(0xffc) == 0 &&
                      bus.word(0xffe) == 0x104 && bus.word(0xffa) == (value == 4 ? 0x10 : 0x18),
                  "CHK empilha o próximo PC e o sinal de Dn em cada caminho de exceção");
            check(bus.writes == std::vector<std::uint32_t>{0xffe, 0xffa, 0xffc},
                  "ordem de escrita do quadro curto de CHK");
        }

        bus = {};
        vector(bus, 3);
        state = initial(0x4198); // CHK (A0)+,D0
        state.address[0] = 0x401;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 58);
        check(cpu.atInstructionBoundary() && cpu.lastException().vector == 3 &&
                  cpu.state().address[0] == 0x403 && bus.word(0xffe) == 0x102,
              "CHK desalinhado gera quadro de endereço antes da comparação");
        check(std::all_of(bus.trace.begin(), bus.trace.begin() + 4,
                          [](const M68KPins &pins)
                          { return pins.addressError && !pins.addressStrobe && pins.address == 0x400; }) &&
                  bus.reads.front() == 12,
              "CHK desalinhado apresenta endereço ao barramento por quatro ciclos sem ler o operando");

        for (const auto sr : {0x2700, 0x0002})
        {
            bus = {};
            vector(bus, 7);
            state = initial(0x4e76); // TRAPV
            state.sr = sr;
            cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
            run(cpu, sr & 2 ? 34 : 4);
            check(cpu.atInstructionBoundary() && cpu.state().pc == (sr & 2 ? 0x200u : 0x102u) &&
                      bus.reads.front() == 0x104 && cpu.state().instructions == 1,
                  "TRAPV faz pré-busca e decide pelo bit V");
            if (sr & 2)
                check(bus.word(0xffa) == 2 && bus.word(0xffe) == 0x102 &&
                          bus.writes == std::vector<std::uint32_t>{0xffe, 0xffa, 0xffc} &&
                          bus.trace.front().functionCode == 6,
                      "TRAPV tomado faz pré-busca de supervisor e salva SR de usuário e PC seguinte");
        }
    }

    void resetAndStop()
    {
        Bus bus;
        Moto68000 cpu(bus);
        auto state = initial(0x4e70); // RESET
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 132);
        check(cpu.atInstructionBoundary() && cpu.state().pc == 0x102 &&
                  cpu.state().sr == state.sr && bus.reads == std::vector<std::uint32_t>{0x104},
              "RESET ocupa 132 ciclos e mantém o estado interno da CPU");
        check(!bus.trace[3].resetAsserted && !bus.trace[128].resetAsserted &&
                  std::count_if(bus.trace.begin(), bus.trace.end(),
                                [](const M68KPins &pins)
                                { return pins.resetAsserted; }) == 124,
              "RESET apresenta o sinal externo por 124 ciclos internos");

        for (const auto opcode : {0x4e70, 0x4e72})
        {
            bus = {};
            vector(bus, 8);
            state = initial(opcode);
            state.sr = 0;
            cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
            run(cpu, 34);
            check(cpu.atInstructionBoundary() && cpu.state().pc == 0x200 &&
                      bus.word(0xffa) == 0 && bus.word(0xffe) == 0x100 &&
                      bus.reads == std::vector<std::uint32_t>{32, 34, 0x200, 0x202} &&
                      !cpu.stopped() && std::none_of(bus.trace.begin(), bus.trace.end(), [](const M68KPins &pins)
                                                     { return pins.resetAsserted; }),
                  "RESET e STOP em modo usuário salvam o PC da instrução");
        }

        bus = {};
        vector(bus, 27);
        bus.setWord(0x104, 0x7009); // MOVEQ #9,D0
        bus.setWord(0x106, 0x4e71);
        state = initial(0x4e72, 0x5af5); // STOP ignora os bits reservados e entra em modo usuário com IPL = 2.
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 4);
        cpu.setInterruptLevel(2);
        run(cpu, 10);
        check(cpu.atInstructionBoundary() && cpu.stopped() && cpu.state().pc == 0x104 &&
                  cpu.state().sr == 0x0215 && cpu.state().address[7] == 0x800 && bus.reads.empty(),
              "STOP carrega SR em quatro ciclos e espera com IPL mascarado");
        cpu.setInterruptLevel(3);
        run(cpu, 44);
        check(!cpu.stopped() && cpu.state().pc == 0x200 &&
                  bus.word(0xffa) == 0x0215 && bus.word(0xffe) == 0x104,
              "interrupção acorda STOP e empilha o endereço posterior ao imediato");
        cpu.setInterruptLevel(0);
        run(cpu, 24);
        check(cpu.state().address[7] == 0x800 && cpu.state().data[0] == 9,
              "RTE retorna à instrução seguinte a STOP");

        bus = {};
        vector(bus, 9);
        state = initial(0x4e72, 0x2000);
        state.sr = 0xa700;
        cpu.restoreInstructionBoundary(state, 0x800, 0x1000);
        run(cpu, 4 + 34);
        check(cpu.state().pc == 0x200 && bus.word(0xffe) == 0x104,
              "trace amostrado na entrada acorda STOP");
    }
}

int main()
{
    try
    {
        decimal();
        boundsAndOverflow();
        resetAndStop();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}