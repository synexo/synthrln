# CLAUDE.md — SYNTHRLN Developer Guide

This file provides context for AI-assisted development (Claude Code or similar tools) as well as human contributors. Read it before modifying any source file.

---

## Project Summary

SYNTHRLN is a C99 BBS-to-TCP bridge. It has no dependencies on any runtime library beyond the C standard library, POSIX sockets (Linux), Winsock2 (Windows), and mTCP (DOS). It must compile cleanly on GCC, Clang, MinGW-w64, MSVC, and OpenWatcom.

The entire codebase is intentionally small and straightforward. Favour clarity and portability over cleverness.

---

## File Map

```
src/
  synthrln.h      Shared types: app_config_t, session_context_t, constants
  main.c          Entry point. Owns: CLI parsing dispatch, rlogin handshake,
                  Telnet IAC filter state machine, shuttle loop, graceful shutdown
  config.c        Config file (Key=Value) parser + CLI override handler
  config.h        config_load(), config_dump() declarations
  dropfile.c      DOOR32.SYS and DORINFO1.DEF parsers → session_context_t
  dropfile.h      dropfile_parse(), dropfile_exists() declarations

  bbs_io.h        Abstract BBS-side interface (6 functions)
  bbs_posix.c     Linux + Windows implementation of bbs_io.h
                  Handles BOTH regular BBS mode and --local mode at runtime
  bbs_fossil.c    DOS-only FOSSIL driver implementation of bbs_io.h
  bbs_local.c     DOS-only --local mode (conio.h) — separate from bbs_fossil.c
                  because on DOS, local and FOSSIL are mutually exclusive builds

  net_io.h        Abstract network-side interface (5 functions)
  net_posix.c     Linux + Windows TCP implementation of net_io.h
  net_mtcp.c      DOS mTCP implementation of net_io.h
```

### Which files compile for each platform

| File | Linux | Windows | DOS |
|---|:---:|:---:|:---:|
| `main.c` | ✓ | ✓ | ✓ |
| `config.c` | ✓ | ✓ | ✓ |
| `dropfile.c` | ✓ | ✓ | ✓ |
| `bbs_posix.c` | ✓ | ✓ | — |
| `bbs_fossil.c` | — | — | ✓ |
| `bbs_local.c` | — | — | ✓ |
| `net_posix.c` | ✓ | ✓ | — |
| `net_mtcp.c` | — | — | ✓ |

`bbs_posix.c` handles both regular BBS I/O and `--local` mode on Linux/Windows via runtime dispatch on `cfg->local_mode`. Do **not** split these back into two files — it caused duplicate symbol link errors.

---

## Core Invariants

These must hold at all times. Do not violate them.

1. **8-bit clean buffer passing.** The shuttle buffer is `uint8_t[1024]`. Never cast to `char *` inside the loop, never pass through `printf`/`puts`, never interpret bytes as characters. CP437 line-drawing bytes must reach the BBS client byte-for-byte.

2. **Non-blocking loop.** The shuttle loop must never block waiting for either side. `bbs_has_data()` and `net_has_data()` use zero-timeout `select()`. If either side has no data, the loop continues immediately to check the other side and then yields.

3. **Single unified codebase.** Platform differences live only in `#ifdef` blocks or in the platform-specific `.c` files. No platform logic belongs in `main.c`, `config.c`, or `dropfile.c`.

