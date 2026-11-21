#include "Core/VDP/VDP.h"
#include <stdexcept>
#include <utility>

namespace R2MEGA::Core
{
    void VDP::reset()
    {
        cycleInLine_ = 0;
        line_ = 0;
        frame_ = 0;
        verticalInterruptPending_ = false;
        verticalInterruptFlag_ = false;
        registers_.fill(0);
        vram_.clear();
        cram_.clear();
        vsram_.clear();
        pixels_.fill(0xff000000);
        address_ = 0;
        code_ = 0;
        commandPending_ = false;
        dmaActive_ = false;
        dmaFillPending_ = false;
        dmaRemaining_ = dmaSource_ = dmaAccumulator_ = 0;
        dmaFillValue_ = 0;
    }

    void VDP::clock()
    {
        clockDma();
        if (line_ == visibleLines && cycleInLine_ == verticalInterruptCycle)
        {
            verticalInterruptPending_ = true;
            verticalInterruptFlag_ = true;
        }
        // A janela ativa usa dez clocks por pixel em H32 e oito em H40.
        const auto cyclesPerDot = (registers_[12] & 1) ? h40MasterCyclesPerDot : masterCyclesPerDot;
        if (line_ < height && cycleInLine_ >= 860 &&
            (cycleInLine_ - 860) % cyclesPerDot == 0)
        {
            const auto x = (cycleInLine_ - 860) / cyclesPerDot;
            if (x < visibleWidth())
                pixels_[line_ * width + x] = renderPixel(x, line_);
        }
        if (++cycleInLine_ != masterCyclesPerLine)
            return;
        cycleInLine_ = 0;
        if (++line_ != linesPerFrame)
            return;
        line_ = 0;
        ++frame_;
    }

    std::uint16_t VDP::readStatus()
    {
        commandPending_ = false;
        // O FIFO está vazio enquanto as portas de escrita ainda não existem.
        std::uint16_t status = 0x0200;
        if (dmaActive_ || dmaFillPending_)
            status |= 0x0002;
        if (line_ >= visibleLines)
            status |= 0x0008; // VBlank.
        // Janela inicial H32; será ajustada junto ao contador HV e aos modos de vídeo.
        if (cycleInLine_ >= 280 && cycleInLine_ < 860)
            status |= 0x0004; // HBlank.
        if (verticalInterruptFlag_)
            status |= 0x0080;
        verticalInterruptFlag_ = false;
        return status;
    }

    void VDP::acknowledgeInterrupt(unsigned level)
    {
        if (level == 6)
            verticalInterruptPending_ = false;
    }

    VDP::PlanePixel VDP::planePixel(unsigned plane, unsigned x, unsigned y) const
    {
        // O registrador 16 define largura e altura em blocos de oito pixels.
        static constexpr unsigned widths[4]{32, 64, 32, 128};
        static constexpr unsigned heights[4]{32, 64, 96, 128};
        const unsigned cellsWide = widths[registers_[16] & 3];
        const unsigned cellsHigh = heights[(registers_[16] >> 4) & 3];
        // A tabela horizontal guarda duas palavras por linha: plano A, depois B.
        static constexpr unsigned lineMasks[4]{0, 7, 0xf8, 0xff};
        const unsigned scrollLine = y & lineMasks[registers_[11] & 3];
        const auto scrollAddress = std::uint16_t((unsigned(registers_[13] & 0x3f) << 10) +
                                                 4 * scrollLine + 2 * plane);
        const auto horizontalScroll = std::uint16_t((unsigned(vram_.read(scrollAddress)) << 8) |
                                                    vram_.read(std::uint16_t(scrollAddress + 1)));
        const unsigned verticalIndex = (registers_[11] & 4) ? 2 * (x >> 4) + plane : plane;
        const unsigned verticalScroll = vsram_.read(verticalIndex) & 0x3ff;
        const unsigned sourceX = (x - horizontalScroll) & (cellsWide * 8 - 1);
        const unsigned sourceY = (y + verticalScroll) % (cellsHigh * 8);
        const unsigned cellX = sourceX >> 3;
        const unsigned cellY = sourceY >> 3;
        const unsigned base = plane == 0
                                  ? (unsigned(registers_[2] & 0x38) << 10)
                                  : (unsigned(registers_[4] & 7) << 13);
        const auto entryAddress = std::uint16_t(base + 2 * (cellY * cellsWide + cellX));
        const auto attribute = std::uint16_t((unsigned(vram_.read(entryAddress)) << 8) |
                                             vram_.read(std::uint16_t(entryAddress + 1)));
        const unsigned tileX = (attribute & 0x0800) ? 7 - (sourceX & 7) : sourceX & 7;
        const unsigned tileY = (attribute & 0x1000) ? 7 - (sourceY & 7) : sourceY & 7;
        const auto patternAddress = std::uint16_t((attribute & 0x07ff) * 32 + tileY * 4 + (tileX >> 1));
        const auto pattern = vram_.read(patternAddress);
        const unsigned color = (tileX & 1) ? (pattern & 15) : (pattern >> 4);
        if (color == 0)
            return {};
        return {std::uint8_t(((attribute >> 9) & 0x30) | color), (attribute & 0x8000) != 0};
    }

