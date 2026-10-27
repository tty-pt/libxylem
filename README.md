<p align="center">
  <img src="assets/logo.png" alt="libxylem logo" width="220" />
</p>

# libxylem

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![Hook modules](https://img.shields.io/badge/hook-modules-4B8BBE)](#)
[![Region Isolation](https://img.shields.io/badge/security-regions-16A34A)](#)

> Hook-based plugin architecture, dynamic module loading, and hierarchical region isolation for C applications. Canonical header is `<ttypt/xy.h>`.

---

## 1. Quickstart: Host & Plugin

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

## 2. Architecture & The Two-Header Rule

To prevent symbol pollution and enforce strict encapsulation across shared `.so` boundaries, `libxylem` enforces the **Two-Header Rule**:

1. **`module-types.h` (Shared Types Header)**: Contains shared structs, enums, and typedefs. Both caller and implementer include this header.
2. **`module.h` (Hook Declaration Header)**: Declares typed hook dispatches via `XY_DECL()`. Consumers include this header to call the hook. The module implementation includes `module-types.h` and defines `XY_IMPL()`, but **never** includes `module.h`.

This rule eliminates `#ifdef *_IMPL` preprocessor switches and ensures compile-time type safety across dynamically loaded modules.

---

## 3. Core API Reference

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

## 4. Regions & In-WASM Dispatch

* **Hierarchical Plugin Sandboxing**: Regions constrain hook dispatch. A hook call dispatches strictly to descendant modules in the caller's region.
* **In-WASM Hook Dispatcher**: Includes an embedded hook registry and dispatcher for WebAssembly environments, supporting `XY_DECL`, `XY_IMPL`, `XY_LISTENER_TAG`, `xy_call`, and `xy_last` chaining directly inside browser WASM without requiring OS `dlopen`.

---

## 5. Installation & Build

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
