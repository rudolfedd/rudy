# ┌─ rudy /

## ├─ What is it
      Minimal nano-like text editor for the terminal.
      Pure C11 + POSIX, zero external dependencies.

## ├─ Features
      🎨 Syntax highlighting — C, C++, C#, Rust, NASM, Java (auto by extension)
      ↩️ Undo / redo with insertion coalescing
      🔍 Regex search and replace, go to line
      📋 Cut / copy / paste — line and selection, selection by Shift+arrows
      🧠 Auto and smart indent, comment toggle (Alt+M)
      💻 Command line `:` with vi-like commands
      💾 Autosave and crash recovery (`:recover`)
      🌍 UTF-8 aware movement, line numbers, visible whitespace
      📦 Single binary, no runtime dependencies

## ├─ How build
 1. install `gcc` (or `cc`) and `make`
 2. `git clone https://github.com/rudolfedd/rudy.git && cd rudy`
 3. `make` — build `./rudy`
 4. `make test` — run the test suite (26 tests)
 5. `sudo make install` — install into `$PREFIX/bin`, `sudo make uninstall` to remove
 6. `make debug` — build with AddressSanitizer + UndefinedBehaviorSanitizer

      Windows — MSYS2 shell: `./build.sh` builds `rudy.exe`, `./run_tests.sh` runs the tests.

## ├─ How run
      rudy [options] [file]

      -l, --language LANG   syntax language: c, cpp, csharp, rust, nasm, java
      --no-syntax           disable syntax highlighting
      --tabsize=N           tab width, 1-16 (default 4)
      -h, --help            show help
      -v, --version         show version

      Language is detected by file extension, `:` sets it manually.

## ├─ Keys · file
      ^O  F2        — open file
      ^S  F3        — save file
      ^X  ^Q        — quit, asks to save when modified
      ^R  F4        — search and replace

## ├─ Keys · edit
      ^Z            — undo
      Alt+Z         — redo
      ^K  F7        — cut line or selection
      ^Y  F6        — copy line or selection
      ^U  F8        — paste
      Alt+M         — toggle line comment
      ^D  Del       — delete character
      ^Space        — select all

## ├─ Keys · navigation
      Arrows        — move cursor
      Shift+Arrows  — extend selection
      ^← ^→         — move by word (Alt+B / Alt+F)
      ^A  Home      — start of line
      ^E  End       — end of line
      ^_  F5        — go to line
      PgUp PgDn     — page up / down

## ├─ Keys · search and view
      ^W            — search (POSIX extended regex)
      ^F  Alt+W     — next match
      Alt+Shift+W   — previous match
      ^T            — toggle syntax highlighting
      Alt+L         — toggle line numbers
      Alt+U         — toggle visible whitespace
      ^L            — clear the status message
      ^G  F1        — help screen
      Alt+C         — command line

## ├─ Commands (:)
      :w [file]       — save, or save as
      :q  :q!         — quit, quit without saving
      :wq  :x         — save and quit
      :o  :e <file>   — open file
      :s/pat/rep/g    — replace all matches by regex
      :set syntax=…   — c, cpp, csharp, rust, nasm, java, off
      :set tabsize=N  — tab width
      :set numbers    — toggle line numbers
      :recover        — restore the autosaved file
      :help           — show the help screen

## ├─ Languages
      c  h                 — C
      cpp  cxx  cc  hpp    — C++
      cs                   — C#
      rs                   — Rust
      asm  nasm            — NASM
      java                 — Java
      txt  md  json  conf… — plain text

## ├─ Project structure
      rudy/
      ├── src/
      │   ├── main.c       — editor loop, key handling, commands
      │   ├── screen.c     — terminal setup, rendering, status line
      │   ├── buffer.c     — text buffer and undo stack
      │   ├── edit.c       — editing, undo/redo, clipboard, cursor
      │   ├── files.c      — file read/write, autosave, recovery
      │   ├── search.c     — regular expression search
      │   ├── highlight.c  — tokenizer and highlighting cache
      │   ├── syntax.c     — language definitions and keyword tables
      │   ├── util.c       — strings, word sets, UTF-8 helpers
      │   └── rudy.h       — shared types and prototypes
      ├── test/
      │   └── run_tests.sh — test suite, `make test`
      ├── Makefile
      ├── build.sh         — MSYS2 build wrapper
      ├── run_tests.sh     — MSYS2 test wrapper
      └── README.md

## └─ Author = rudolfedd
