#pragma once
#include "Systems/KeyboardMapping.h"
#include <functional>

namespace R2MEGA::Window
{
    // Mantém um rascunho separado até salvar o mapeamento dos dois jogadores.
    class KeyboardDialog
    {
    public:
        using Saver = std::function<bool(const Systems::KeyboardMapping &)>;
        void setMapping(const Systems::KeyboardMapping &mapping) { mapping_ = mapping; }
        const Systems::KeyboardMapping &mapping() const { return mapping_; }
        void open();
        bool active() const { return requested_ || open_; }
        bool processEvent(const SDL_Event &event, SDL_WindowID id);
        void draw(const Saver &save);

    private:
        Systems::KeyboardMapping mapping_ = Systems::defaultKeyboardMapping();
        Systems::KeyboardMapping draft_ = mapping_;
        int player_ = 0, capture_ = -1;
        bool requested_ = false, open_ = false, error_ = false;
    };
}