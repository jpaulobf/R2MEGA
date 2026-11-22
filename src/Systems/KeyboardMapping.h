#pragma once
#include <SDL3/SDL.h>
#include <array>

namespace R2MEGA::Systems
{
    // Ordem: cima, baixo, esquerda, direita, A, B, C e Start; zero remove a associação.
    using KeyboardMapping = std::array<std::array<SDL_Keycode, 8>, 2>;

    inline KeyboardMapping defaultKeyboardMapping()
    {
        return {{{SDLK_W, SDLK_S, SDLK_A, SDLK_D, SDLK_J, SDLK_K, SDLK_L, SDLK_RETURN},
                 {SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_Z, SDLK_X, SDLK_C, SDLK_1}}};
    }

    inline bool assignableKey(SDL_Keycode key)
    {
        if (!key || key == SDLK_ESCAPE || key == SDLK_TAB || key == SDLK_PAUSE ||
            (key >= SDLK_F5 && key <= SDLK_F12) || key == SDLK_LCTRL || key == SDLK_RCTRL ||
            key == SDLK_LALT || key == SDLK_RALT || key == SDLK_LGUI || key == SDLK_RGUI)
            return false;
        return SDL_GetKeyName(key)[0] != '\0';
    }
}