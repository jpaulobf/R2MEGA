#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include "Core/Memory/VRAM.h"
#include "Core/Memory/CRAM.h"
#include "Core/Memory/VSRAM.h"

namespace R2MEGA::Core
{
    // Base temporal do VDP NTSC em H32 e H40, acionada a cada ciclo mestre.
    class VDP
    {
    public:
        static constexpr std::uint32_t masterCyclesPerLine = 3420;
        static constexpr std::uint32_t masterCyclesPerDot = 10;
        static constexpr std::uint32_t h40MasterCyclesPerDot = 8;
        static constexpr std::uint32_t linesPerFrame = 262;
        static constexpr std::uint32_t visibleLines = 224;
        static constexpr std::uint32_t verticalInterruptCycle = 770;

        void reset();
        void clock();

        std::uint32_t cycleInLine() const { return cycleInLine_; }
        std::uint32_t dot() const { return cycleInLine_ / ((registers_[12] & 1) ? h40MasterCyclesPerDot : masterCyclesPerDot); }
        std::uint32_t line() const { return line_; }
        std::uint64_t frame() const { return frame_; }

        // Leitura da porta de controle em $C00004/$C00006.
        std::uint16_t readStatus();
        // O pedido vertical permanece até o reconhecimento pelo 68000.
        unsigned interruptLevel() const { return verticalInterruptPending_ && (registers_[1] & 0x20) ? 6 : 0; }
        void acknowledgeInterrupt(unsigned level);
        std::uint16_t readData();
        void writeControl(std::uint16_t value);
        void writeData(std::uint16_t value);
        void setDmaReader(std::function<std::uint16_t(std::uint32_t)> reader);
        bool dmaActive() const { return dmaActive_; }
        bool dmaBlocksM68K() const { return dmaActive_ && dmaMode_ == DmaMode::Bus; }
        std::uint32_t dmaRemaining() const { return dmaRemaining_; }

        std::uint8_t registerValue(unsigned index) const { return registers_.at(index); }
        std::uint8_t vramByte(std::uint16_t address) const { return vram_.read(address); }
        std::uint16_t cramWord(unsigned index) const { return cram_.read(index); }
        std::uint16_t vsramWord(unsigned index) const { return vsram_.read(index); }
        std::uint16_t dataAddress() const { return address_; }
        static constexpr unsigned h32Width = 256;
        static constexpr unsigned width = 320;
        static constexpr unsigned height = visibleLines;
        unsigned visibleWidth() const { return (registers_[12] & 1) ? width : h32Width; }
        const std::array<std::uint32_t, width * height> &pixels() const { return pixels_; }

    private:
        struct PlanePixel
        {
            std::uint8_t color = 0;
            bool priority = false;
        };
        enum class DmaMode
        {
            Bus,
            Fill,
            Copy
        };
        PlanePixel planePixel(unsigned plane, unsigned x, unsigned y) const;
        std::uint32_t renderPixel(unsigned x, unsigned y) const;
        std::uint32_t paletteColor(unsigned index) const;
        std::uint32_t backdropColor() const;
        void startDma(DmaMode mode);
        void clockDma();
        void finishDma();
        std::uint32_t cycleInLine_ = 0;
        std::uint32_t line_ = 0;
        std::uint64_t frame_ = 0;
        bool verticalInterruptPending_ = false;
        bool verticalInterruptFlag_ = false;
        std::array<std::uint8_t, 24> registers_{};
        VRAM vram_;
        CRAM cram_;
        VSRAM vsram_;
        std::array<std::uint32_t, width * height> pixels_{};
        std::uint16_t address_ = 0;
        std::uint8_t code_ = 0;
        bool commandPending_ = false;
        std::function<std::uint16_t(std::uint32_t)> dmaReader_;
        DmaMode dmaMode_ = DmaMode::Bus;
        bool dmaActive_ = false;
        bool dmaFillPending_ = false;
        std::uint32_t dmaRemaining_ = 0;
        std::uint32_t dmaSource_ = 0;
        std::uint32_t dmaAccumulator_ = 0;
        std::uint16_t dmaFillValue_ = 0;
    };
}