#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

namespace R2MEGA::Core
{
    // Intenções dos dois jogadores; cada botão pressionado usa um bit ativo em 1.
    class ControllerInput
    {
    public:
        enum Button : std::uint16_t
        {
            Up = 1 << 0,
            Down = 1 << 1,
            Left = 1 << 2,
            Right = 1 << 3,
            A = 1 << 4,
            B = 1 << 5,
            C = 1 << 6,
            Start = 1 << 7
        };
        using State = std::array<std::uint16_t, 2>;

        void reset() { buttons_.fill(0); }
        void setButtons(unsigned player, std::uint16_t buttons)
        {
            if (player >= buttons_.size())
                throw std::out_of_range("Jogador fora das portas de controle");
            buttons_[player] = buttons & 0xff;
        }
        std::uint16_t buttons(unsigned player) const { return buttons_.at(player); }

    private:
        State buttons_{};
    };
}