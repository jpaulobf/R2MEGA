#include "Core/CPU/Moto68000/Moto68000.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace R2MEGA::Core;

namespace
{
    // Protocolo usado por m68000_matrix.py. Todos os inteiros de entrada têm 32 bits LE.
    bool readWord(std::uint32_t &value)
    {
        unsigned char bytes[4];
        const auto count = std::fread(bytes, 1, sizeof bytes, stdin);
        if (!count && std::feof(stdin))
            return false;
        if (count != sizeof bytes)
            throw std::runtime_error("truncated vector");
        value = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
                (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
        return true;
    }

    std::uint32_t readWord()
    {
        std::uint32_t value;
        if (!readWord(value))
            throw std::runtime_error("truncated vector");
        return value;
    }

    using Ram = std::unordered_map<std::uint32_t, std::uint8_t>;

    struct Snapshot
    {
        Moto68000::State cpu;
        std::uint32_t usp, ssp;
        Ram ram;
    };

    Snapshot readSnapshot()
    {
        Snapshot snapshot;
        for (auto &value : snapshot.cpu.data)
            value = readWord();
        for (unsigned reg = 0; reg < 7; ++reg)
            snapshot.cpu.address[reg] = readWord();
        snapshot.usp = readWord();
        snapshot.ssp = readWord();
        snapshot.cpu.sr = static_cast<std::uint16_t>(readWord());
        snapshot.cpu.pc = readWord();
        for (auto &word : snapshot.cpu.prefetch)
            word = static_cast<std::uint16_t>(readWord());
        for (auto count = readWord(); count; --count)
        {
            const auto address = readWord();
            snapshot.ram[address] = static_cast<std::uint8_t>(readWord());
        }
        return snapshot;
    }

    // idle, read, write, TAS, read address error, write address error.
    using Transaction = std::array<std::uint32_t, 6>;

    struct VectorBus : M68KBus
    {
        Ram ram;
        std::vector<Transaction> trace;
        unsigned transferCycles = 0;

        std::uint8_t readByte(std::uint32_t address) override
        {
            const auto found = ram.find(address);
            return found == ram.end() ? 0 : found->second;
        }
        std::uint16_t readWord(std::uint32_t address) override
        {
            return (std::uint16_t(readByte(address)) << 8) | readByte(address + 1);
        }
        void writeByte(std::uint32_t address, std::uint8_t value) override
        {
            ram[address] = value;
        }
        void writeWord(std::uint32_t address, std::uint16_t value) override
        {
            writeByte(address, static_cast<std::uint8_t>(value >> 8));
            writeByte(address + 1, static_cast<std::uint8_t>(value));
        }
        void observe(const M68KPins &pins) override
        {
            if (pins.addressError)
            {
                if (pins.phase == 3)
                    trace.push_back({pins.write ? 5u : 4u, 4, pins.functionCode, pins.address, 2, 0});
                return;
            }
            if (!pins.active)
            {
                if (!trace.empty() && trace.back()[0] == 0)
                    ++trace.back()[1];
                else
                    trace.push_back({0, 1, 0, 0, 0, 0});
                return;
            }
            ++transferCycles;
            if (pins.phase == 3)
            {
                const bool word = pins.upperDataStrobe && pins.lowerDataStrobe;
                const auto data = word ? pins.data : pins.upperDataStrobe ? pins.data >> 8
                                                                          : pins.data & 0xff;
                trace.push_back({pins.write ? 2u : 1u, transferCycles, pins.functionCode,
                                 pins.address, word ? 2u : 1u, static_cast<unsigned>(data)});
                transferCycles = 0;
            }
        }
    };

    unsigned compare(unsigned cycles, bool stopExpected, const Snapshot &initial,
                     const Snapshot &expected, const std::vector<Transaction> &transactions)
    {
        VectorBus bus;
        bus.ram = initial.ram;
        Moto68000 cpu(bus);
        cpu.restoreInstructionBoundary(initial.cpu, initial.usp, initial.ssp);
        try
        {
            const auto limit = std::max(256u, cycles + 32);
            do
                cpu.clock();
            while (!cpu.atInstructionBoundary() && !cpu.halted() && cpu.state().cycles < limit);
        }
        catch (const std::logic_error &)
        {
            return 64;
        }

        const auto &actual = cpu.state();
        unsigned differences = 0;
        if (actual.data != expected.cpu.data ||
            !std::equal(actual.address.begin(), actual.address.begin() + 7, expected.cpu.address.begin()) ||
            cpu.userStackPointer() != expected.usp || cpu.supervisorStackPointer() != expected.ssp)
            differences |= 1;
        if (actual.sr != expected.cpu.sr)
            differences |= 2;
        if (actual.pc != expected.cpu.pc || actual.prefetch != expected.cpu.prefetch)
            differences |= 4;
        for (const auto &[address, value] : expected.ram)
            if (bus.readByte(address) != value)
                differences |= 8;
        if (actual.cycles != cycles)
            differences |= 16;
        if (bus.trace != transactions)
            differences |= 32;
        if (!cpu.atInstructionBoundary())
            differences |= 64;
        if (cpu.stopped() != stopExpected)
            differences |= 128;
        return differences;
    }
}

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try
    {
        std::uint32_t cycles;
        while (readWord(cycles))
        {
            const bool stopExpected = readWord() != 0;
            const auto initial = readSnapshot();
            const auto expected = readSnapshot();
            std::vector<Transaction> trace(readWord());
            for (auto &transaction : trace)
                for (auto &value : transaction)
                    value = readWord();
            if (std::putchar(compare(cycles, stopExpected, initial, expected, trace)) == EOF)
                throw std::runtime_error("unable to write vector result");
        }
        return std::fflush(stdout) == 0 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}