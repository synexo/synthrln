# Building SYNTHRLN for MS-DOS

This document covers building the DOS target, which requires OpenWatcom and the mTCP library. This path is not tested in CI; it is a manual human-tested build only.

---

## Prerequisites

### 1. OpenWatcom 2.0

Download from https://github.com/open-watcom/open-watcom-v2/releases

Install and ensure `wcc` (C compiler) and `wlink` (linker) are on your PATH, or set `WATCOM` to your installation directory.

### 2. mTCP

mTCP is a public-domain TCP/IP stack for DOS that drives a hardware or emulated packet driver.

- Download from http://www.brutman.com/mTCP/
- Extract to `vendor/mtcp/` in the SYNTHRLN repo root so the layout is:
  ```
  vendor/
    mtcp/
      include/
        tcpinc.h
        utils.h
        (other mTCP headers...)
      lib/
        mtcplib.lib
  ```

mTCP ships as a C++ library. Because SYNTHRLN is written in C, `net_mtcp.c` calls a thin C-callable wrapper. See the section on the C wrapper below.

---

## Building

### Option A: OpenWatcom makefile

A DOS-specific makefile is provided for OpenWatcom:

```dos
wmake /f Makefile.dos
```

This produces `SYNTHRLN.EXE` in the `build\` directory.

### Option B: Manual compilation

```dos
set WATCOM=C:\WATCOM
set PATH=%WATCOM%\BINNT;%PATH%
set INCLUDE=%WATCOM%\H;vendor\mtcp\include

wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\main.c -fo=build\main.obj
wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\config.c -fo=build\config.obj
wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\dropfile.c -fo=build\dropfile.obj
wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\bbs_fossil.c -fo=build\bbs_fossil.obj
wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\bbs_local.c -fo=build\bbs_local.obj
wcc386 -bt=dos -mf -3r -fp3 -s -d1 src\net_mtcp.c -fo=build\net_mtcp.obj

wlink system dos4g &
  name build\SYNTHRLN.EXE &
  file build\main.obj &
  file build\config.obj &
  file build\dropfile.obj &
  file build\bbs_fossil.obj &
  file build\bbs_local.obj &
  file build\net_mtcp.obj &
  library vendor\mtcp\lib\mtcplib.lib
```

---

## The mTCP C Wrapper

mTCP is a C++ library. `net_mtcp.c` declares the `mtcp_*` functions as `extern` and expects them to be provided by a thin C++ wrapper file `src/net_mtcp_wrap.cpp`.

You must compile this wrapper file with `wpp386` (the OpenWatcom C++ compiler) and link it in:

```dos
wpp386 -bt=dos -mf -3r -fp3 -s src\net_mtcp_wrap.cpp -fo=build\net_mtcp_wrap.obj
```

The wrapper implements:

```cpp
// src/net_mtcp_wrap.cpp
#include "tcpinc.h"
#include "utils.h"
#include <string.h>

extern "C" {

static TcpSocket *g_sock = NULL;

int mtcp_init(void) {
    return Utils::initStack(1, 1) ? 0 : -1;
}

void mtcp_shutdown(void) {
    Utils::endStack();
}

int mtcp_connect(const char *host, int port) {
    g_sock = new TcpSocket();
    if (!g_sock) return -1;
    IpAddr_t addr;
    if (Dns::resolve(host, addr, 1) < 0) { delete g_sock; g_sock = NULL; return -1; }
    return g_sock->connect(addr, (uint16_t)port, 10000) ? 0 : -1;
}

int mtcp_has_data(void) {
    if (!g_sock) return 0;
    return (g_sock->bytesWaiting() > 0) ? 1 : 0;
}

int mtcp_recv(unsigned char *buf, int max_len) {
    if (!g_sock) return -1;
    return g_sock->recv((uint8_t *)buf, (uint16_t)max_len);
}

int mtcp_send(const unsigned char *buf, int len) {
    if (!g_sock) return -1;
    return g_sock->send((uint8_t *)buf, (uint16_t)len);
}

int mtcp_is_connected(void) {
    if (!g_sock) return 0;
    return g_sock->isConnected() ? 1 : 0;
}

void mtcp_close(void) {
    if (g_sock) { g_sock->close(); delete g_sock; g_sock = NULL; }
}

void mtcp_process(void) {
    Utils::idle();
}

} // extern "C"
```

---

## Runtime Setup (on the DOS machine)

1. **Packet driver** — Load a packet driver for your network card before running SYNTHRLN:
   ```dos
   NE2000.COM 0x60 5 0x300
   ```
   (Parameters depend on your card and driver. Consult your packet driver documentation.)

2. **mTCP configuration** — Create `MTCP.CFG` with your network settings:
   ```
   PACKETINT  0x60
   IPADDR     192.168.1.50
   NETMASK    255.255.255.0
   GATEWAY    192.168.1.1
   NAMESERVER 192.168.1.1
   HOSTNAME   BBSDOS
   ```

3. **Set `MTCPCFG` environment variable**:
   ```dos
   SET MTCPCFG=C:\MTCP\MTCP.CFG
   ```

4. **FOSSIL driver** — Load a FOSSIL driver for serial I/O (e.g., X00, BNU, or RBCOMM):
   ```dos
   X00.SYS
   ```

5. **Place SYNTHRLN** in your BBS door directory alongside `SYNTHRLN.CFG`.

---

## DOS-Specific Config Notes

On DOS, the default dropfile type is `DORINFO1` and the default dropfile filename is `DORINFO1.DEF`. A typical DOS config:

```ini
DropfileType = DORINFO1
DropfilePath = C:\BBS\DORINFO1.DEF
Protocol     = RLOGIN
Server       = 192.168.1.100
Port         = 513
ClientUser   = USERNAME
ServerUser   = DOORNAME
TermType     = ANSI
RespectTime  = TRUE
```

---

## Testing in DOSBox

DOSBox-X supports packet drivers for testing mTCP applications:

1. Enable the `ne2000` emulation in `dosbox-x.conf`
2. Use the `NE2000.COM` packet driver pointing at DOSBox's virtual INT
3. Set up `MTCP.CFG` with the virtual network's IP range

This allows end-to-end testing of the DOS binary against a real telnet/rlogin server on the host machine.
