# SYNTHRLN

**Multi-Platform BBS-to-TCP Bridge** — a transparent, two-way byte shuttle between legacy Bulletin Board Systems and modern TCP game servers.

```
BBS User ──[ FOSSIL / stdio / socket ]──> SYNTHRLN ──[ TCP ]──> Game Server
```

SYNTHRLN reads a standard BBS dropfile (`DOOR32.SYS` or `DORINFO1.DEF`), extracts the user's session metadata, establishes an outbound TCP connection (rlogin or raw telnet), and passes bytes in both directions in a tight non-blocking loop. It is intentionally stateless and protocol-transparent — 8-bit clean, CP437-safe, with optional Telnet IAC filtering to protect legacy clients from garbage sequences.

---

## Platform Support

| Target | BBS Transport | Network | Dropfile |
|---|---|---|---|
| **Linux** | `stdin`/`stdout` (pipes) | BSD sockets | `DOOR32.SYS` |
| **Windows** | Inherited Winsock socket | Winsock | `DOOR32.SYS` |
| **MS-DOS** | FOSSIL driver (`INT 14h`) | mTCP (packet driver) | `DORINFO1.DEF` |

---

## Quick Start

### 1. Build

**Linux (GCC or Clang)**
```bash
make linux
# binary: build/synthrln
```

**Linux → Windows cross-compile (requires mingw-w64)**
```bash
make windows
# binary: build/synthrln.exe
```

**CMake (any platform)**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**MS-DOS (OpenWatcom + mTCP)**

See [`docs/BUILDING_DOS.md`](docs/BUILDING_DOS.md).

### 2. Configure

Copy the example config and edit it:

```bash
cp synthrln.cfg.example synthrln.cfg
```

Minimum required config for a rlogin door:

```ini
DropfileType = DOOR32
DropfilePath = /bbs/nodes/node1/door32.sys
Protocol     = RLOGIN
Server       = 192.168.1.100
Port         = 513
ClientUser   = USERNAME
ServerUser   = DOORNAME
TermType     = ANSI
RespectTime  = TRUE
```

### 3. Wire it up in your BBS

**Synchronet (Linux/Windows):** In SCFG, add an external program. Set the command line to:
```
/path/to/synthrln
```
Synchronet will write `DOOR32.SYS` into the node directory before launching.

**Any BBS that can set a startup directory:** Point `DropfilePath` to wherever your BBS writes its dropfile, then add SYNTHRLN as a door with no arguments.

### 4. Test locally (no BBS required)

```bash
# Test against a telnet server without any dropfile
./build/synthrln --local --server towel.blinkenlights.nl --port 23 --proto telnet

# Test rlogin handshake against a local server
./build/synthrln --local --server 127.0.0.1 --port 513 --proto rlogin \
                 --clntuser myuser --srvruser thedoor --termtype ANSI
```

---

## Command-Line Reference

Both `/` (DOS style) and `--` (POSIX style) prefixes are accepted. Keys are 8 characters or fewer for compatibility with 16-bit DOS launchers.

| Argument | Description |
|---|---|
| `cfgfile <path>` | Use an alternate config file instead of `synthrln.cfg` |
| `server <addr>` | Override `Server=` from config |
| `port <n>` | Override `Port=` from config |
| `proto <proto>` | Override protocol: `rlogin` or `telnet` |
| `clntuser <n>` | Override rlogin client username |
| `srvruser <n>` | Override rlogin server username |
| `termtype <t>` | Override rlogin terminal type |
| `local` | Local diagnostic mode — bypasses BBS I/O, uses your terminal directly |

---

## Configuration File Reference

SYNTHRLN looks for `synthrln.cfg` in its execution directory by default.

```ini
# Dropfile
DropfileType = DOOR32          # DOOR32 or DORINFO1
DropfilePath = /path/to/door32.sys

# Connection
Protocol     = RLOGIN          # RLOGIN or TELNET
Server       = 192.168.1.100
Port         = 513

# Rlogin handshake field mapping
# Valid values: USERNAME, REALNAME, DOORNAME, or a literal string
ClientUser   = USERNAME
ServerUser   = DOORNAME
TermType     = ANSI

# Session management
RespectTime  = TRUE            # TRUE = disconnect when BBS time expires
```

---

## Architecture

```
main.c          ← Entry point, IAC filter, shuttle loop, rlogin handshake
config.c        ← Key=Value config file parser + CLI override handler
dropfile.c      ← DOOR32.SYS and DORINFO1.DEF parsers

bbs_io.h        ← Abstract BBS-side I/O interface
bbs_posix.c     ← Linux (stdio pipes) + Windows (socket handle) + --local mode
bbs_fossil.c    ← DOS FOSSIL driver via INT 14h (DOS-only)
bbs_local.c     ← DOS --local mode via <conio.h> (DOS-only)

net_io.h        ← Abstract network-side I/O interface
net_posix.c     ← Linux + Windows outbound TCP (BSD sockets / Winsock)
net_mtcp.c      ← DOS outbound TCP via mTCP library (DOS-only)
```

The shuttle loop is non-blocking on both ends. `bbs_has_data()` and `net_has_data()` use zero-timeout `select()` calls so neither side can stall the other. `platform_yield()` surrenders the CPU for ~1 ms each iteration to avoid busy-looping on multitasking operating systems.

### Telnet IAC Handling

When `Protocol = TELNET`, SYNTHRLN runs a small RFC 854 filter on bytes arriving from the server. Any `IAC WILL x` or `IAC DO x` sequence is consumed and a `WONT x` / `DONT x` refusal is sent back automatically. This keeps raw IAC bytes from reaching the BBS client and appearing as CP437 garbage characters.

### Rlogin Handshake

When `Protocol = RLOGIN`, SYNTHRLN sends the standard RFC 1282 four-field NUL-delimited handshake immediately after the TCP connection is established:

```
\0 <client-username> \0 <server-username> \0 <terminal-type/speed> \0
```

The `ClientUser`, `ServerUser`, and `TermType` config values control exactly what is sent. Values `USERNAME`, `REALNAME`, and `DOORNAME` are interpolated from the dropfile at runtime.

---

## MS-DOS Build Notes

The DOS build requires:

1. **OpenWatcom** 2.0 — cross-compiler targeting 16/32-bit DOS
2. **mTCP** — a public-domain TCP/IP stack for DOS using packet drivers

See [`docs/BUILDING_DOS.md`](docs/BUILDING_DOS.md) for step-by-step instructions including mTCP library placement and packet driver setup.

---

## Contributing

See [`CLAUDE.md`](CLAUDE.md) for codebase conventions, module boundaries, and guidance for AI-assisted development.

Pull requests welcome. Please ensure `make linux` builds with zero warnings before submitting. For Windows, zero warnings under MinGW-w64 cross-compile is the baseline.

---

## License

MIT — see [`LICENSE`](LICENSE).
