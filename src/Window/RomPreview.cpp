#include "Window/RomPreview.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace R2MEGA::Window
{
    namespace
    {
        unsigned word(const std::vector<std::uint8_t> &rom, std::size_t address)
        {
            return (unsigned(rom[address]) << 8) | rom[address + 1];
        }
    }

    PreviewInstruction decodePreview(const std::vector<std::uint8_t> &rom, std::size_t address)
    {
        if (address >= rom.size())
            return {0, ""};
        char text[96];
        if (rom.size() - address == 1)
        {
            std::snprintf(text, sizeof(text), "DC.B   $%02X", unsigned(rom[address]));
            return {1, text};
        }
        // Instruções desconhecidas ou incompletas são apresentadas como dados.
        const unsigned op = word(rom, address);
        if (op == 0x4e71)
            return {2, "NOP"};
        if (op == 0x4e75)
            return {2, "RTS"};
        if (op == 0x4e73)
            return {2, "RTE"};
        if (op == 0x4e77)
            return {2, "RTR"};
        if (op == 0x4e70)
            return {2, "RESET"};
        if ((op & 0xf100) == 0x7000)
        {
            const int immediate = (op & 0x80) ? int(op & 0xff) - 256 : int(op & 0xff);
            std::snprintf(text, sizeof(text), "MOVEQ  #%d,D%u", immediate, (op >> 9) & 7);
            return {2, text};
        }
        if ((op & 0xf000) == 0x6000)
        {
            static const char *const names[] = {"BRA", "BSR", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
                                                "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE"};
            int displacement = int(op & 0xff);
            std::size_t length = 2;
            if (displacement == 0)
            {
                if (rom.size() - address < 4)
                    goto unknown;
                displacement = int(word(rom, address + 2));
                if (displacement & 0x8000)
                    displacement -= 0x10000;
                length = 4;
            }
            else if (displacement & 0x80)
                displacement -= 0x100;
            const auto target = static_cast<unsigned>((static_cast<std::int64_t>(address) + 2 + displacement) & 0xffffff);
            std::snprintf(text, sizeof(text), "%-6s $%06X", names[(op >> 8) & 15], target);
            return {length, text};
        }
        if ((op == 0x4ef9 || op == 0x4eb9) && rom.size() - address >= 6)
        {
            const unsigned target = (word(rom, address + 2) << 16) | word(rom, address + 4);
            std::snprintf(text, sizeof(text), "%s    $%08X", op == 0x4ef9 ? "JMP" : "JSR", target);
            return {6, text};
        }
    unknown:
        std::snprintf(text, sizeof(text), "DC.W   $%04X", op);
        return {2, text};
    }

    void RomPreview::reset(const std::vector<std::uint8_t> &rom)
    {
        validReset_ = false;
        address_ = 0;
        if (rom.size() < 8)
            return;
        // O vetor de reset do 68000 ocupa os bytes 4 a 7, em ordem big-endian.
        const unsigned entry = (unsigned(rom[4]) << 24) | (unsigned(rom[5]) << 16) |
                               (unsigned(rom[6]) << 8) | unsigned(rom[7]);
        if ((entry & 1) == 0 && entry < rom.size())
        {
            address_ = entry;
            validReset_ = true;
        }
    }

    void RomPreview::draw(const std::vector<std::uint8_t> &rom)
    {
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Temporary ROM preview", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("ROM: %zu bytes | Temporary 68000 preview", rom.size());
        ImGui::TextUnformatted("Partial decoder: DC.W is unknown; following alignment may be incorrect.");
        if (!validReset_)
            ImGui::TextUnformatted("Reset vector unavailable or outside the ROM; initial offset is 0.");
        ImGui::SetNextItemWidth(150);
        ImGui::InputScalar("Address (hex)", ImGuiDataType_U32, &address_, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        if (ImGui::Button("Back 100 hex bytes"))
            address_ = address_ > 256 ? address_ - 256 : 0;
        ImGui::SameLine();
        // Uma página contém até 128 instruções decodificadas; a próxima começa ao final dela.
        const unsigned start = std::min<unsigned>(address_, static_cast<unsigned>(rom.size()));
        std::size_t end = start;
        for (int i = 0; i < 128 && end < rom.size(); ++i)
            end += decodePreview(rom, end).length;
        if (ImGui::Button("Next"))
            address_ = static_cast<unsigned>(end);
        ImGui::SameLine();
        if (ImGui::Button("Reset vector"))
            reset(rom);
        if (ImGui::BeginChild("ROM bytes", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (std::size_t p = std::min<std::size_t>(address_, rom.size()), i = 0; i < 128 && p < rom.size(); ++i)
            {
                const auto instruction = decodePreview(rom, p);
                char bytes[32] = {};
                std::size_t used = 0;
                for (std::size_t n = 0; n < instruction.length; ++n)
                    used += static_cast<std::size_t>(std::snprintf(bytes + used, sizeof(bytes) - used, "%02X ", unsigned(rom[p + n])));
                ImGui::Text("%08X  %-18s %s", static_cast<unsigned>(p), bytes, instruction.text.c_str());
                p += instruction.length;
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }
}