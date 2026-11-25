#include "Core/VDP/VDP.h"
#include <algorithm>
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
        horizontalInterruptPending_ = false;
        horizontalInterruptCounter_ = 0;
        registers_.fill(0);
        vram_.clear();
        cram_.clear();
        vsram_.clear();
        pixels_.fill(0xff000000);
        spriteLine_.fill({});
        spriteCollision_ = false;
        spriteOverflow_ = false;
        address_ = 0;
        code_ = 0;
        commandPending_ = false;
        dmaActive_ = false;
        dmaFillPending_ = false;
        dmaRemaining_ = dmaSource_ = dmaSlots_ = dmaQueued_ = 0;
        dmaCopyByte_ = 0;
        dmaFillValue_ = 0;
        fifo_.fill({});
        fifoHead_ = fifoCount_ = 0;
    }

    void VDP::clock()
    {
        if ((fifoCount_ != 0 || dmaActive_) && externalAccessSlot())
            clockAccessSlot();
        if (line_ == visibleLines && cycleInLine_ == verticalInterruptCycle)
        {
            verticalInterruptPending_ = true;
            verticalInterruptFlag_ = true;
        }
        if (line_ < visibleLines && cycleInLine_ == horizontalInterruptCycle)
        {
            if (horizontalInterruptCounter_ == 0)
            {
                horizontalInterruptPending_ = true;
                horizontalInterruptCounter_ = registers_[10];
            }
            else
                --horizontalInterruptCounter_;
        }
        // A janela ativa usa dez clocks por pixel em H32 e oito em H40.
        const auto cyclesPerDot = (registers_[12] & 1) ? h40MasterCyclesPerDot : masterCyclesPerDot;
        if (line_ < height && cycleInLine_ >= 860 &&
            (cycleInLine_ - 860) % cyclesPerDot == 0)
        {
            const auto x = (cycleInLine_ - 860) / cyclesPerDot;
            if (x < visibleWidth())
            {
                if (x == 0)
                    prepareSpriteLine(line_);
                pixels_[line_ * width + x] = renderPixel(x, line_);
            }
        }
        if (++cycleInLine_ != masterCyclesPerLine)
            return;
        cycleInLine_ = 0;
        if (++line_ != linesPerFrame)
            return;
        line_ = 0;
        ++frame_;
        horizontalInterruptCounter_ = registers_[10];
    }

    std::uint16_t VDP::readStatus()
    {
        commandPending_ = false;
        std::uint16_t status = fifoCount_ == 0 ? 0x0200 : fifoCount_ == fifo_.size() ? 0x0100
                                                                                     : 0;
        if (dmaActive_ || dmaFillPending_)
            status |= 0x0002;
        if (line_ >= visibleLines)
            status |= 0x0008; // VBlank.
        // Janela inicial H32; será ajustada junto ao contador HV e aos modos de vídeo.
        if (cycleInLine_ >= 280 && cycleInLine_ < 860)
            status |= 0x0004; // HBlank.
        if (verticalInterruptFlag_)
            status |= 0x0080;
        if (spriteCollision_)
            status |= 0x0020;
        if (spriteOverflow_)
            status |= 0x0040;
        verticalInterruptFlag_ = false;
        spriteCollision_ = false;
        spriteOverflow_ = false;
        return status;
    }

    std::uint16_t VDP::readHvCounter() const
    {
        // No NTSC de 224 linhas, V recua de $EA para $E5 no fim do quadro.
        const unsigned vertical = line_ <= 0xea ? line_ : 0xe5 + line_ - 0xeb;
        const bool h40 = (registers_[12] & 1) != 0;
        const unsigned firstRange = h40 ? 0xb7 : 0x94;
        const unsigned secondRange = h40 ? 0xe4 : 0xe9;
        const unsigned ticksPerLine = firstRange + 0x100 - secondRange;
        const unsigned tick = cycleInLine_ * ticksPerLine / masterCyclesPerLine;
        const unsigned horizontal = tick < firstRange ? tick : secondRange + tick - firstRange;
        return std::uint16_t((vertical << 8) | horizontal);
    }

    void VDP::acknowledgeInterrupt(unsigned level)
    {
        if (level == 6)
            verticalInterruptPending_ = false;
        else if (level == 4)
            horizontalInterruptPending_ = false;
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

    bool VDP::windowVisible(unsigned x, unsigned y) const
    {
        const unsigned horizontalBoundary = (registers_[17] & 0x1f) * 16;
        const unsigned verticalBoundary = (registers_[18] & 0x1f) * 8;
        const bool horizontal = (registers_[17] & 0x80) ? x >= horizontalBoundary
                                                        : x < horizontalBoundary;
        const bool vertical = (registers_[18] & 0x80) ? y >= verticalBoundary
                                                      : y < verticalBoundary;
        return horizontal || vertical;
    }

    VDP::PlanePixel VDP::windowPixel(unsigned x, unsigned y) const
    {
        const bool h40 = (registers_[12] & 1) != 0;
        const unsigned base = unsigned(registers_[3] & (h40 ? 0x3c : 0x3e)) << 10;
        const unsigned cellsWide = h40 ? 64 : 32;
        const auto entryAddress = std::uint16_t(base + 2 * ((y >> 3) * cellsWide + (x >> 3)));
        const auto attribute = vramWord(entryAddress);
        const unsigned tileX = (attribute & 0x0800) ? 7 - (x & 7) : x & 7;
        const unsigned tileY = (attribute & 0x1000) ? 7 - (y & 7) : y & 7;
        const auto patternAddress = std::uint16_t((attribute & 0x07ff) * 32 + tileY * 4 + (tileX >> 1));
        const auto pattern = vram_.read(patternAddress);
        const unsigned color = (tileX & 1) ? pattern & 15 : pattern >> 4;
        if (color == 0)
            return {};
        return {std::uint8_t(((attribute >> 9) & 0x30) | color), (attribute & 0x8000) != 0};
    }

    std::uint16_t VDP::vramWord(unsigned address) const
    {
        return std::uint16_t((unsigned(vram_.read(std::uint16_t(address))) << 8) |
                             vram_.read(std::uint16_t(address + 1)));
    }

    void VDP::prepareSpriteLine(unsigned y)
    {
        spriteLine_.fill({});
        if (!(registers_[1] & 0x40))
            return;

        const bool h40 = (registers_[12] & 1) != 0;
        const unsigned maxSprites = h40 ? 80 : 64;
        const unsigned maxLineSprites = h40 ? 20 : 16;
        const unsigned base = unsigned(registers_[5] & (h40 ? 0x7e : 0x7f)) << 9;
        unsigned link = 0;
        unsigned lineSprites = 0;
        unsigned remainingDots = visibleWidth();
        // A ordem da lista define qual sprite conserva o pixel em uma sobreposição.
        for (unsigned visited = 0; visited < maxSprites; ++visited)
        {
            const unsigned entry = base + link * 8;
            const unsigned positionY = vramWord(entry) & 0x1ff;
            const unsigned sizeAndLink = vramWord(entry + 2);
            const unsigned attribute = vramWord(entry + 4);
            const unsigned positionX = vramWord(entry + 6) & 0x1ff;
            const unsigned tilesWide = ((sizeAndLink >> 10) & 3) + 1;
            const unsigned tilesHigh = ((sizeAndLink >> 8) & 3) + 1;
            const unsigned localY = (y + 128 - positionY) & 0x1ff;
            const unsigned next = sizeAndLink & 0x7f;

            if (localY < tilesHigh * 8)
            {
                if (lineSprites == maxLineSprites || remainingDots == 0)
                {
                    spriteOverflow_ = true;
                    break;
                }
                ++lineSprites;
                const unsigned spriteWidth = tilesWide * 8;
                const unsigned drawnDots = std::min(spriteWidth, remainingDots);
                remainingDots -= drawnDots;

                // X igual a zero oculta os sprites seguintes desta linha.
                if (positionX == 0)
                    break;

                const unsigned sourceY = (attribute & 0x1000) ? tilesHigh * 8 - 1 - localY : localY;
                for (unsigned localX = 0; localX < drawnDots; ++localX)
                {
                    const int x = int(positionX) - 128 + int(localX);
                    if (x < 0 || x >= int(visibleWidth()))
                        continue;
                    const unsigned sourceX = (attribute & 0x0800) ? spriteWidth - 1 - localX : localX;
                    // Os tiles de um sprite avançam primeiro na vertical.
                    const unsigned tile = ((attribute & 0x07ff) +
                                           (sourceX >> 3) * tilesHigh + (sourceY >> 3)) &
                                          0x07ff;
                    const auto patternAddress = std::uint16_t(tile * 32 + (sourceY & 7) * 4 + ((sourceX & 7) >> 1));
                    const unsigned pattern = vram_.read(patternAddress);
                    const unsigned color = (sourceX & 1) ? pattern & 15 : pattern >> 4;
                    if (color == 0)
                        continue;
                    auto &pixel = spriteLine_[unsigned(x)];
                    if (pixel.color)
                    {
                        spriteCollision_ = true;
                        continue;
                    }
                    pixel = {std::uint8_t(((attribute >> 9) & 0x30) | color), (attribute & 0x8000) != 0};
                }
            }
            if (next == 0 || next >= maxSprites)
                break;
            link = next;
        }
    }

    std::uint32_t VDP::renderPixel(unsigned x, unsigned y) const
    {
        if (!(registers_[1] & 0x40))
            return backdropColor();
        // A janela substitui o plano A inclusive nos pixels transparentes.
        const auto a = windowVisible(x, y) ? windowPixel(x, y) : planePixel(0, x, y);
        const auto b = planePixel(1, x, y);
        const auto sprite = spriteLine_[x];
        // Ordem do modo 5: sprites altos, A alta, B alta, sprites baixos, A baixa e B baixa.
        if (sprite.color && sprite.priority)
            return paletteColor(sprite.color);
        if (a.color && a.priority)
            return paletteColor(a.color);
        if (b.color && b.priority)
            return paletteColor(b.color);
        if (sprite.color)
            return paletteColor(sprite.color);
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
        writeDataAt(address_, code_, value);
        address_ = std::uint16_t(address_ + registers_[15]);
        if (startFill)
        {
            dmaFillPending_ = false;
            dmaFillValue_ = value;
            startDma(DmaMode::Fill);
        }
    }

    bool VDP::tryWriteData(std::uint16_t value)
    {
        if (dmaFillPending_)
        {
            writeData(value);
            return true;
        }
        if (fifoCount_ == fifo_.size())
            return false;
        const auto mode = code_ & 0x0f;
        if (mode != 1 && mode != 3 && mode != 5)
            throw std::logic_error("Modo de escrita de dados do VDP ainda não implementado");
        if (mode == 5 && ((address_ & 0x7e) >> 1) >= VSRAM::size())
            throw std::logic_error("Endereço de VSRAM fora da área física");
        fifo_[(fifoHead_ + fifoCount_) % fifo_.size()] = {address_, value, code_, std::uint8_t(mode == 1 ? 2 : 1), false};
        ++fifoCount_;
        address_ = std::uint16_t(address_ + registers_[15]);
        commandPending_ = false;
        return true;
    }

    void VDP::writeDataAt(std::uint16_t address, std::uint8_t code, std::uint16_t value)
    {
        switch (code & 0x0f)
        {
        case 1:
        {
            const auto aligned = address & 0xfffe;
            if (address & 1)
                value = std::uint16_t((value << 8) | (value >> 8));
            vram_.write(aligned, std::uint8_t(value >> 8));
            vram_.write(aligned + 1, std::uint8_t(value));
            break;
        }
        case 3:
            cram_.write((address >> 1) & 0x3f, value & 0x0eee);
            break;
        case 5:
            if (((address & 0x7e) >> 1) >= VSRAM::size())
                throw std::logic_error("Endereço de VSRAM fora da área física");
            vsram_.write((address & 0x7e) >> 1, value & 0x07ff);
            break;
        default:
            throw std::logic_error("Modo de escrita de dados do VDP ainda não implementado");
        }
    }

    bool VDP::externalAccessSlot()
    {
        const bool h40 = (registers_[12] & 1) != 0;
        if (line_ < visibleLines && (registers_[1] & 0x40))
        {
            // Posições em clocks mestres medidas para os slots externos do modo 5.
            // Fonte: Genesis Plus GX, core/vdp_ctrl.c, fifo_timing_h32/h40.
            static constexpr std::array<unsigned, 16> h32{
                230, 510, 810, 970, 1130, 1450, 1610, 1770,
                2090, 2250, 2410, 2730, 2890, 3050, 3350, 3370};
            static constexpr std::array<unsigned, 18> h40Slots{
                352, 820, 948, 1076, 1332, 1460, 1588, 1844, 1972,
                2100, 2356, 2484, 2612, 2868, 2996, 3124, 3364, 3380};
            return h40 ? std::binary_search(h40Slots.begin(), h40Slots.end(), cycleInLine_)
                       : std::binary_search(h32.begin(), h32.end(), cycleInLine_);
        }

        if (!h40)
        {
            // Em H32, quatro clocks seriais formam um acesso de 20 clocks mestres.
            // A fase dos quatro refreshes em branco ainda é uma estimativa.
            if (cycleInLine_ % 20 != 10)
                return false;
            const unsigned slot = cycleInLine_ / 20;
            return slot != 31 && slot != 63 && slot != 95 && slot != 127;
        }

        // H40 alterna os divisores do clock na região de sincronismo. A fase
        // dos slots e refreshes em branco ainda precisa de medição; preservamos
        // 210 slots por linha e os cinco ciclos reservados ao refresh da VRAM.
        const unsigned next = (cycleInLine_ + 1) * 210 / masterCyclesPerLine;
        if (next == cycleInLine_ * 210 / masterCyclesPerLine)
            return false;
        const unsigned slot = next - 1;
        return slot != 41 && slot != 83 && slot != 125 && slot != 167 && slot != 209;
    }

    void VDP::clockAccessSlot()
    {
        // A FIFO drena antes de um novo pedido de DMA. Um slot nunca atende
        // duas operações de VRAM/CRAM/VSRAM.
        const bool fifoOccupied = fifoCount_ != 0;
        if (fifoOccupied)
            clockFifo();
        else if (dmaActive_ && dmaMode_ != DmaMode::Bus)
            clockDma();

        // A DMA pelo barramento lê uma palavra e a coloca na mesma FIFO.
        // A leitura pode ocorrer enquanto o slot drena a entrada anterior.
        if (dmaActive_ && dmaMode_ == DmaMode::Bus && dmaRemaining_ && fifoCount_ < fifo_.size())
            queueDmaWord();
    }

    void VDP::clockFifo()
    {
        auto &entry = fifo_[fifoHead_];
        if (--entry.slots)
            return;
        writeDataAt(entry.address, entry.code, entry.value);
        const bool fromDma = entry.dma;
        fifoHead_ = (fifoHead_ + 1) % fifo_.size();
        --fifoCount_;
        if (fromDma && --dmaQueued_ == 0 && dmaRemaining_ == 0)
            finishDma();
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
        dmaSlots_ = mode == DmaMode::Fill ? 2 : 0;
        dmaQueued_ = 0;
        dmaCopyByte_ = 0;
        dmaActive_ = true;
    }

    void VDP::queueDmaWord()
    {
        const auto value = dmaReader_(dmaSource_);
        const auto mode = code_ & 0x0f;
        fifo_[(fifoHead_ + fifoCount_) % fifo_.size()] =
            {address_, value, code_, std::uint8_t(mode == 1 ? 2 : 1), true};
        ++fifoCount_;
        ++dmaQueued_;
        address_ = std::uint16_t(address_ + registers_[15]);
        dmaSource_ = (dmaSource_ & 0xfe0000u) | ((dmaSource_ + 2) & 0x1ffffu);
        --dmaRemaining_;
        registers_[19] = std::uint8_t(dmaRemaining_);
        registers_[20] = std::uint8_t(dmaRemaining_ >> 8);
    }

    void VDP::clockDma()
    {
        // O preenchimento espera dois slots após a escrita inicial.
        if (dmaMode_ == DmaMode::Fill && dmaSlots_ != 0)
        {
            --dmaSlots_;
            return;
        }
        if (dmaMode_ == DmaMode::Copy)
        {
            // A leitura ocupa o primeiro slot; a escrita ocorre no segundo.
            if (dmaSlots_ == 0)
            {
                dmaCopyByte_ = vram_.read(dmaSource_ & 0xffff);
                dmaSlots_ = 1;
                return;
            }
            dmaSlots_ = 0;
            vram_.write(address_, dmaCopyByte_);
            dmaSource_ = (dmaSource_ + 1) & 0xffff;
            address_ = std::uint16_t(address_ + registers_[15]);
        }
        else
        {
            if ((code_ & 0x0f) == 1)
            {
                vram_.write(address_, std::uint8_t(dmaFillValue_ >> 8));
                address_ = std::uint16_t(address_ + registers_[15]);
            }
            else
                writeData(dmaFillValue_);
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