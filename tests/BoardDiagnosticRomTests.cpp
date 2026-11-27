#include "Core/Board.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace R2MEGA::Core;

namespace
{
    void check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void putWord(std::vector<std::uint8_t> &rom, unsigned address, std::uint16_t value)
    {
        rom[address] = static_cast<std::uint8_t>(value >> 8);
        rom[address + 1] = static_cast<std::uint8_t>(value);
    }

    void runUntil(Board &board, std::uint64_t instructions)
    {
        const auto deadline = board.masterCycles() + 4000;
        while (board.m68k().state().instructions < instructions &&
               board.masterCycles() < deadline)
            board.clock();
        check(board.m68k().state().instructions == instructions,
              "A ROM de diagnóstico não concluiu as instruções esperadas");
        check(board.masterCycles() == board.m68k().state().cycles * 7,
              "O scheduler perdeu a proporção de sete clocks mestres por ciclo do 68000");
    }

    void testPeriodicBusWait(const std::filesystem::path &directory)
    {
        const auto path = directory / "m68k-bus-wait.md";
        for (const bool write : {false, true})
        {
            std::vector<std::uint8_t> rom(0x200);
            putWord(rom, 0, 0x00ff);
            putWord(rom, 2, 0xff00);
            putWord(rom, 6, 0x100);
            putWord(rom, 0x100, 0x7034); // MOVEQ #$34,D0.
            putWord(rom, 0x102, 0x41f8); // LEA $FF20.W,A0.
            putWord(rom, 0x104, 0xff20);
            for (unsigned address = 0x106; address < 0x132; address += 2)
                putWord(rom, address, 0x4e71);
            putWord(rom, 0x12c, write ? 0x3080 : 0x3210); // MOVE.W D0,(A0) ou (A0),D1.
            {
                std::ofstream file(path, std::ios::binary);
                file.write(reinterpret_cast<const char *>(rom.data()), rom.size());
                check(bool(file), "Não foi possível gravar a ROM de espera do barramento");
            }

            auto board = std::make_unique<Board>();
            board->insertCartridge(Cartridge::load(path));
            board->writeWord(0xffff20, 0x1234);
            runUntil(*board, 21);
            check(board->m68k().state().cycles == 128 && board->m68k().state().pc == 0x12c,
                  "A ROM deve chegar ao acesso de RAM depois dos primeiros 128 ciclos");
            for (unsigned cycle = 129; cycle <= 133; ++cycle)
            {
                for (unsigned tick = 0; tick < Scheduler::M68KDivider; ++tick)
                    board->clock();
                const auto &pins = board->m68k().pins();
                const bool waiting = cycle == 131 || cycle == 132;
                check(pins.waiting == waiting && pins.address == 0xffff20 && pins.write == write,
                      "A espera periódica deve segurar o mesmo acesso por dois ciclos");
                check(board->readWord(0xffff20) == (write && cycle == 133 ? 0x0034 : 0x1234),
                      "A escrita só pode chegar à RAM quando DTACK liberar o acesso");
            }
            runUntil(*board, 22);
            check(board->m68k().state().cycles == 138 &&
                      (write || board->m68k().state().data[1] == 0x1234),
                  "O MOVE deve terminar em dez ciclos e preservar o dado lido");
        }
    }

