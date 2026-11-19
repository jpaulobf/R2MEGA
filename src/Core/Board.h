#pragma once

#include "Core/Audio/SN76489/SN76489.h"
#include "Core/Audio/YM2612/YM2612.h"
#include "Core/Bus/M68KBus.h"
#include "Core/Bus/Z80Bus.h"
#include "Core/CPU/Moto68000/Moto68000.h"
#include "Core/CPU/Z80/Z80.h"
#include "Core/Cartridge/Cartridge.h"
#include "Core/IO/ControllerPorts.h"
#include "Core/Memory/WorkRAM.h"
#include "Core/VDP/VDP.h"
#include "Core/Scheduler/Scheduler.h"

namespace R2MEGA::Core
{
    // Possui os componentes e coordenará todos em uma base de tempo comum.
    // O mapa inicial cobre ROM e Work RAM; periféricos, arbitragem e DMA virão depois.
    class Board final : public M68KBus, public Z80Bus, private SchedulerTarget
    {
    public:
        Board();
        Board(const Board &) = delete;
        Board &operator=(const Board &) = delete;

        // Instala o cartucho e inicia a leitura dos vetores de reset do 68000.
        void insertCartridge(Cartridge cartridge);
        const Moto68000 &m68k() const { return cpu_; }
        // Permite que os componentes da placa apresentem um nível IPL ao 68000.
        void setM68KInterruptLevel(unsigned level);

        // Avança um ciclo mestre; a CPU ainda não executa instruções.
        using Z80Bus::clock;
        void clock();
        std::uint64_t masterCycles() const { return scheduler_.masterCycles(); }

        // Mapeamento inicial do 68000: ROM do cartucho e Work RAM.
        std::uint8_t readByte(std::uint32_t address) override;
        std::uint16_t readWord(std::uint32_t address) override;
        void writeByte(std::uint32_t address, std::uint8_t value) override;
        void writeWord(std::uint32_t address, std::uint16_t value) override;
        std::uint8_t readMemory(std::uint16_t address) const override;
        void writeMemory(std::uint16_t address, std::uint8_t value) override;
        std::uint8_t input(std::uint16_t port) override;
        void output(std::uint16_t port, std::uint8_t value) override;

    private:
        void clockVDP() override;
        void clockM68K() override;
        void clockZ80() override;

        Moto68000 cpu_;
        Z80 z80_;
        Cartridge cartridge_;
        WorkRAM workRam_;
        VDP vdp_;
        YM2612 ym2612_;
        SN76489 sn76489_;
        ControllerPorts controllers_;
        Scheduler scheduler_;
    };
}