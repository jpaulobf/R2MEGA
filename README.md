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

## Pacotes de release

O workflow em `.github/workflows/release.yml` compila e testa Windows x64 e
Linux x64. A execução manual pela aba Actions disponibiliza os dois pacotes
como artefatos. Uma tag `vX.Y.Z` correspondente à versão declarada no
`CMakeLists.txt` publica os pacotes na página Releases após os testes passarem.
Por exemplo, para a versão atual, a tag é `v0.0.3`.

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
