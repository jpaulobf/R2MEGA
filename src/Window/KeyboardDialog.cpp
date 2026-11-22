#include "Window/KeyboardDialog.h"
#include <imgui.h>
#include <string>

namespace R2MEGA::Window
{
    void KeyboardDialog::open()
    {
        draft_ = mapping_;
        player_ = 0;
        capture_ = -1;
        error_ = false;
        requested_ = true;
    }

    bool KeyboardDialog::processEvent(const SDL_Event &event, SDL_WindowID id)
    {
        if (!active() || event.type != SDL_EVENT_KEY_DOWN || event.key.windowID != id)
            return false;
        if (!event.key.repeat && capture_ >= 0 && Systems::assignableKey(event.key.key))
        {
            // Uma tecla só pertence a uma função entre os dois controles.
            for (auto &player : draft_)
                for (auto &key : player)
                    if (key == event.key.key)
                        key = 0;
            draft_[player_][capture_] = event.key.key;
            capture_ = -1;
        }
        return true;
    }

    void KeyboardDialog::draw(const Saver &save)
    {
        if (requested_)
        {
            ImGui::OpenPopup("Configure Keyboard");
            requested_ = false;
            open_ = true;
        }
        ImGui::SetNextWindowSize(ImVec2(430, 350), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Configure Keyboard", &open_, ImGuiWindowFlags_NoResize))
            return;
        ImGui::TextUnformatted("Controller");
        ImGui::SameLine(150);
        ImGui::TextUnformatted("Button");
        ImGui::SameLine(270);
        ImGui::TextUnformatted("Mapping");
        ImGui::BeginChild("controllers", ImVec2(130, 215), ImGuiChildFlags_Borders);
        for (int player = 0; player < 2; ++player)
            if (ImGui::Selectable(player ? "Pad 2" : "Pad 1", player == player_))
            {
                player_ = player;
                capture_ = -1;
            }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("bindings", ImVec2(0, 215), ImGuiChildFlags_Borders);
        constexpr const char *names[] = {"Up", "Down", "Left", "Right", "A", "B", "C", "Start"};
        for (int button = 0; button < 8; ++button)
        {
            ImGui::PushID(button);
            ImGui::TextUnformatted(names[button]);
            ImGui::SameLine(120);
            const auto key = draft_[player_][button];
            const char *label = capture_ == button || !key ? "" : SDL_GetKeyName(key);
            if (ImGui::Button((std::string(label) + "###key").c_str(), ImVec2(-1, 0)))
                capture_ = button;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextUnformatted(capture_ >= 0 ? "Press a key..." : "Select a mapping to change it.");
        if (ImGui::Button("Cancel"))
        {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Default"))
        {
            draft_ = Systems::defaultKeyboardMapping();
            capture_ = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (save(draft_))
            {
                mapping_ = draft_;
                open_ = false;
                ImGui::CloseCurrentPopup();
            }
            else
                error_ = true;
        }
        if (error_)
            ImGui::TextWrapped("Could not save keyboard settings.");
        ImGui::EndPopup();
    }
}