# Changelog

Todas as mudanças notáveis do R2MEGA são documentadas aqui.

## v0.2.1 — Menu File

- `File > Recent Files` reabre até 10 ROMs carregadas com sucesso, da mais recente à mais antiga, sem duplicatas.
- `File > Open...` e `Ctrl+O` abrem o seletor na pasta da última ROM carregada. O histórico e a pasta ficam em `resources/config.ini` e são restaurados ao iniciar o emulador.
- `File > Reset` ou `F12` reinicia o jogo usando a ROM já carregada em memória.
- `File > Unload` descarrega a ROM, encerra a reprodução do áudio e volta à tela inicial, mantendo o histórico disponível.

## v0.2.0 — Áudio

- Reprodução de áudio estéreo a 48 kHz pelo SDL3, com o PSG SN76489 e a síntese FM e o DAC do YM2612 via ymfm.
- Emulação dos temporizadores e do estado `BUSY` do YM2612, com espelhamento das portas nos barramentos do M68000 e do Z80.
- Amostragem sincronizada com o relógio do console e descarte do áudio enfileirado ao pausar, trocar de ROM ou alternar o avanço rápido.
- Testes de síntese e temporização do YM2612, da fila de áudio do SDL3 e da geração de um tom pelo PSG em uma ROM de diagnóstico.
- Inclusão da licença BSD do ymfm nos pacotes para Windows e Linux.

## v0.1.0 — M68000

- Correções no ajuste decimal, nos indicadores do CCR e na temporização de `ABCD`.
- Correção do pós-incremento em `CMPM.L` quando a primeira leitura usa um endereço ímpar.
- Correções de temporização e do tratamento de acessos desalinhados em `MOVE` para `SR`, `RTE` e `RTR`.
- Ajuste das esperas periódicas do barramento do M68000, incluindo acessos à ROM e interação com o DMA do VDP.
- Testes de regressão das instruções corrigidas e ROM de diagnóstico para verificar as esperas do barramento.

### Funcionalidades de base

- Janela SDL3 com navegador de ROMs e os comandos `File > Open...` e `File > Exit`.
- Carregamento de ROMs em memória, incluindo arquivos ZIP e conversão de SMD.
- Controles de três botões para dois jogadores, com teclado, gamepads SDL e remapeamento persistente.
