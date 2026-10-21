<p align="center">
  <img src="assets/logo.png" alt="libxylem logo" width="220" />
</p>

# libxylem

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![Hook modules](https://img.shields.io/badge/hook-modules-4B8BBE)](#)
[![Region Isolation](https://img.shields.io/badge/security-regions-16A34A)](#)

> Hook-based plugin architecture, dynamic module loading, and hierarchical region isolation for C applications.

`libxylem` is the universal cross-module dispatch layer across the `tty-pt` platform. It replaces brittle dynamic linking and fragile function pointer tables with typed hook dispatches (`XY_DEF`, `XY_DECL`, `XY_IMPL`), automatic module discovery, and hierarchical region sandboxing.

---

## Contents

- [Architecture & The Two-Header Rule](#architecture--the-two-header-rule)
- [Module Discovery & SONAME Resolution](#module-discovery--soname-resolution)
- [Quickstart: Host & Plugin](#quickstart-host--plugin)
- [Core API Reference](#core-api-reference)
- [Design Philosophy: Regions as Security Firewalls](#design-philosophy-regions-as-security-firewalls)
- [Installation & Build](#installation--build)
- [License](#license)

---

## Architecture & The Two-Header Rule

To prevent symbol pollution and enforce strict encapsulation across shared `.so` boundaries, `libxylem` enforces the **Two-Header Rule**:

1. **`module-types.h` (Shared Types Header)**: Contains shared structs, enums, and typedefs. Both caller and implementer include this header.
2. **`module.h` (Hook Declaration Header)**: Declares typed hook dispatches via `XY_DECL()`. Consumers include this header to call the hook. The module implementation includes `module-types.h` and defines `XY_IMPL()`, but **never** includes `module.h`.

This rule eliminates `#ifdef *_IMPL` preprocessor switches and ensures compile-time type safety across dynamically loaded modules.

---

## Module Discovery & SONAME Resolution

When loading plugins via `xy_load(name)`, `libxylem` dynamically resolves targets:

1. **Explicit Relative/Absolute Paths**: E.g. `xy_load("./mods/scoreboard.so")` loads the exact file.
2. **SONAME & Module Resolution**: If given a short name (e.g. `xy_load("axil-auth")`), `libxylem` searches:
   - Paths listed in `LD_LIBRARY_PATH`.
   - The caller's module directory (`./mods/`, `lib/`).
   - Standard system library prefixes (`/usr/local/lib`, `/usr/lib`).
3. **ABI Generation Verification**: Checks `XY_CTX_ABI_VER` before executing `.init_array`, safely rejecting mismatched or stale shared libraries.

---

## Quickstart: Host & Plugin

Define a typed hook in the host, implement it in a shared module, and dispatch:

**Host (`host.c`):**
```c
#include <stdio.h>
#include <ttypt/xy.h>

// Define canonical hook: signature is int on_player_score(int id, int pts)
XY_DEF(int, on_player_score, int, player_id, int, points);

int main(void) {
    xy_init();
    xy_load("mods/scoreboard"); // loads mods/scoreboard.so
    on_player_score(1, 100);    // dispatches to all loaded listeners
    xy_shutdown();
    return 0;
}
```

**Plugin Module (`mods/scoreboard.c`):**
```c
#include <stdio.h>
#include <ttypt/xy-mod.h>

XY_IMPL(int, on_player_score, int, player_id, int, points) {
    printf("Player %d earned %d points!\n", player_id, points);
    return 0;
}

void xy_install(void) {} // Called automatically on first load
```

Build host and module:
```sh
cc -o host host.c -lxylem -pthread
cc -o mods/scoreboard.so mods/scoreboard.c -fPIC -shared -lxylem
```

---

## Core API (The Essential 20%)

| Function / Macro | Signature | Description |
| :--- | :--- | :--- |
| `XY_DEF` | `XY_DEF(ret, name, ...)` | Define host-side hook dispatch adapter |
| `XY_DECL` | `XY_DECL(ret, name, ...)` | Shared header hook declaration |
| `XY_IMPL` | `XY_IMPL(ret, name, ...)` | Module listener implementation (auto-registered) |
| `xy_load` | `int xy_load(const char *path)` | Dynamically load `.so` into caller's current region |
| `xy_deny` | `int xy_deny(what, type)` | Block specific hooks or modules in child regions |
| `xy_claim_at` | `int xy_claim_at(id, plen, fn, ud)` | Carve out isolated execution region |

👉 *For context ABI details, prefix lengths `(id, plen)`, and region traversal, see [docs/api.md](docs/api.md).*

---

## Design Philosophy: Regions as Security Firewalls

* **Hierarchical Plugin Sandboxing**: Regions constrain hook dispatch. A hook call dispatches strictly to descendant modules in the caller's region.
* **ABI Generation Guards**: `XY_CTX_ABI_VER` verification refuses mismatched or stale `.so` modules at load time before execution starts.
* **Zero Boilerplate**: Modules register listeners automatically at load time via `.init_array`, requiring no manual dispatch registration lists.

---

## Installation & Build

### Prebuilt Packages
Prebuilt packages are distributed from tty.pt for Linux (APT / Alpine / Arch / Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD. Follow the [installation instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md) and use **libxylem** as the package name.

### Build from Source
```sh
make                  # builds lib/libxylem.so
make test             # run validation suite over hooks and regions
sudo make install     # install headers and library to system
```

## License

BSD 2-Clause License. Copyright (c) 2025, tty-pt. See `LICENSE`.
