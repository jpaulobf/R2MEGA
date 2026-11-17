#include "Core/Board.h"
#include <stdexcept>

namespace R2MEGA::Core
{
    namespace
    {
        [[noreturn]] void notImplemented()
        {
            throw std::logic_error("R2MEGA machine emulation is not implemented yet");
        }
    }

    Board::Board() : cpu_(*this), z80_(*this) {}
    void Board::clock() { notImplemented(); }
    std::uint8_t Board::readByte(std::uint32_t) { notImplemented(); }
    std::uint16_t Board::readWord(std::uint32_t) { notImplemented(); }
    void Board::writeByte(std::uint32_t, std::uint8_t) { notImplemented(); }
    void Board::writeWord(std::uint32_t, std::uint16_t) { notImplemented(); }
    std::uint8_t Board::readMemory(std::uint16_t) { notImplemented(); }
    void Board::writeMemory(std::uint16_t, std::uint8_t) { notImplemented(); }
    std::uint8_t Board::input(std::uint16_t) { notImplemented(); }
    void Board::output(std::uint16_t, std::uint8_t) { notImplemented(); }
}