    void testPsgAudio(const std::filesystem::path &directory)
    {
        const auto path = directory / "psg-tone.md";
        std::vector<std::uint8_t> rom(0x200);
        putWord(rom, 0, 0x00ff);
        putWord(rom, 2, 0xff00);
        putWord(rom, 6, 0x100);
        unsigned pc = 0x100;
        // Período $0FE no primeiro canal, aproximadamente 440 Hz em NTSC.
        for (const auto value : {0x8e, 0x0f, 0x90})
        {
            for (const auto word : {0x13fc, value, 0x00c0, 0x0011}) // MOVE.B #valor,$C00011.L.
            {
                putWord(rom, pc, word);
                pc += 2;
            }
        }
        putWord(rom, pc, 0x4e72); // STOP #$2700.
        putWord(rom, pc + 2, 0x2700);
        const auto saveRom = [&rom](const std::filesystem::path &path)
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()), rom.size());
            check(bool(file), "Falha ao gravar a ROM de áudio");
        };
        saveRom(path);

        auto board = std::make_unique<Board>();
        board->insertCartridge(Cartridge::load(path));
        const auto run = [&board](unsigned cycles)
        {
            for (unsigned cycle = 0; cycle < cycles; ++cycle)
                board->clock();
        };
        const auto produced = [&board]()
        {
            const auto psgCycles = board->masterCycles() / Scheduler::Z80Divider;
            return psgCycles * Scheduler::Z80Divider * Board::AudioSampleRate / Scheduler::MasterClock *
                   Board::AudioChannels;
        };
        std::vector<float> samples;
        run(Scheduler::MasterClock / 10);
        board->takeAudio(samples);
        check(!samples.empty() && samples.size() == produced() && board->m68k().stopped(),
              "O PSG deve produzir PCM a 48 kHz mesmo com os processadores parados");
        const auto [minimum, maximum] = std::minmax_element(samples.begin(), samples.end());
        check(*minimum < -0.04f && *maximum > 0.04f, "O tom deve ter as duas polaridades no PCM");
        unsigned risingEdges = 0;
        for (std::size_t index = 0; index < samples.size(); index += Board::AudioChannels)
        {
            check(samples[index] == samples[index + 1], "O PSG mono deve chegar aos dois canais");
            if (index >= Board::AudioChannels)
                risingEdges += samples[index - Board::AudioChannels] <= 0 && samples[index] > 0;
        }
        check(risingEdges >= 43 && risingEdges <= 45, "A ROM deve gerar um tom de aproximadamente 440 Hz");

        const auto firstCount = samples.size();
        board->takeAudio(samples);
        check(samples.empty(), "A coleta deve esvaziar a fila PCM");
        run(234567);
        board->takeAudio(samples);
        check(firstCount + samples.size() == produced(), "A fase de amostragem deve sobreviver à coleta");

        run(10000); // Deixa áudio pendente antes de trocar a ROM.
        putWord(rom, 0x112, 0x9f);
        const auto mutedPath = directory / "psg-muted.md";
        saveRom(mutedPath);
        board->insertCartridge(Cartridge::load(mutedPath));
        board->takeAudio(samples);
        check(samples.empty(), "A troca de ROM deve descartar o PCM anterior");
        run(10000);
        board->takeAudio(samples);
        check(samples.size() == produced() &&
                  std::all_of(samples.begin(), samples.end(), [](float value)
                              { return value == 0.0f; }),
              "A troca de ROM deve reiniciar a fase e o filtro do PSG");
    }

    void testCartridgeReset(const std::filesystem::path &directory)
    {
        const auto path = directory / "m68k-vres.md";
        std::vector<std::uint8_t> rom(0x200);
        putWord(rom, 0x000, 0x00ff);
        putWord(rom, 0x002, 0xff00); // SSP = $00FFFF00.
        putWord(rom, 0x006, 0x0100);
        putWord(rom, 8 * 4 + 2, 0x0130); // Violação de privilégio.
        putWord(rom, 0x100, 0x7007);     // MOVEQ #7,D0
        putWord(rom, 0x102, 0x4e70);     // RESET
        putWord(rom, 0x104, 0x7209);     // MOVEQ #9,D1
        putWord(rom, 0x106, 0x4e73);     // RTE muda para modo usuário.
        putWord(rom, 0x120, 0x4e70);     // RESET em modo usuário gera vetor 8.
        putWord(rom, 0x130, 0x740b);     // MOVEQ #11,D2 no tratador.
        putWord(rom, 0x132, 0x60fe);
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()),
                       static_cast<std::streamsize>(rom.size()));
            check(bool(file), "Falha ao gravar a ROM de diagnóstico de /VRES");
        }

        auto storage = std::make_unique<Board>();
        auto &board = *storage;
        using Edge = std::pair<std::uint64_t, bool>;
        std::vector<Edge> edges;
        board.setCartridgeVresHandler([&](bool asserted)
                                      {
            check(board.cartridgeVresAsserted() == asserted, "Nível de /VRES durante a notificação");
            edges.emplace_back(board.masterCycles(), asserted); });
        board.insertCartridge(Cartridge::load(path));
        check(!board.cartridgeVresAsserted() && edges.empty(), "/VRES deve começar inativo");
        board.writeWord(0xffff00, 0x0000); // SR de usuário no quadro para RTE.
        board.writeWord(0xffff04, 0x0120);
        board.writeWord(0xffff20, 0xabcd);
        board.writeByte(0xa00000, 0x18); // JR -2 no Z80.
        board.writeByte(0xa00001, 0xfe);
        board.writeByte(0xa04000, 0x22);
        board.writeByte(0xa04001, 0x5a);
        board.writeByte(0xc00011, 0x92);
        board.writeByte(0xa10009, 0x40);   // Direção do pino TH da porta 1.
        board.writeWord(0xc00004, 0x8140); // Liga a exibição da VDP.
        board.writeByte(0xa11200, 1);      // Libera ZRES.
        runUntil(board, 1);
        const auto resetStart = board.masterCycles();
        constexpr auto divider = Scheduler::M68KDivider;
        // A pré-busca final recebe dois ciclos de espera da placa.
        for (unsigned cycle = 1; cycle <= 134 * divider; ++cycle)
        {
            board.clock();
            check(board.cartridgeVresAsserted() == (cycle >= 5 * divider && cycle < 129 * divider),
                  "/VRES deve manter o nível entre clocks do 68000");
        }
        check(edges == std::vector<Edge>{{resetStart + 5 * divider, true}, {resetStart + 129 * divider, false}},
              "O conector deve receber só as duas bordas do pulso de 124 ciclos");
        check(board.m68k().state().pc == 0x104 && board.m68k().state().data[0] == 7,
              "RESET deve preservar D0 e continuar na instrução seguinte");
        check(board.vdp().cycleInLine() == board.masterCycles() && board.vdp().registerValue(1) == 0x40 &&
                  board.z80().state().cycles == board.masterCycles() / Scheduler::Z80Divider &&
                  board.ym2612().registerValue(0, 0x22) == 0x5a && board.psg().registerValue(1) == 2 &&
                  board.readByte(0xa10009) == 0x40 && board.readWord(0xffff20) == 0xabcd &&
                  board.readMemory(1) == 0xfe,
              "A atividade do vídeo e do Z80 e o estado dos periféricos devem sobreviver a /VRES");

        runUntil(board, 5);
        check(board.m68k().state().data[1] == 9 && board.m68k().state().data[2] == 11 &&
                  board.readWord(0xffff04) == 0x0120,
              "A ROM deve continuar após RESET e entrar no vetor 8 em modo usuário");
        check(edges.size() == 2 && !board.cartridgeVresAsserted(),
              "RESET em modo usuário deve deixar /VRES inativo");

        board.insertCartridge(Cartridge::load(path));
        runUntil(board, 1);
        for (unsigned cycle = 0; cycle < 5 * divider; ++cycle)
            board.clock();
        check(board.cartridgeVresAsserted() && edges.size() == 3 && edges.back().second,
              "A nova execução deve emitir outro pulso");
        board.insertCartridge(Cartridge::load(path));
        check(!board.cartridgeVresAsserted() && edges.size() == 4 && !edges.back().second,
              "A troca de cartucho durante o pulso deve liberar /VRES");
    }

    void testZ80VerticalInterrupt(const std::filesystem::path &path)
    {
        auto storage = std::make_unique<Board>();
        auto &board = *storage;
        board.insertCartridge(Cartridge::load(path));
        // LD SP,$1FFF; IM 1; EI; HALT.
        const std::uint8_t program[] = {0x31, 0xff, 0x1f, 0xed, 0x56, 0xfb, 0x76};
        for (unsigned i = 0; i < sizeof program; ++i)
            board.writeMemory(i, program[i]);
        board.writeMemory(0x38, 0x3e); // LD A,$37 no tratador de IM 1.
        board.writeMemory(0x39, 0x37);
        board.writeMemory(0x3a, 0x76);
        board.writeByte(0xa11200, 1);

        constexpr auto vblankStart = VDP::visibleLines * VDP::masterCyclesPerLine;
        while (board.masterCycles() < vblankStart + VDP::verticalInterruptCycle)
            board.clock();
        check(board.z80().state().halted && !board.z80().state().irq && board.z80().state().a == 0,
              "O Z80 deve continuar em HALT antes do disparo do V-INT");

        while (board.masterCycles() < vblankStart + VDP::verticalInterruptCycle + 600)
            board.clock();
        check(board.z80().state().irq && board.z80().state().halted && board.z80().state().a == 0x37 &&
                  board.vdp().interruptLevel() == 0,
              "O V-INT deve acordar o Z80 mesmo com a interrupção do 68000 desabilitada");
        check(board.z80().state().sp == 0x1ffd && board.readMemory(0x1ffd) == 7,
              "O Z80 empilha o PC posterior ao HALT");

        while (board.masterCycles() < vblankStart + VDP::masterCyclesPerLine + Scheduler::Z80Divider)
            board.clock();
        check(!board.z80().state().irq, "O pulso de INT termina ao sair da primeira linha de VBlank");
    }

    void testZ80UnmappedPorts(const std::filesystem::path &path)
    {
        auto board = std::make_unique<Board>();
        board->insertCartridge(Cartridge::load(path));
        // LD A,$42; OUT ($80),A; IN A,($80); HALT.
        const std::uint8_t program[] = {0x3e, 0x42, 0xd3, 0x80, 0xdb, 0x80, 0x76};
        for (unsigned i = 0; i < sizeof program; ++i)
            board->writeMemory(i, program[i]);
        board->writeByte(0xa11200, 1);
        for (unsigned cycle = 0; cycle < 600; ++cycle)
            board->clock();
        check(board->z80().state().halted && board->z80().state().a == 0xff,
              "As portas desmapeadas do Genesis ignoram OUT e devolvem $FF em IN");
    }
}

