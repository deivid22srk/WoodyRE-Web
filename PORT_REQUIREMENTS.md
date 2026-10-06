# PORT_REQUIREMENTS.md — Requisitos e reconhecimento para o port web (FASE 0)

Resposta objetiva às perguntas da Fase 0 para o projeto **WoodyRE**
(https://github.com/jjmhalew/WoodyRE.git — versão analisada: `master` @ `c931399`, "Version 1.0.4").

## O que é o projeto

Reimplementação em **C99** do engine de *Woody Woodpecker: Escape from Buzz Buzzard Park*
(PC, Eko Software / Cryo, 2001), feita por engenharia reversa. Licença **GPL-3.0**. O jogo em si
não está no repositório: o port lê os arquivos **do CD original do usuário** e os verifica por
SHA-1 contra o manifesto `src/datafiles.h` (232 arquivos, ~638 MB — apenas metadados, sem
conteúdo protegido).

## 1. Para INICIAR o port (ler, entender e modificar o código), é preciso alguma ISO/asset?

**Não.** O código-fonte é completo e autocontido: engine, plataformas, ferramentas de análise e
documentação extensa em `docs/`. Nenhuma ISO, ROM ou asset é necessário para estudar ou modificar
o código. Para **testar de verdade** (jogar), é necessário o conteúdo do CD original
(`Data/`, `Common/`, `Logo/`, `Game/`, `Music.bf`) — que não temos e não devemos obter
(Copyright; regra 0.4).

## 2. Para COMPILAR a versão web (CI) é preciso alguma ISO/asset?

**Não.** O build web compila só código C + SDL2 (via Emscripten). Nenhuma etapa de build lê
assets do jogo. Os arquivos do CD são necessários apenas para **executar**, e no navegador o
**usuário fornece os dele** via `<input type="file">` (ISO ou pasta do CD), como já acontece no
port Android (file picker do sistema).

## 3. Ferramentas, SDKs e dependências

| Componente | Detalhe |
|---|---|
| Linguagem | C99 puro (`-std=gnu99`), ~22,5 mil linhas em `src/`. Sem C++ |
| Build nativo | Windows: `build.bat` (Zig cc) · Linux: `build.sh` (cc + SDL2) · Android: Gradle + NDK 27.2 + CMake 3.22 |
| SDL2 | `build.sh` usa a do sistema; Android baixa SDL 2.32.10 com hash verificado |
| Gráficos | OpenGL 1.1 fixed-function (Windows/Linux) ou **OpenGL ES 2.0/3.0 via `src/gles/`** (Android) — emulação de fixed-function em shaders |
| Áudio | SDL2 audio puro (`SDL_OpenAudioDevice` + `SDL_QueueAudio`) |
| Input | SDL2 (teclado, mouse, gamepad) + `src/touch.c` (controles de toque, já existentes) |
| `stb_image` / `stb_image_write` | Embutidos em `src/stb/` (domínio público/MIT) |
| Threads | **Nenhuma** no engine (pthread só no log do Android, `#ifdef __ANDROID__`) |
| Rede/sockets | **Nenhuma** |
| Build web (novo) | Emscripten **3.1.74** (fixado), port SDL2 do próprio Emscripten, sem dependências extras |

Ponto de entrada: `main()` em `src/main_engine.c` (linha 3772) → loop principal em
`while (!win.quit)` (linha 3890). Camadas de plataforma isoladas: `src/plat.h` (shims),
`src/plat_sdl.c` (janela/input SDL), `src/datasetup_posix.c` (busca/cópia dos dados do CD).

## 4. O projeto compila sem os arquivos proprietários?

**Sim, integralmente** — em todas as plataformas. Sem os arquivos do CD o que **não funciona** é
só a execução: o jogo precisa de `Data/` (níveis), `Common/` (personagens), `Logo/` (filmes de
abertura), `Game/` (`mask.bin`) e `Music.bf` (músicas, 306 MB). No primeiro start o port procura o
CD/ISO e copia os 232 arquivos do manifesto para a pasta de dados, verificando cada SHA-1
(`data_find()` em `src/datasetup_posix.c`; no Android, lê **imagem ISO diretamente** com um
parser ISO9660/Joliet incluído no próprio código).

## 5. O projeto é viável no navegador? Riscos

**Sim, viável com risco baixo**, porque o port Android já percorreu o caminho difícil
(GLES2 + touch + dados via picker):

| Item | Avaliação |
|---|---|
| Gráficos | ✅ Caminho GLES2 (`src/gles/gles2.c`) já existe → WebGL2 via `-sUSE_WEBGL=2` |
| Áudio | ✅ SDL2 → Web Audio. Exige gesto do usuário (o botão "Jogar" resolve) |
| Loop principal | ⚠️ `while(!win.quit)` + `Sleep()` no frame cap e nos filmes de abertura → resolvido com `-sASYNCIFY` (`Sleep` → `emscripten_sleep`), sem reestruturar o código |
| Threads | ✅ Nenhuma → **não** precisa de COOP/COEP → GitHub Pages funciona sem service worker |
| Input | ✅ SDL2 no Emscripten cobre teclado/mouse/Gamepad API; `touch.c` já faz os controles móveis |
| Arquivos | ⚠️ Ponto crítico: ~638 MB de dados. Estratégia: usuário fornece ISO/pasta → extração pelo código C existente → persistência em **IndexedDB (IDBFS)**. Na primeira visita, pico de memória ~700 MB (dados em memória durante a sessão) — desktop OK, celular fraco pode não abrir |
| Tamanho do binário | ✅ ~2–4 MB WASM + SDL2 (gzip no Pages reduz a ~1 MB) |
| Performance | ✅ Jogo de 2001; WebGL2 + WASM dão folga. Custo do ASYNCIFY aceitável (unwind só nos pontos de yield) |
| Persistência | ⚠️ Quota do IndexedDB (~640 MB): Chrome/Firefox desktop suportam; se falhar, o jogo roda só na sessão (degradação graciosa) |

## Decisões da Fase 1 (estratégia de port)

- **Stack**: C/C++ + SDL2 → **Emscripten** (`-sUSE_SDL=2`, WebGL2), fonte única com
  `#ifdef __EMSCRIPTEN__` seguindo o padrão `__ANDROID__` existente. Build nativo intocado.
- **Sistema de arquivos**: MEMFS em execução + **IDBFS** montado em `/woody` (persistência de
  `data/`, `woodyre.cfg`, `woodyre.sav`). Nada é enviado a servidores; tudo fica no navegador.
- **Loop principal**: `emscripten_sleep` (ASYNCIFY) — mantém o `while (!win.quit)` e os loops de
  filme do código original.
- **Dados do jogo**: picker local (ISO — reusa o parser ISO9660 do projeto; ou pasta do CD via
  `webkitdirectory`) → verificação SHA-1 (código existente) → IDBFS. **Nenhuma ISO no CI nem no
  repositório.**
- **Página**: `web/shell.html` → `index.html` com barra de progresso, seletor de arquivos,
  tela cheia, aviso legal e log; caminhos 100% relativos.
- **CI**: `.github/workflows/build-web.yml` com emsdk **3.1.74 fixado**, cache do toolchain,
  `.nojekyll` e deploy no GitHub Pages.

## Riscos/pendências conhecidos

1. **Memória**: os dados ficam residentes em memória na sessão (MEMFS/IDBFS). Otimização futura:
   WASMFS+OPFS (streaming) — fora do escopo do MVP.
2. **Mobile**: navegadores móveis podem não segurar ~700 MB; a página avisa. Controles de toque
   existem, mas o teste real em celular ficou como item de verificação pós-deploy.
3. **Não testamos gameplay** (não temos o CD): o port foi validado até a inicialização do engine
   e a tela de fornecimento de dados; o fluxo de extração reusa o código já provado no Android.
