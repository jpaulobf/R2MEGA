#pragma once

#include "Core/Audio/SN76489/SN76489.h"
#include "Core/Audio/YM2612/YM2612.h"
#include "Core/Bus/M68KBus.h"
#include "Core/Bus/Z80Bus.h"
#include "Core/CPU/Moto68000/Moto68000.h"
#include "Core/CPU/Z80/Z80.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/IO/ControllerPorts.h"
#include "Core/VDP/VDP.h"

namespace R2MEGA::Core
{
    // Owns the components and will coordinate them on a common time base.
    // Bus mappings, clock ratios, arbitration and DMA remain to be implemented.
    class Board final : public M68KBus, public Z80Bus
    {
    public:
        Board();
        Board(const Board &) = delete;
        Board &operator=(const Board &) = delete;

        // Advance the machine by one master-clock tick (not implemented yet).
        void clock();

        std::uint8_t readByte(std::uint32_t address) override;
        std::uint16_t readWord(std::uint32_t address) override;
        void writeByte(std::uint32_t address, std::uint8_t value) override;
        void writeWord(std::uint32_t address, std::uint16_t value) override;
        std::uint8_t readMemory(std::uint16_t address) override;
        void writeMemory(std::uint16_t address, std::uint8_t value) override;
        std::uint8_t input(std::uint16_t port) override;
        void output(std::uint16_t port, std::uint8_t value) override;

    private:
        Moto68000 cpu_;
        Z80 z80_;
        Cartridge cartridge_;
        VDP vdp_;
        YM2612 ym2612_;
        SN76489 sn76489_;
        ControllerPorts controllers_;
    };
}
