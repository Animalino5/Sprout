# Sprout

**A tiny BASIC compiler for the Nintendo 3DS.**

Sprout turns `.bas` files into native 3DS homebrew (`.3dsx`)! no C required. Write BASIC, run on real hardware or in an emulator. Designed for beginners and anyone who wants to make a game without fighting a build system.

## Why Sprout?

- **BASIC, not C.** No pointers, no headers, no `malloc`. Just simple functions like `IF`, `TEXT`, `DRAWIMAGE` and more!
- **Native 3DS output.** Real GPU via citro2d, real audio via ndsp, real button/touch/circle-pad input. Not an emulator sandbox.
- **One command to build.** `./scripts/build_3ds.sh my_game.bas` → `.3dsx` you can run.
- **Asset pipeline included.** Drop PNGs and WAVs in `assets/` — Sprout converts them to `.t3x` and bundles them as RomFS automatically.

## Features

| Area        | What you get                                                    |
| ----------- | --------------------------------------------------------------- |
| Language    | Variables (int/float/string), arrays, `FUNCTION`/`END FUNCTION`, `IF/ELSEIF/ELSE`, `WHILE`, `FOR/NEXT`, `DO/LOOP`, `IMPORT` for multi-file projects |
| Graphics    | `CLS`, `PIXEL`, `LINE`, `RECT`, `CIRCLE`, `TRIANGLE`, `ELLIPSE`, `TEXT`, `TEXTSIZE`, custom TTF fonts via `LOADFONT`/`FONT` |
| Transforms  | `TRANSLATE`, `ROTATE`, `RESIZE` — global affine transform applied to all draws |
| Images      | `LOADIMAGE`, `DRAWIMAGE`, `DRAWIMAGEEX` (scale + rotate), `LOADSHEET` + `DRAWSHEET` for sprite-sheet animation |
| Dual screen  | `SETSCREEN 0` (top 400×240), `SETSCREEN 1` (bottom 320×240, touch-enabled) |
| Input       | `BUTTON`, `BUTTONDOWN`, `BUTTONUP`, `TOUCHDOWN`, `GETTOUCH`, `GETCIRCLEPAD` |
| Audio       | `LOADSOUND`/`PLAYSOUND` (WAV, in RAM), `LOADMUSIC`/`PLAYMUSIC` (streamed), `VOLMUSIC`, `PAUSEMUSIC` |
| Files       | `OPENW`/`OPENR`/`WRITE`/`READNUM`/`READLINE$`/`CLOSE` -- save games on SD card |
| Math/Strings| `ABS`, `SIN`, `COS`, `SQR`, `RND`, `RNDF`, `LEN`, `LEFT$`, `MID$`, `STR$`, `UCASE$`, `INSTR`, ... |

## Quick start

### 1. Install devkitPro

You need the 3DS toolchain. Follow the official guide at <https://devkitpro.org/wiki/Getting_Started>, then install:

```bash
sudo dkp-pacman -S 3ds-dev 3ds-tools
```

Make sure `DEVKITPRO` and `DEVKITARM` env vars are set (the installer does this).

### 2. Build the compiler

```bash
make sproutc
```

That's it — `./sproutc` is a normal host binary (gcc, no special deps).

### 3. Compile a BASIC program to a 3DS app

```bash
./scripts/build_3ds.sh examples/function/hello.bas
```

Output lands in `build-3ds/hello.3dsx`.

## A first program

```basic
' hello.bas, your first Sprout program
CLS RGB(20, 20, 50)
COLOR RGB(100, 255, 200)
TEXTSIZE 2
TEXT 100, 40, "Hello, Sprout!"
TEXTSIZE 1

WHILE TRUE
    IF BUTTONDOWN(START) THEN END
    WAITFRAME
WEND
```

## Examples

The `examples/` folder is half the documentation, read it in order:

| #  | File                  | What it teaches                                             |
| -- | --------------------- | ----------------------------------------------------------- |
| 01 | `hello.bas`        | Minimal program, `TEXT`, main loop                          |
| 02 | `variables.bas`    | Variables, type inference, math, string concatenation       |
| 03 | `input.bas`        | Buttons, touch, circle pad                                  |
| 04 | `drawing.bas`      | `CLS`, `COLOR`, `RGB`, all shape primitives, `TEXTSIZE`     |
| 05 | `images.bas`       | `LOADIMAGE`, `DRAWIMAGE`, `DRAWIMAGEEX`, image dimensions   |
| 06 | `spritesheets.bas` | Sprite sheets and frame animation                           |
| 07 | `audio.bas`        | Sound effects and music                                     |
| 08 | `files.bas`        | Save/load with sequential file I/O                          |
| 09 | `dualscreen.bas`   | Drawing to both top and bottom screens                      |
| 10 | `multifile.bas`    | `IMPORT` to split a project across multiple `.bas` files    |
| 11 | `transforms.bas`   | `TRANSLATE`, `ROTATE`, `RESIZE` (global transform state)    |

## Games

Two complete games live in `examples/complex_samples`

- **`neondrifter/neon_drifter.bas`** — A polished dual-screen shoot-em-up. Demonstrates particles, screen shake via `TRANSLATE`, state machines, music streaming.
- **`chocolatecatch/cc_finished.bas`** — A touch-based catch game. Good example of touch input, sprite animation, and a simple menu.

Each game has its own `assets/` folder and a `config.txt` for app metadata (name, description, author).

## Project structure

```
sprout/
├── src/              Compiler source (C)
│   ├── lexer.c       Tokenizer
│   ├── parser.c      Recursive-descent parser → AST
│   ├── typecheck.c   Type inference + builtin signatures
│   ├── emit.c        AST → C99 emitter
│   └── sproutc.c     CLI driver
├── runtime/
│   ├── nb_runtime.h  Public API (what BASIC calls compile to)
│   ├── 3ds/          3DS implementation (citro2d + ndsp + libctru)
├── examples/         11 example .bas files + 2 games
├── scripts/
│   └── build_3ds.sh  .bas → .3dsx pipeline (PNG→t3x, TTF→bcfnt, RomFS)
├── tests/            Lexer + parser test drivers
└── Makefile          Builds sproutc + host runtime for dev
```

## Customizing your app

Drop a `config.txt` next to your `.bas` file to set 3DS homebrew metadata:

```ini
name=My Cool Game
description=A game made with Sprout
author=Your Name
```

Put an `icon.png` (48×48) in `assets/` and it becomes the homebrew icon.

## Games made with Sprout!

To have your game featured here, please make a Github issue!

## License

MIT - see [LICENSE](LICENSE). Sprout is free for any use, including commercial. If you ship something made with it, a credit is appreciated but not required.

## Contributing

Bug reports, feature ideas, and example/game submissions are welcome. The codebase is small on purpose! read `src/emit.c` first, it's the most interesting file.