    std::uint32_t VDP::renderPixel(unsigned x, unsigned y) const
    {
        if (!(registers_[1] & 0x40))
            return backdropColor();
        const auto a = planePixel(0, x, y);
        const auto b = planePixel(1, x, y);
        // Ordem do modo 5: A alta, B alta, A baixa e B baixa.
        if (a.color && a.priority)
            return paletteColor(a.color);
        if (b.color && b.priority)
            return paletteColor(b.color);
        if (a.color)
            return paletteColor(a.color);
        if (b.color)
            return paletteColor(b.color);
        return backdropColor();
    }

    std::uint32_t VDP::paletteColor(unsigned index) const
    {
        const auto color = cram_.read(index);
        const auto red = ((color >> 1) & 7) * 36;
        const auto green = ((color >> 5) & 7) * 36;
        const auto blue = ((color >> 9) & 7) * 36;
        return 0xff000000u | (red << 16) | (green << 8) | blue;
    }

    std::uint32_t VDP::backdropColor() const
    {
        return paletteColor(registers_[7] & 0x3f);
    }

    void VDP::writeControl(std::uint16_t value)
    {
        if (!commandPending_)
        {
            address_ = (address_ & 0xc000) | (value & 0x3fff);
            code_ = (code_ & 0x3c) | std::uint8_t(value >> 14);
            if ((value & 0xc000) == 0x8000)
            {
                const auto index = std::size_t((value >> 8) & 0x1f);
                if (index < registers_.size())
                    registers_[index] = std::uint8_t(value);
            }
            else
                commandPending_ = true;
            return;
        }
        address_ = (address_ & 0x3fff) | std::uint16_t((value & 3) << 14);
        code_ = (code_ & 3) | std::uint8_t((value >> 2) & 0x3c);
        commandPending_ = false;
        if ((code_ & 0x20) && (registers_[1] & 0x10))
        {
            const auto mode = registers_[23] >> 6;
            if (mode == 2)
                dmaFillPending_ = true;
            else
                startDma(mode == 3 ? DmaMode::Copy : DmaMode::Bus);
        }
    }

    void VDP::writeData(std::uint16_t value)
    {
        commandPending_ = false;
        const bool startFill = dmaFillPending_;
        switch (code_ & 0x0f)
        {
        case 1:
        {
            const auto aligned = address_ & 0xfffe;
            if (address_ & 1)
                value = std::uint16_t((value << 8) | (value >> 8));
            vram_.write(aligned, std::uint8_t(value >> 8));
            vram_.write(aligned + 1, std::uint8_t(value));
            break;
        }
        case 3:
            cram_.write((address_ >> 1) & 0x3f, value & 0x0eee);
            break;
        case 5:
            if (((address_ & 0x7e) >> 1) >= VSRAM::size())
                throw std::logic_error("Endereço de VSRAM fora da área física");
            vsram_.write((address_ & 0x7e) >> 1, value & 0x07ff);
            break;
        default:
            throw std::logic_error("Modo de escrita de dados do VDP ainda não implementado");
        }
        address_ = std::uint16_t(address_ + registers_[15]);
        if (startFill)
        {
            dmaFillPending_ = false;
            dmaFillValue_ = value;
            startDma(DmaMode::Fill);
        }
    }

