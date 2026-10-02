# libxylem

[![C99](https://img.shields.io/badge/C-C99-555?logo=c)](#)
[![BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue)](#)
[![Hook modules](https://img.shields.io/badge/hook-modules-4B8BBE)](#)

> Hook-based extensibility and dynamic module loading.

A small C library for hook-based extensibility and dynamic module loading. The
host program defines **hooks** — typed dispatch points — and loads **modules**
(shared libraries) that implement them. Calling a hook dispatches to every
loaded module that provides it. Modules can load other modules, deny hooks,
intercept calls, and claim isolated address regions.

## Contents

- [Features](#features)
- [Install](#install)
- [Build from source](#build-from-source)
- [Concepts](#concepts)
- [Quick start](#quick-start)
- [API reference](#api-reference)
- [Region walkthrough](#region-walkthrough)
- [Testing](#testing)
- [License](#license)

## Features

- **Hooks** — named, typed dispatch points any number of modules can implement
- **Dynamic module loading** — `.so`/`.dll` modules with a one-time
  `xy_install()` entry point
- **Regions** — isolated namespaces constraining dispatch within the module graph
- **Module isolation** — `xy_deny`, `xy_claim`, `xy_require_claim`
- **Interceptors** — planned middleware for hooks
- **Pledges** — planned caller restrictions per hook

## Install

Prebuilt packages are distributed from tty.pt for Linux (APT / Alpine / Arch /
Fedora-RHEL), macOS (Homebrew), Windows (winget / MSYS2), and OpenBSD. Follow
the [installation instructions](https://github.com/tty-pt/ci/blob/main/docs/install.md)
and use **libxylem** as the package name.

## Build from source

The library builds with a plain `make` (the shared [`mk` include.mk](https://github.com/tty-pt/mk)):

```sh
make                  # builds lib/libxylem.so
make test             # run the in-tree test suite
sudo make install     # lib + headers + xylem.pc → $(PREFIX), default /usr/local
```

Link it from your own C code:

```sh
cc my_app.c $(pkg-config --cflags --libs xylem) -pthread
```

**Dependencies:** `libqsys`, `libcorm` (and `-pthread` when linking a host).

## Concepts

### Hooks

A hook is a named, typed function that any number of modules can implement.
The host declares the hook's signature once; each module that wants to handle
it provides an implementation. When the hook is called, all implementing
modules run.

### `XY_DECL` vs `XY_DEF` vs `XY_IMPL`

- **`XY_DECL`** — goes in shared headers. Declares a hook that callers invoke
  as a normal function.
- **`XY_DEF`** — goes in one host `.c` file. Emits the canonical adapter for
  that hook and provides the same normal-function call syntax in that
  translation unit.
- **`XY_IMPL`** — goes in module `.c` files. Registers a module-side listener
  implementation for a hook.

### Host vs Module

- The **host** is the main executable. It defines hooks with `XY_DEF`, loads
  modules with `xy_load`, and dispatches by calling the hook function directly.
- A **module** is a shared library (`.so`/`.dll`). It includes
  `<ttypt/xy-mod.h>`, implements hooks with `XY_IMPL`, and exports
  `xy_install()` which is called once on first load.

### Regions

Regions are isolated namespaces within the module graph. A `call_*` dispatches
only to modules whose region is a descendant-or-equal of the caller's current
region. The root region (region 0) reaches every module.

A module opts into having its own region by calling `xy_claim(bits)` from
`xy_install()`. The parent region must have a claim handler registered via
`xy_require_claim`; without one, `xy_claim` always fails. Once claimed, all
subsequent `xy_load` and `xy_deny` calls from that module operate on the new
child region.

A region is identified by the **pair** `(id, plen)` — the id is only half the
key, and is *not* unique across the tree, because a region's left half has
exactly its parent's numeric id. Every call that names a region therefore takes
both halves, and `plen` is never inferred from the id. See
[`docs/api.md`](docs/api.md#region-identity-id-plen-never-id-alone).

The engine can also create regions directly, with `xy_claim_at()`, and dispatch
to exactly one region with `xy_call_self()`; both are listed under
[Naming a region](#naming-a-region).

#### Context ABI generation

`XY_CTX_ABI_VER` is **3** and `XY_CTX_SIZE` is **184** (generation 2 was 2 /
160; the difference is the `claim_at`, `region_at` and `call_self` pointers in
`struct xy_ctx`). The host calls a module's exported `xy_ctx_abi()` at load time
and **refuses** the module if the descriptor does not match, naming both
generations in the diagnostic — so a stale `.so` fails loudly at load rather than
corrupting memory later.

Bumping the generation therefore requires rebuilding every module, and a
header-only bump does not always do it: if a module's Makefile rule does not list
the installed xylem headers as prerequisites, `make` sees only older sources and
reports the stale binary as up to date.

## Quick start

**Shared header** (`hooks.h`):

```c
#include <ttypt/xy.h>

XY_DECL(int, on_damage, int, player_id, int, amount);
```

**Module** (`mods/combat_log.c`):

```c
#include <ttypt/xy-mod.h>
#include "hooks.h"

XY_IMPL(int, on_damage, int, player_id, int, amount)
{
    printf("player %d took %d damage\n", player_id, amount);
    return 0;
}

void xy_install(void) {}
```

**Host** (`host.c`):

```c
#include "hooks.h"

XY_DEF(int, on_damage, int, player_id, int, amount);

int main(void)
{
    xy_load("mods/combat_log");
    on_damage(1, 50);
    xy_shutdown();
    return 0;
}
```

**Build:**

```sh
# host
cc -o host host.c -lxylem -pthread

# module
cc -o mods/combat_log.so mods/combat_log.c -fPIC -shared -lxylem
```

## API reference

### Macros

#### `XY_DECL(ftype, fname, type, name, ...)`

Declares a hook in a shared header. Generates:
- `fname_t` — function typedef
- `call_fname(...)` — typed dispatch wrapper (calls `XY_CALL` internally)

Parameters alternate as `type, name` pairs. At least one pair is required.

```c
XY_DECL(int, on_tick, int, dt);
// Generates: call_on_tick(int dt)
```

#### `XY_DEF(ftype, fname, type, name, ...)`

Defines the canonical host-side hook adapter. The host calls the hook
directly as a normal function.

```c
XY_DEF(int, on_tick, int, dt);
```

#### `XY_IMPL(ftype, fname, type, name, ...)`

Defines a module listener implementation. Registers the adapter automatically
at startup (via `.init_array`) and opens the function body immediately after.

```c
XY_IMPL(int, on_tick, int, dt)
{
    return dt * 2;
}
```

#### `XY_CALL(retp, fname, ...)`

Dispatches the hook to all eligible modules. `retp` receives the return value
of the last module that ran (zero-initialised if none ran).

```c
int result;
XY_CALL(&result, on_tick, 16);
```

In module context, hook calls use the normal function form as well; the
generated inline dispatch routes through the injected `xy` context and sets
the caller identity correctly.

### Lifecycle

#### `int xy_load(char *fname)`

Load a module into the caller's current region. `fname` is the path to the
shared library, without the `.so`/`.dll` extension.

On first load, `xy_install()` is called. Subsequent loads of the same path
into the same region are no-ops.

Returns `XY_OK` on success, negative on failure.

```c
xy_load("mods/combat_log");
```

#### `void xy_shutdown(void)`

Unload all modules and free resources. After this, `xy_load` can be called
again.

#### `void xy_init(void)`

Explicit initialisation. Called automatically on first use; only needed if
you want to control when initialisation happens.

### Errors

#### `int xy_errno(void)`

Returns the last error code set by any API function.

#### `const char *xy_strerror(int err)`

Returns a static human-readable string for an error code.

```c
int r = xy_load("mods/missing");
if (r != XY_OK)
    fprintf(stderr, "load failed: %s\n", xy_strerror(r));
```

**Error codes:**

| Code | Value | Meaning |
|---|---|---|
| `XY_OK` | 0 | Success |
| `XY_ERR_NOTFOUND` | -1 | Module or hook not found |
| `XY_ERR_INVALID` | -2 | Invalid argument |
| `XY_ERR_TOOBIG` | -3 | Return type too large / no free region slot |
| `XY_ERR_INIT` | -4 | Initialisation failed |
| `XY_ERR_EPERM` | -5 | Not permitted (pledge or claim violation) |

### Pledge

#### ~~`int xy_pledge(const char *hook_name)`~~ — does not exist

**This section used to describe an unimplemented API and was wrong twice over.**
There is no `xy_pledge`, no `xy_pledge_t`, and no reserved `pledge` slot in
`xy_ctx` — all three have zero occurrences in `include/` and `src/`. The
`xy_ctx` listing in `docs/api.md` did show a `pledge` slot, which is where the
claim came from; that listing has been corrected too. See `CHANGELOG.md` 1.5.0.

If you want "no descendant of this region may implement/call that hook", that is
[`xy_deny`](#regions), which *is* implemented: it denies a hook name or module
path in the caller's region and all its descendants, refusing the dispatch with
`XY_ERR_EPERM`.

### Regions

All region functions operate on the caller's **current region**, set
implicitly by the dispatch and load machinery. There are no explicit region-ID
parameters.

#### `XY_MODULE_API uint8_t xy_claim = N`

Module-side region declaration. A module opts into having its own sub-region
by exporting this data symbol with the requested bit-width `N`.

This is a **data symbol**, not a function. The host reads it at load time via
`dlsym`. If the parent region has a claim handler installed (via
`xy_require_claim`), the host performs the claim automatically before calling
`xy_install()`. If no handler is registered, the symbol is ignored and the
module loads flat into the parent's region.

On success:
- The module's region becomes the newly created child of the parent.
- All subsequent `xy_load`, `xy_deny`, `xy_require_claim` calls operate on the
  child region.

```c
// mods/sub_mod.c
XY_MODULE_API uint8_t xy_claim = 4; // request a 4-bit sub-region

void xy_install(void)
{
    // already in our own region — host claimed it for us
    xy_load("mods/sub_module");
}
```

#### `int xy_require_claim(xy_claim_handler_fn_t *fn, void *ud)`

Register a claim handler on the caller's current region and enforce the claim
contract. Only one handler per region; a second non-NULL call replaces the
first.

When `fn` is non-NULL: any `xy_load()` into this region whose module exports
no `xy_claim` symbol is rejected immediately (`xy_install` never runs) and
`xy_load` returns `XY_ERR_EPERM`. When the symbol is present, `fn` is invoked
to approve or deny before `xy_install` runs.

When both `fn` and `ud` are NULL: clears the gate — subsequent loads no longer
require `xy_claim`.

```c
typedef int xy_claim_handler_fn_t(
    const char *module_path,   // path of the requesting module (read-only)
    uint8_t     requested_bits,
    uint8_t    *granted_bits,  // write the approved width here
    void       *ud
);
```

Return `XY_OK` to approve (with `*granted_bits` set), or `XY_ERR_EPERM` to
reject.

```c
static int my_handler(const char *path, uint8_t req,
                       uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 8 ? 8 : req; // cap at 8 bits
    return XY_OK;
}

void xy_install(void)
{
    xy_require_claim(my_handler, NULL);
    xy_load("mods/child"); // child must export xy_claim; approved up to 8 bits
}
```

#### `int xy_deny(const char *what, xy_deny_type_t type)`

Block a hook or module within the caller's current region and all its
descendants. The denial applies to the caller's own region as well — any call
dispatched from within the denying region (or any descendant) is affected.

`type` is one of:

| Value | Meaning |
|---|---|
| `XY_DENY_HOOK` | `what` is a hook name |
| `XY_DENY_MODULE` | `what` is a module path |

```c
// Prevent any module in this region or sub-regions from calling "dangerous_hook"
xy_deny("dangerous_hook", XY_DENY_HOOK);

// Prevent a specific module from running in this region or sub-regions
xy_deny("mods/untrusted", XY_DENY_MODULE);
```

#### ~~`int xy_intercept(const char *hook_name, xy_interceptor_fn_t *fn, void *ud)`~~ — does not exist

**This section used to describe an unimplemented API, and the "reserved
`intercept` slot" it cited did not exist either.** `xy_intercept`,
`xy_intercept_t` and `xy_interceptor_fn_t` have zero occurrences in `include/`
and `src/`. Middleware interception is not implemented and there is no ABI slot
for it; do not plan against it on the strength of this README. See
`CHANGELOG.md` 1.5.0.

The implemented primitive with a related shape is `xy_deny`, which bounds what
descendants may do (structurally) rather than inspecting calls.

#### `int xy_region_each(xy_region_each_fn_t *fn, void *ud)`

Enumerate immediate child regions of the caller's current region. Calls
`fn(child_id, plen, ud)` for each child.

**`plen` is not optional.** A region's identity is the pair `(id, plen)`, never
the id alone: the left half of a region has exactly its parent's numeric id, so
`(0,0)`, `(0,16)`, `(0,17)` and `(0,64)` all have `id == 0`. `plen` is that
child's prefix length in bits. `child_id` is an opaque `uint64_t` — diagnostic
and logging use only.

Return `XY_OK` from `fn` to continue, any other value to stop.
`xy_region_each` returns the last value returned by `fn`, or `XY_OK` if there
were no children.

```c
typedef int xy_region_each_fn_t(uint64_t child_id, uint8_t plen, void *ud);
```

```c
static int print_child(uint64_t id, uint8_t plen, void *ud)
{
    (void)ud;
    fprintf(stderr, "child region: id=%016llx plen=%u\n",
            (unsigned long long)id, (unsigned)plen);
    return XY_OK;
}

xy_region_each(print_child, NULL);
```

#### Naming a region

Because an id alone is ambiguous, every call that must *name* a region takes both
halves, and the accessors come in a pair:

```c
int       xy_with_region(uint64_t region_id, uint8_t plen,
                         xy_scope_fn_t *fn, void *ud);
uint64_t  xy_current_region(void);       // id half only — not a unique name
uint8_t   xy_current_region_plen(void);  // plen half; always read both
int       xy_region_exists(uint64_t region_id, uint8_t plen);
                                         // XY_OK / XY_ERR_NOTFOUND
```

Those all *name* an existing region. Two more calls go the other way — they
*create* a region, or *find* the one covering a point, and neither can be
expressed as a bare accessor:

```c
int       xy_claim_at(uint64_t id, uint8_t plen,
                      xy_claim_handler_fn_t *fn, void *ud);
uint64_t  xy_region_at(uint64_t prefix_id, uint8_t plen,
                       uint8_t *region_plen);
int       xy_call_self(void *retp, xy_adapter_t *adapter, void *args);
```

`xy_claim_at()` is the engine-driven counterpart to `xy_claim()`: the engine
carves out `(id, plen)` itself, with no module loaded, and it becomes the
caller's current region so a following `xy_load()` lands inside it. The new
region attaches to the **nearest existing ancestor**, and no intermediate
ancestors are invented — claiming `(0,64)` straight from the root creates one
region, not four. A same-id child is legal (widening is what matters), an exact
match is idempotent, and a non-NULL `fn` reconfigures an existing region's claim
handler rather than being ignored.

`xy_region_at()` answers "which region is this point in?" for a caller walking
the tree coarse-to-fine. It returns the deepest region that actually **covers**
the point, which is generally not the deepest region that *exists*, and it
returns the covering region's width as well — necessary because `(0,0)`,
`(0,16)`, `(0,32)` and `(0,48)` all share `id == 0`.

`xy_call_self()` dispatches to the caller's current region's **own** modules
only, where `xy_call()` reaches the whole subtree below it. On a three-deep
chain the same walk visits 3 listeners this way and 7 via `xy_call()`, which is
the entire reason it exists: it gives each region one observable turn instead of
re-running every level's subtree once per step. Deny and `xy_last()` semantics
are unchanged. Note `xy_last()` carries only the **last** runner's return value.

`xy_with_region()` runs `fn` with the thread's current region set to
`(region_id, plen)`; nested calls inside `fn` inherit it, and it returns
`XY_ERR_NOTFOUND` if that exact pair does not exist. There is deliberately no
`xy_region_plen(uint64_t id)` getter — a lookup by id alone would be a query, not
a lookup, and a `uint8_t` return could not tell "root" from "not found".

#### `xy_my_region()`

Macro. Returns the `uint64_t` region ID assigned to the calling module.
Available in module context only (requires `<ttypt/xy-mod.h>`). Intended for
diagnostic use.

```c
fprintf(stderr, "my region: id=%016llx plen=%u\n",
        (unsigned long long)xy_my_region(),
        (unsigned)xy_current_region_plen());
```

**This returns only the id half, which is not a unique name for a region.** It is
the *assigned* id captured at load time, not necessarily the current region's
id either. For the two halves of the region you are actually in, use
`xy_current_region()` together with `xy_current_region_plen()`. Do not use
`xy_my_region()` as a map key or an identity comparison.

## Region walkthrough

This example shows a moderator module that controls a child region, an
interceptor, and a deny.

**Shared hook header** (`game_hooks.h`):

```c
#include <ttypt/xy.h>
XY_DECL(int, on_tick, int, dt);
```

**Worker module** (`mods/worker.c`) — lives in the child region:

```c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

XY_IMPL(int, on_tick, int, dt)
{
    printf("worker tick: dt=%d\n", dt);
    return dt;
}

void xy_install(void) {}
```

**Moderator module** (`mods/moderator.c`):

```c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

/* Interceptor: halves dt before passing it on */
/* Claim handler: approve up to 4 bits */
static int claim_handler(const char *path, uint8_t req,
                          uint8_t *granted, void *ud)
{
    (void)path; (void)ud;
    *granted = req > 4 ? 4 : req;
    return XY_OK;
}

/* Request a 1-bit sub-region — data symbol, not a function call */
XY_MODULE_API uint8_t xy_claim = 1;

void xy_install(void)
{
    /* Register handler so child modules may claim */
    xy_require_claim(claim_handler, NULL);

    /* Already in our own region — host claimed it for us, so
     * xy_current_region_plen() reports our own width here. */
    xy_load("mods/worker");
}
```

**Host** (`host.c`):

```c
#include "game_hooks.h"

XY_DEF(int, on_tick, int, dt);

int main(void)
{
    xy_load("mods/moderator");

    /* Dispatches root → moderator's region → worker */
    call_on_tick(100); // worker sees dt=50 after interceptor

    xy_shutdown();
    return 0;
}
```

When `call_on_tick(100)` is dispatched from root:
1. The halving interceptor (registered in moderator's region) fires first,
   setting `dt = 50`.
2. The worker's `on_tick` runs with `dt = 50`.

## Testing

```sh
make test     # builds + runs the validation bins over XY_DECL/DEF/IMPL, regions…
```

From the repository root, `make boundary-check` runs the module-layer gates,
and `make test` runs the full platform suite.

## License

BSD 2-Clause License. Copyright (c) 2025, tty-pt. See `LICENSE`.