# R2MEGA

Emulador de Sega Mega Drive / Genesis em C++17, em desenvolvimento. O núcleo
inclui M68000, Z80, VDP, PSG, YM2612 e controles de três botões. A precisão de
ciclos e a renderização dot a dot ainda estão em evolução.

## Compilação

Requer CMake 3.16+, compilador C++17 e Git. A configuração gráfica obtém zlib,
SDL3 e Dear ImGui na primeira execução:

```sh
cmake -S . -B build -DR2MEGA_BUILD_GUI=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Para compilar somente o núcleo, use `-DR2MEGA_BUILD_GUI=OFF`.

## Auditoria do MC68000

O alvo `m68k_vector_runner` executa os vetores independentes com a CPU isolada.
Baixe os dois arquivos ZIP nas revisões usadas pela auditoria e salve-os como
`build/680x0.zip` e `build/m68000.zip`.
[SingleStepTests/680x0](https://github.com/SingleStepTests/680x0/archive/e0d5ece9670205cc84a0101081837deb446f86a3.zip)
e [SingleStepTests/m68000](https://github.com/SingleStepTests/m68000/archive/64b253116a3de04aaac4346c43680960dc9b67e5.zip).

```sh
cmake --build build --target m68k_vector_runner --config Debug
python tests/m68000_matrix.py --runner build/m68k_vector_runner --680x0 build/680x0.zip --mame build/m68000.zip --output build/m68000.csv
```

Em Windows, passe o caminho do `m68k_vector_runner.exe`, na subpasta `Debug`
se o gerador CMake usar múltiplas configurações. Use `--opcode 4e76` para
executar apenas os casos de um opcode. O CSV contém as 65.536 palavras, os
totais por fonte e o primeiro caso divergente por opcode.

## ROMs e controles

`File > Open...` ou `Ctrl+O` carrega arquivos `.md`, `.gen`, `.bin`, `.smd` e
arquivos `.zip` com uma ROM compatível. O controle padrão de ambos os jogadores
é o teclado:

| Função | Jogador 1 | Jogador 2 |
| --- | --- | --- |
| Direções | W, S, A, D | Setas |
| A | J | Z |
| B | K | X |
| C | L | C |
| Start | Enter | 1 |

Em `Input > Player Controller`, cada jogador pode escolher `Keyboard` ou
`Joystick`. Os gamepads SDL usam D-pad ou analógico esquerdo para as direções,
Oeste para A, Sul para B, Leste para C e Start para Start. O primeiro gamepad
conectado atende o primeiro jogador que estiver configurado como Joystick.

`Input > Configure Keyboard...` altera as teclas dos dois jogadores. `Save`
grava o mapeamento em `resources/keyboard.ini`, relativo ao diretório de
trabalho; `Default` restaura o padrão no diálogo.

## Licença

GNU GPL v3.0. Consulte [LICENSE](LICENSE).