4. **C99 only.** No C11, no C++, no compiler extensions, no VLAs, no `//` comments in new code within `#ifdef` blocks that might be compiled by OpenWatcom in C89 mode. (OpenWatcom's C99 support is partial.)

5. **No dynamic memory allocation.** All buffers are stack-allocated or static. No `malloc`/`free`. This is intentional for DOS compatibility and simplicity.

6. **`stderr` for diagnostics, `stdout`/socket/pipe for data.** All `fprintf` calls use `stderr`. Never write diagnostic text to `stdout` — on Linux it is wired to the user's terminal connection.

---

## Module Contracts

### `config.c` / `config.h`

- `config_load()` sets defaults first, then reads the file, then applies CLI overrides. Order matters.
- A missing config file is non-fatal if `--server` is provided on CLI.
- All string fields in `app_config_t` are NUL-terminated and bounded. Use `snprintf` not `strncpy` for assignments from external input.
- The `local_mode` flag is CLI-only (no `Local=` in the config file).

### `dropfile.c` / `dropfile.h`

- Parsers are line-number based (not key=value). Line counting starts at 1.
- A truncated dropfile is non-fatal if enough fields were parsed for the session.
- `session_context_t.socket_handle` is only meaningful on Windows (from DOOR32.SYS line 2).
- `session_context_t.comm_port` is only meaningful on DOS (from DORINFO1.DEF line 4). It is 1-based (COM1=1); the FOSSIL driver converts to 0-based internally.

### `bbs_io.h` implementations

- `bbs_init()` is called once before any other `bbs_*` function.
- `bbs_read()` must be non-blocking. Return 0 if no data is available, negative on disconnect/error.
- `bbs_write()` should write all bytes before returning. Partial writes are an error.
- `bbs_close()` must be safe to call multiple times (idempotent).
- On Linux, `--local` mode puts `STDIN_FILENO` into raw termios mode. `bbs_close()` restores saved termios state.

### `net_io.h` implementations

- `net_connect()` performs blocking DNS resolution and TCP connect, then switches the socket to non-blocking mode.
- `net_read()` and `net_write()` operate on the non-blocking socket. `net_read()` returns 0 (not error) on `EAGAIN`/`EWOULDBLOCK`.
- `net_close()` calls `shutdown()` before `close()`/`closesocket()` for clean TCP teardown.

### Telnet IAC filter (`main.c`)

- State machine lives in `g_telnet_state` and `g_telnet_cmd` (file-scope statics in `main.c`).
- Only applied to **server → client** bytes. Client → server bytes pass through unfiltered.
- On `IAC WILL x`: respond `IAC WONT x`. On `IAC DO x`: respond `IAC DONT x`.
- On `IAC IAC`: pass a single `0xFF` byte through to the client.
- Single-byte commands (GA=0xF9, NOP=0xF1, etc.) are silently consumed.

---

## Adding a New Protocol

If a third protocol (e.g., SSH, raw serial) needs to be added:

1. Add a `PROTOCOL_XXX` constant to `synthrln.h`.
2. Add a `proto = xxx` case to `config.c`'s `apply_kv()` and `apply_cli()`.
3. Add handshake logic to `main.c` Phase 3 (after the `RLOGIN`/`TELNET` block).
4. If it requires a different wire format (not plain TCP), add a new `net_xxx.c` implementing `net_io.h`.

---

## Adding a New Dropfile Format

1. Add a `DROPFILE_XXX` constant to `synthrln.h`.
2. Add a `DropfileType = XXX` case to `config.c`'s `apply_kv()`.
3. Implement a `parse_xxx()` function in `dropfile.c` populating `session_context_t`.
4. Add the new case to `dropfile_parse()`.

---

## Build System

### Makefile targets
- `make linux` — GCC, `-std=c99 -Wall -Wextra -pedantic`
- `make windows` — MinGW-w64 cross-compile
- `make debug-linux` — adds `-g -O0 -fsanitize=address`
- `make clean` — removes `build/`

### CMake
- Standard CMake flow: `cmake -B build && cmake --build build`
- DOS target requires `cmake -DCMAKE_SYSTEM_NAME=DOS` with OpenWatcom toolchain file

### Zero-warning policy
The build must emit **zero warnings** with `-Wall -Wextra -pedantic` on GCC and Clang. The `#pragma comment(lib, ...)` lines in Windows sources will produce harmless `ignoring pragma` warnings under MinGW — these are acceptable and expected.

### DOS build
The DOS build is not tested in CI (no DOS emulator in GitHub Actions). It is a human-tested path only. See `docs/BUILDING_DOS.md`.

---

## Testing

There is no automated test suite yet. Manual test procedure:

```bash
# 1. Verify help text
./build/synthrln --help

# 2. Local mode connection test (requires netcat listener)
nc -l 9999 &
./build/synthrln --local --server 127.0.0.1 --port 9999 --proto telnet

# 3. Config file parsing test
cat > /tmp/test.cfg <<EOF
Protocol = TELNET
Server   = 127.0.0.1
Port     = 9999
EOF
nc -l 9999 &
./build/synthrln --cfgfile /tmp/test.cfg --local

# 4. DOOR32.SYS parse test
cat > /tmp/door32.sys <<EOF
2
1234
38400
TestBBS
1
Test User
TestHandle
30
45
1
80
24
EOF
./build/synthrln --local --server 127.0.0.1 --port 9999 \
  --cfgfile /dev/null  # will fail gracefully, showing dropfile was read
```

When adding a test suite, use a simple netcat-based loopback or a minimal C test harness in `tests/`. Do not introduce a test framework dependency.

---

## Common Pitfalls

- **Do not `fflush(stdout)` or `setbuf(stdout, NULL)` in BBS mode on Linux.** The stdout file descriptor is the user's live connection. Spurious flushes are fine but `setbuf` with a NULL buffer can interfere with partial-write behaviour.

- **Do not use `signal(SIGPIPE, SIG_IGN)` globally.** Instead, handle `EPIPE` errors in `bbs_write()` and `net_write()` by setting the `active` flag to 0 and returning -1. The shuttle loop checks `bbs_is_connected()` and `net_is_connected()` every iteration.

- **Windows socket inheritance.** The BBS must have called `SetHandleInformation(sock, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)` before spawning SYNTHRLN. If not, the socket handle from DOOR32.SYS will be invalid. This is a BBS configuration issue, not a SYNTHRLN bug.

- **mTCP requires `MTCPCFG` env var.** On DOS, the mTCP stack reads its configuration (IP address, gateway, packet driver INT) from the file pointed to by the `MTCPCFG` environment variable. If it is not set, `mtcp_init()` will fail. Document this in your BBS's door configuration instructions.

- **OpenWatcom and C99.** OpenWatcom's C99 support is incomplete. Avoid `_Bool`, `<stdbool.h>`, designated initialisers, and compound literals in code that must compile under `__WATCOMC__`. Use `int` for booleans and `memset` for struct initialisation.
