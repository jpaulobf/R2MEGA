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
        static constexpr std::uint32_t horizontalInterruptCycle = 280;

        void reset();
        void clock();

        std::uint32_t cycleInLine() const { return cycleInLine_; }
        std::uint32_t dot() const { return cycleInLine_ / ((registers_[12] & 1) ? h40MasterCyclesPerDot : masterCyclesPerDot); }
        std::uint32_t line() const { return line_; }
        std::uint64_t frame() const { return frame_; }

        // Leitura da porta de controle em $C00004/$C00006.
        std::uint16_t readStatus();
        // Em $C00008, V ocupa o byte alto e H ocupa o byte baixo.
        std::uint16_t readHvCounter() const;
        // V-INT tem prioridade sobre H-INT; cada pedido permanece até o reconhecimento.
        unsigned interruptLevel() const
        {
            if (verticalInterruptPending_ && (registers_[1] & 0x20))
                return 6;
            return horizontalInterruptPending_ && (registers_[0] & 0x10) ? 4 : 0;
        }
        // A saída INT do Z80 vai do disparo vertical até o fim da primeira linha de VBlank.
        bool z80InterruptAsserted() const
        {
            return line_ == visibleLines && cycleInLine_ > verticalInterruptCycle;
        }
        void acknowledgeInterrupt(unsigned level);
        std::uint16_t readData();
        void writeControl(std::uint16_t value);
        void writeData(std::uint16_t value);
        bool tryWriteData(std::uint16_t value);
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
        PlanePixel windowPixel(unsigned x, unsigned y) const;
        bool windowVisible(unsigned x, unsigned y) const;
        void prepareSpriteLine(unsigned y);
        std::uint16_t vramWord(unsigned address) const;
        std::uint32_t renderPixel(unsigned x, unsigned y) const;
        std::uint32_t paletteColor(unsigned index) const;
        std::uint32_t backdropColor() const;
        void startDma(DmaMode mode);
        bool externalAccessSlot();
        void clockAccessSlot();
        void queueDmaWord();
        void clockDma();
        void clockFifo();
        void finishDma();
        void writeDataAt(std::uint16_t address, std::uint8_t code, std::uint16_t value);
        std::uint32_t cycleInLine_ = 0;
        std::uint32_t line_ = 0;
        std::uint64_t frame_ = 0;
        bool verticalInterruptPending_ = false;
        bool verticalInterruptFlag_ = false;
        bool horizontalInterruptPending_ = false;
        std::uint8_t horizontalInterruptCounter_ = 0;
        std::array<std::uint8_t, 24> registers_{};
        VRAM vram_;
        CRAM cram_;
        VSRAM vsram_;
        std::array<std::uint32_t, width * height> pixels_{};
        std::array<PlanePixel, width> spriteLine_{};
        bool spriteCollision_ = false;
        bool spriteOverflow_ = false;
        std::uint16_t address_ = 0;
        std::uint8_t code_ = 0;
        bool commandPending_ = false;
        std::function<std::uint16_t(std::uint32_t)> dmaReader_;
        DmaMode dmaMode_ = DmaMode::Bus;
        bool dmaActive_ = false;
        bool dmaFillPending_ = false;
        std::uint32_t dmaRemaining_ = 0;
        std::uint32_t dmaSource_ = 0;
        unsigned dmaSlots_ = 0;
        unsigned dmaQueued_ = 0;
        std::uint8_t dmaCopyByte_ = 0;
        std::uint16_t dmaFillValue_ = 0;
        struct FifoEntry
        {
            std::uint16_t address = 0, value = 0;
            std::uint8_t code = 0, slots = 0;
            bool dma = false;
        };
        std::array<FifoEntry, 4> fifo_{};
        unsigned fifoHead_ = 0, fifoCount_ = 0;
    };
}