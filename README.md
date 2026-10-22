# rudy

A minimal, nano-like terminal text editor written in pure C (C11) with POSIX APIs. No external dependencies.

## Features

- Syntax highlighting for C, C++, C#, Rust, NASM, Java (auto-detected by file extension)
- Undo/redo with insertion coalescing
- Cut/copy/paste (line-based and selection)
- Selection with Shift+arrows
- Incremental search with regex (POSIX extended regex)
- Search and replace (`^R`, or `:s/pattern/replacement/g`)
- Go to line (`^_`)
- Auto-indentation and smart indent on `Enter`
- Line/block commenting (`Alt+M`)
- Line numbers, visible whitespace toggle
- Toggle syntax highlighting on/off
- Command line (`:`) with common vi-like commands
- Autosave to `$XDG_CACHE_HOME/rudy/` (or `~/.cache/rudy/`), recovery via `:recover`
- UTF-8 aware movement
- Works in a plain terminal (raw mode + alternate screen)

## Build

Requirements: POSIX environment with `gcc`/`cc`, `make`, standard headers (`termios.h`, `sys/ioctl.h`, `regex.h`).

```sh
make
```

Debug build with sanitizers:

```sh
make debug
```

## Install / Uninstall

```sh
sudo make install
sudo make uninstall
```

Override paths if needed: `make install PREFIX=$HOME/.local BINDIR=$HOME/.local/bin`

## Usage

```sh
rudy [options] [file]
```

Options:
- `-l, --language LANG`  set syntax language (`c`, `cpp`, `csharp`, `rust`, `nasm`, `java`)
- `--no-syntax`          disable syntax highlighting
- `--tabsize=N`          tab width (1-16, default 4)
- `-h, --help`
- `-v, --version`

## Key Bindings

### File
| Key | Action |
|---|---|
| `^O` / `F2` | Open file |
| `^S` / `F3` | Save |
| `^X`, `^Q` / `F9`,`F10` | Quit (prompts to save if modified) |
| `^R` / `F4` | Search and replace |

### Edit
| Key | Action |
|---|---|
| `^Z` | Undo |
| `Alt+Z` | Redo |
| `^K` / `F7` | Cut line or selection |
| `^Y` / `F6` | Copy line or selection |
| `^U` / `F8` | Paste |
| `Alt+M` | Toggle line comment |
| `^D` / `Del` | Delete character |
| `Backspace` | Backspace |
| `^A` / `Home` | Start of line |
| `^E` / `End` | End of line |
| `^Space` | Select all |

### Movement / Search
| Key | Action |
|---|---|
| Arrows | Move cursor |
| `Shift+Arrows` | Extend selection |
| `^←/^→` or `Alt+B/Alt+F` | Move by word |
| `^W` | Search (regex) |
| `^F` / `Alt+W` | Next match |
| `Alt+Shift+W` | Previous match |
| `^_` / `F5` | Go to line |
| `PgUp/PgDn`, `^↑/^↓` | Page up/down |

### View / Misc
| Key | Action |
|---|---|
| `^T` | Toggle syntax highlighting |
| `Alt+L` | Toggle line numbers |
| `Alt+U` | Toggle visible whitespace (spaces/tabs) |
| `^L` | Clear status message |
| `^G` / `F1` | Help |
| `Alt+C` | Command line |

## Commands (`:`)

| Command | Action |
|---|---|
| `:w` | Save |
| `:w <file>` | Save as |
| `:q` | Quit |
| `:q!` | Quit without saving |
| `:wq` or `:x` | Save and quit |
| `:o <file>` / `:e <file>` | Open file |
| `:s/pattern/replace/g` | Replace all matches by regex |
| `:set syntax=<lang\|off>` | Change syntax highlighting |
| `:set tabsize=<n>` | Set tab width |
| `:set numbers` / `:set nonumbers` | Toggle line numbers |
| `:recover` | Recover from autosave |
| `:help` | Show help |

## Languages

Auto-detected by extension: `c,h` (C), `cpp,cxx,cc,c++,hpp,hxx,hh,h++` (C++), `cs,csx` (C#), `rs` (Rust), `asm,nasm` (NASM), `java` (Java), `txt,log,md,conf,cfg,ini,toml,json,yml,yaml` (Text).

## Tests

```sh
make test
```