int main(int argc, char **argv)
{
    try
    {
        check(argc == 2, "Diretório de saída da ROM ausente");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        testPsgAudio(directory);
        testPeriodicBusWait(directory);
        testCartridgeReset(directory);
        const auto path = directory / "m68k-diagnostic.md";
        std::vector<std::uint8_t> rom(0x200, 0);
        putWord(rom, 0x000, 0x00ff); // SSP = $00FFFF00, dentro da Work RAM.
        putWord(rom, 0x002, 0xff00);
        putWord(rom, 0x004, 0x0000); // PC inicial = $00000100.
        putWord(rom, 0x006, 0x0100);
        putWord(rom, 0x100, 0x41f8); // LEA $FF20.W,A0
        putWord(rom, 0x102, 0xff20);
        putWord(rom, 0x104, 0x7006); // MOVEQ #6,D0
        putWord(rom, 0x106, 0x7207); // MOVEQ #7,D1
        putWord(rom, 0x108, 0xc0c1); // MULU.W D1,D0: 42
        putWord(rom, 0x10a, 0x30c0); // MOVE.W D0,(A0)+
        putWord(rom, 0x10c, 0x7202); // MOVEQ #2,D1
        putWord(rom, 0x10e, 0x80c1); // DIVU.W D1,D0: 21
        putWord(rom, 0x110, 0x30c0); // MOVE.W D0,(A0)+
        putWord(rom, 0x112, 0x74fd); // MOVEQ #-3,D2
        putWord(rom, 0x114, 0xc1c2); // MULS.W D2,D0: -63
        putWord(rom, 0x116, 0x2080); // MOVE.L D0,(A0)
        putWord(rom, 0x118, 0x0c80); // CMPI.L #-63,D0
        putWord(rom, 0x11a, 0xffff);
        putWord(rom, 0x11c, 0xffc1);
        putWord(rom, 0x11e, 0x6604); // BNE.S para o caminho de falha.
        putWord(rom, 0x120, 0x7601); // Sucesso: D3 = 1.
        putWord(rom, 0x122, 0x6002); // Salta o caminho de falha.
        putWord(rom, 0x124, 0x7600); // Falha: D3 = 0.
        putWord(rom, 0x126, 0x60fe); // Laço final.
        putWord(rom, 0x128, 0x4e71);
        {
            std::ofstream file(path, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()),
                       static_cast<std::streamsize>(rom.size()));
            check(bool(file), "Falha ao gravar a ROM de diagnóstico");
        }
        testZ80VerticalInterrupt(path);
        testZ80UnmappedPorts(path);

        Board board;
        board.insertCartridge(Cartridge::load(path));
        runUntil(board, 5);
        check(board.readWord(0xffff20) == 42 && board.m68k().state().address[0] == 0xffffff22,
              "MULU e primeira escrita na Work RAM falharam");
        runUntil(board, 8);
        check(board.readWord(0xffff22) == 21 && board.m68k().state().data[0] == 21,
              "DIVU e segunda escrita na Work RAM falharam");
        runUntil(board, 11);
        check(board.m68k().state().pc == 0x118 &&
                  board.m68k().state().data[0] == 0xffffffc1u &&
                  board.readWord(0xffff24) == 0xffff &&
                  board.readWord(0xffff26) == 0xffc1,
              "MULS e escrita longa na Work RAM falharam");
        runUntil(board, 12);
        check((board.m68k().state().sr & 0x04) != 0 &&
                  board.m68k().state().data[0] == 0xffffffc1u,
              "CMPI.L alterou D0 ou não marcou a igualdade");
        runUntil(board, 16);
        check(board.m68k().state().pc == 0x126 && board.m68k().state().data[3] == 1 &&
                  board.readWord(0xffff20) == 42 && board.readWord(0xffff22) == 21 &&
                  board.readWord(0xffff26) == 0xffc1,
              "BNE não preservou o caminho de sucesso da ROM");
        putWord(rom, 0x11c, 0xffc2); // Uma comparação diferente deve tomar BNE.
        const auto mismatchPath = directory / "m68k-diagnostic-mismatch.md";
        {
            std::ofstream file(mismatchPath, std::ios::binary);
            file.write(reinterpret_cast<const char *>(rom.data()),
                       static_cast<std::streamsize>(rom.size()));
            check(bool(file), "Falha ao gravar a variante da ROM");
        }
        Board mismatchBoard;
        mismatchBoard.insertCartridge(Cartridge::load(mismatchPath));
        runUntil(mismatchBoard, 12);
        check((mismatchBoard.m68k().state().sr & 0x04) == 0,
              "CMPI.L marcou igualdade para valores diferentes");
        runUntil(mismatchBoard, 13);
        check(mismatchBoard.m68k().state().pc == 0x124,
              "BNE não desviou para o caminho de falha da ROM");
        runUntil(mismatchBoard, 14);
        check(mismatchBoard.m68k().state().pc == 0x126 &&
                  mismatchBoard.m68k().state().data[3] == 0,
              "O caminho de falha da ROM não concluiu com D3 zerado");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}