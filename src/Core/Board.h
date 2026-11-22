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
#include "Core/Memory/AudioRAM.h"
#include "Core/VDP/VDP.h"
#include "Core/Scheduler/Scheduler.h"

namespace R2MEGA::Core
{
    // Possui os componentes e os coordena em uma base de tempo comum.
    // O mapa atual cobre ROM, Work RAM, Audio RAM, controles do Z80 e portas do VDP.
    class Board final : public M68KBus, public Z80Bus, private SchedulerTarget
    {
    public:
        Board();
        Board(const Board &) = delete;
        Board &operator=(const Board &) = delete;

        // Instala o cartucho e inicia a leitura dos vetores de reset do 68000.
        void insertCartridge(Cartridge cartridge);
        const Moto68000 &m68k() const { return cpu_; }
        const VDP &vdp() const { return vdp_; }
        const Z80 &z80() const { return z80_; }
        const SN76489 &psg() const { return sn76489_; }
        const YM2612 &ym2612() const { return ym2612_; }

        // Atualiza os botões lógicos das duas portas antes da leitura pela ROM.
        void setControllerButtons(unsigned player, std::uint16_t buttons) { controllers_.setButtons(player, buttons); }
        std::uint16_t controllerButtons(unsigned player) const { return controllers_.buttons(player); }

        // Permite que os componentes da placa apresentem um nível IPL ao 68000.
        void setM68KInterruptLevel(unsigned level);

        // Avança um ciclo mestre de todos os componentes conectados ao scheduler.
        using Z80Bus::clock;
        void clock();
        std::uint64_t masterCycles() const { return scheduler_.masterCycles(); }

        // Acessos do 68000 aos dispositivos atualmente ligados ao barramento.
        std::uint8_t readByte(std::uint32_t address) override;
        std::uint16_t readWord(std::uint32_t address) override;
        bool tryWriteWord(std::uint32_t address, std::uint16_t value) override;
        bool tryAcknowledgeInterrupt(unsigned level, M68KInterruptResponse &response) override;
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
        bool z80BusGranted() const;
        void setZ80BusRequest(bool requested);
        void setZ80Reset(bool asserted);
        void writeZ80BankBit(std::uint8_t value);

        Moto68000 cpu_;
        Z80 z80_;
        Cartridge cartridge_;
        WorkRAM workRam_;
        AudioRAM audioRam_;
        VDP vdp_;
        YM2612 ym2612_;
        SN76489 sn76489_;
        ControllerPorts controllers_;
        Scheduler scheduler_;
        bool z80BusRequested_ = false;
        bool z80ResetAsserted_ = true;
        std::uint16_t z80Bank_ = 0;
        unsigned externalInterruptLevel_ = 0;
    };
}