    std::uint16_t VDP::readData()
    {
        commandPending_ = false;
        std::uint16_t value = 0;
        switch (code_ & 0x1f)
        {
        case 0:
        {
            const auto aligned = address_ & 0xfffe;
            value = (std::uint16_t(vram_.read(aligned)) << 8) | vram_.read(aligned + 1);
            break;
        }
        case 4:
            if (((address_ & 0x7e) >> 1) >= VSRAM::size())
                throw std::logic_error("Endereço de VSRAM fora da área física");
            value = vsram_.read((address_ & 0x7e) >> 1);
            break;
        case 8:
            value = cram_.read((address_ >> 1) & 0x3f);
            break;
        default:
            throw std::logic_error("Modo de leitura de dados do VDP ainda não implementado");
        }
        address_ = std::uint16_t(address_ + registers_[15]);
        return value;
    }

    void VDP::setDmaReader(std::function<std::uint16_t(std::uint32_t)> reader)
    {
        dmaReader_ = std::move(reader);
    }

    void VDP::startDma(DmaMode mode)
    {
        if (dmaActive_)
            throw std::logic_error("Nova DMA antes do término da anterior");
        if (mode == DmaMode::Bus && !dmaReader_)
            throw std::logic_error("Barramento de origem da DMA não conectado");
        if (mode == DmaMode::Copy && !(code_ & 0x10))
            throw std::logic_error("Comando de cópia de VRAM inválido");
        dmaMode_ = mode;
        dmaRemaining_ = (std::uint32_t(registers_[20]) << 8) | registers_[19];
        if (dmaRemaining_ == 0)
            dmaRemaining_ = 0x10000;
        const auto source = (std::uint32_t(registers_[22]) << 8) | registers_[21];
        dmaSource_ = mode == DmaMode::Bus
                         ? (std::uint32_t(registers_[23] & 0x7f) << 17) | (source << 1)
                         : source;
        dmaAccumulator_ = 0;
        dmaActive_ = true;
    }

    void VDP::clockDma()
    {
        if (!dmaActive_)
            return;
        const bool blank = line_ >= visibleLines || !(registers_[1] & 0x40);
        const bool h40 = (registers_[12] & 1) != 0;
        // Distribui a taxa média de acessos por linha; os slots exatos virão com o FIFO.
        unsigned rate = 0;
        if (dmaMode_ == DmaMode::Fill)
            rate = blank ? (h40 ? 203 : 165) : (h40 ? 17 : 15);
        else if (dmaMode_ == DmaMode::Copy || (code_ & 0x0f) == 1)
            rate = blank ? (h40 ? 102 : 83) : (h40 ? 9 : 8);
        else
            rate = blank ? (h40 ? 198 : 161) : (h40 ? 18 : 16);
        dmaAccumulator_ += rate;
        if (dmaAccumulator_ < masterCyclesPerLine)
            return;
        dmaAccumulator_ -= masterCyclesPerLine;

        if (dmaMode_ == DmaMode::Fill)
        {
            if ((code_ & 0x0f) == 1)
            {
                vram_.write(address_, std::uint8_t(dmaFillValue_ >> 8));
                address_ = std::uint16_t(address_ + registers_[15]);
            }
            else
                writeData(dmaFillValue_);
        }
        else if (dmaMode_ == DmaMode::Copy)
        {
            vram_.write(address_, vram_.read(dmaSource_ & 0xffff));
            dmaSource_ = (dmaSource_ + 1) & 0xffff;
            address_ = std::uint16_t(address_ + registers_[15]);
        }
        else
        {
            writeData(dmaReader_(dmaSource_));
            dmaSource_ = (dmaSource_ & 0xfe0000u) | ((dmaSource_ + 2) & 0x1ffffu);
        }
        --dmaRemaining_;
        registers_[19] = std::uint8_t(dmaRemaining_);
        registers_[20] = std::uint8_t(dmaRemaining_ >> 8);
        if (dmaRemaining_ == 0)
            finishDma();
    }

    void VDP::finishDma()
    {
        dmaActive_ = false;
        if (dmaMode_ != DmaMode::Fill)
        {
            const auto source = dmaMode_ == DmaMode::Bus ? dmaSource_ >> 1 : dmaSource_;
            registers_[21] = std::uint8_t(source);
            registers_[22] = std::uint8_t(source >> 8);
        }
    }
}