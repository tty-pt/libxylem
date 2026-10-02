# libxylem Public Interface

## Principles

1. **Retro-compatibility**: code that never uses regions continues to work unchanged.
   `XY_DEF`, `XY_DECL`, `XY_CALL`, `xy_load`, `xy_shutdown`, `xy_errno`,
   `xy_strerror` all keep their pre-region signatures.

2. **Region IDs are internal**. No public function takes or returns a raw region ID.
   `xy_my_region()` is kept as a diagnostic-only escape hatch but is not part of
   the primary API narrative.

3. **All region management is implicit** — operations always apply to the caller's
   current region, which is set automatically by the dispatch and load machinery.

4. **Modules opt into being region modules** by exporting
   `XY_MODULE_API uint8_t xy_claim = N` as a data symbol (where N is the
   requested bit width).  If they don't, they live flat in whatever region
   they were loaded into, exactly as before.

 5. **Parents control subdivision** by installing a claim handler via
   `xy_require_claim(fn, ud)`.  When a module with an `xy_claim` symbol is
   loaded into a region that has a handler, the host performs the claim
   automatically before running `xy_install()`.

 6. **Regions can enforce the claim contract** with `xy_require_claim(fn, ud)`.
   Once a non-NULL handler is installed, any `xy_load()` into that region
   that finds no `xy_claim` symbol returns `XY_ERR_EPERM` and
   `xy_install` never runs.  Passing `NULL, NULL` clears the gate.

---

## Unchanged

```c
// Define a hook (host or module implementation)
XY_DEF(ftype, fname, ...)

// Declare a hook (consumer-side: generates call_ wrapper only)
XY_DECL(ftype, fname, ...)

// Dispatch to all modules implementing fname whose region is a
// descendant-or-equal of the caller's current region
XY_CALL(retp, fname, ...)

// ~~xy_pledge()~~ — REMOVED from the documentation, not from the ABI.
//
// This entry never described a real function: `xy_pledge`, `xy_pledge_t` and
// `xy_interceptor_fn_t` have zero occurrences in include/ and src/. The
// CHANGELOG's 1.1.2 entry still advertised them. See CHANGELOG.md 1.5.0 and
// ST.md §15.4. The nearest implemented primitive with the same intent is
// xy_deny(), which refuses a hook or module to a region and all its
// descendants.
// int xy_pledge(const char *hook_name);

// Lifecycle
void        xy_init(void);
void        xy_shutdown(void);
int         xy_errno(void);
const char *xy_strerror(int err);

// Adapter registration — called automatically by XY_DEF constructors
unsigned xy_areg(char *name, xy_adapter_t *adapter);
```

---

## Changed signatures

```c
// Load a module into the caller's current region.
// (region_id argument removed)
int xy_load(char *fname);

// Deny a hook name or module path within the caller's current region
// and all its descendants (including the caller's own region).
// (region_id and children_only arguments removed)
int xy_deny(const char *what, xy_deny_type_t type);

// ~~xy_intercept()~~ — REMOVED from the documentation, not from the ABI.
//
// Also phantom: `xy_intercept` / `xy_intercept_t` / `xy_interceptor_fn_t` do not
// exist in include/ or src/. Middleware interception is not implemented; do not
// plan against it on the strength of this file.
// int xy_intercept(const char *hook_name, xy_interceptor_fn_t *fn, void *ud);
```

---

## New API

```c
// Module-side region declaration (data symbol, not a function call).
//
// A module opts into being a region module by exporting this symbol:
//
//   XY_MODULE_API uint8_t xy_claim = N;
//
// where N is the requested bit-width of the sub-region.
// The host reads this symbol at load time.  If the parent region has a
// claim handler (or has require_claim set), the host performs the claim
// automatically before running xy_install().  If neither is set, the
// symbol is ignored and the module loads flat into the parent's region.
//
// This is a linker-visible data symbol — it is NOT a function to call.


// Claim handler type.
//
//   module_path    — path of the child module making the request (read-only)
//   requested_bits — number of bits the child asked for
//   granted_bits   — out-param: write the approved width here to accept
//   ud             — user data supplied to xy_require_claim
//
// Return XY_OK to approve (with *granted_bits set), XY_ERR_EPERM to reject.
typedef int xy_claim_handler_fn_t(
    const char *module_path,
    uint8_t     requested_bits,
    uint8_t    *granted_bits,
    void       *ud);


// Register a claim handler on the caller's current region AND enforce the
// claim contract (require_claim gate) in one call.
//
// fn != NULL:
//   Sets require_claim = 1 and installs fn as the handler.
//   Any xy_load() into this region that finds no `xy_claim` symbol is
//   rejected immediately (xy_install() never runs) and xy_load() returns
//   XY_ERR_EPERM.  When the symbol is present the host invokes fn to approve
//   or deny the request before running xy_install().
//   Only one handler per region; a second non-NULL call replaces the first.
//
// fn == NULL (both fn and ud must be NULL):
//   Clears require_claim = 0 and sets the handler to NULL.
//   Subsequent xy_load() calls no longer require xy_claim; modules with the
//   symbol still load flat (no auto-claim) until a new handler is installed.
//
// Modules loaded before the first xy_require_claim() in the same
// xy_install() context are unaffected.
//
// Returns XY_OK, or XY_ERR_NOTFOUND if the current region cannot be found.
int xy_require_claim(xy_claim_handler_fn_t *fn, void *ud);


// Enumerate immediate child regions of the caller's current region.
//
// Calls fn(child_id, plen, ud) for each child, in allocation order.
//
// (child_id, plen) is the child's identity — both halves are needed. plen is
// that child's prefix length in bits; two regions may share an id and differ
// only in plen, so id alone cannot name a region (see "Region identity" below).
// child_id is an opaque uint64_t; it is provided for diagnostic and logging use
// only and must not be stored or compared.
//
// Return XY_OK from fn to continue iteration, any other value to stop.
// xy_region_each returns the last value returned by fn, or XY_OK if
// there were no children.
typedef int xy_region_each_fn_t(uint64_t child_id, uint8_t plen, void *ud);
int xy_region_each(xy_region_each_fn_t *fn, void *ud);

// Run fn with the thread's current region set to the region identified by
// (region_id, plen). Nested calls inside fn inherit that region.
// Returns XY_ERR_NOTFOUND if no region with that exact (id, plen) exists.
int xy_with_region(uint64_t region_id, uint8_t plen, xy_scope_fn_t *fn,
                   void *ud);

// The two halves of the caller's current region identity.
uint64_t xy_current_region(void);       // id half only — not a unique name
uint8_t  xy_current_region_plen(void);  // plen half — pairs with the above

// XY_OK if the region identified by (region_id, plen) exists,
// XY_ERR_NOTFOUND otherwise.
int xy_region_exists(uint64_t region_id, uint8_t plen);


// ---------------------------------------------------------------------------
// CP-4: engine-driven region creation, cover lookup, exact-region dispatch
// ---------------------------------------------------------------------------

// Create the region (id, plen) and make it the caller's current region.
//
// The engine-driven counterpart to xy_require_claim(): no module is loaded, the
// engine just carves out a region that later xy_load() calls and dispatches can
// name. On success the region is the caller's current region, so a following
// xy_load() lands inside it.
//
// The new region attaches to the NEAREST EXISTING ANCESTOR — the region of the
// largest width below plen that prefix-covers (id, plen), the root always
// qualifying. No intermediate ancestors are invented, so a multi-bit jump is
// legal and cheap: claiming (0,64) straight from the root creates one region,
// not four.
//
// Covering is a HIGH-bit mask: (a, plen_a) covers (id, plen) when
// plen_a <= plen and (id & high-bit-mask(plen_a)) == a. Masking the low bits
// instead would nest the siblings (1<<48,16), (2<<48,16), (3<<48,16) under
// (0,16) rather than under the root, which is the opposite of the tree shape
// the ids imply.
//
// Rejections:
//   plen > 64                              -> XY_ERR_TOOBIG
//   (id & high-bit-mask(plen)) != id       -> XY_ERR_INVALID (misaligned)
//   plen <= nearest covering ancestor width -> XY_ERR_INVALID
//
// A same-id child is legal: claiming (0,16) under the root (0,0) widens the
// region, so it is accepted. The last rejection is about WIDTH, never about id
// equalling the ancestor's — (0,0) and (0,16) sharing an id is the CP-3
// invariant, not an error.
//
// An exact (id, plen) match is idempotent: it succeeds, becomes the current
// region, and — like the creation path — installs fn as that region's claim
// handler when fn is non-NULL. A later claim therefore RECONFIGURES an
// existing region rather than silently ignoring the handler.
int xy_claim_at(uint64_t id, uint8_t plen, xy_claim_handler_fn_t *fn,
                void *ud);

// Find the deepest existing region whose prefix covers a point.
//
// The "which region is this point in?" primitive: a caller walking the tree
// coarse-to-fine needs to tell an exact hit from an ancestor fallback. The
// answer is the deepest covering region, which is generally NOT the deepest
// region that exists — regions below it exist but do not cover this point.
//
// The id half alone does not name a region: (0,0), (0,16), (0,32) and (0,48)
// all share id == 0, so the covering region's width comes back too. Feed the
// pair to xy_with_region() to dispatch there. That width is a property of the
// region FOUND, not of the point asked about, which is why it is a separate
// out-param rather than derivable from plen.
//
// region_plen is set to 0 on every failure path, so it is always defined when
// non-NULL; pass NULL if you do not want it.
// Returns the covering region's id half, or XY_REGION_INVALID if none. The
// root covers every point, so the only way to miss is a plen no region reaches.
uint64_t xy_region_at(uint64_t prefix_id, uint8_t plen,
                      uint8_t *region_plen);

// Dispatch to the caller's current region's own modules only.
//
// The exact-region counterpart of xy_call(). xy_call() reaches the current
// region's whole SUBTREE; this reaches only the modules loaded DIRECTLY into
// the current region — no descendant region, and no ancestor.
//
// It exists so an ancestor walk gives each region one observable turn instead
// of re-running every level's entire subtree once per step. On a three-deep
// chain the same walk visits 3 listeners in this scope and 7 in xy_call()'s.
//
// Deny checks and xy_last() predecessor semantics are identical to xy_call()'s:
// a listener still sees its predecessor's return value, and a deny from any
// ancestor still refuses the whole dispatch with XY_ERR_EPERM.
//
// Returns XY_ERR_NOTFOUND if no listener in this exact region ran. That is NOT
// the same as a listener returning zero — read xy_errno() to tell them apart,
// and note xy_last() only carries the LAST runner's return value.
int xy_call_self(void *retp, xy_adapter_t *adapter, void *args);
```

---

## struct xy_ctx

Injected into every module at load time. Modules access it as the global `xy`
variable (provided by `xy-mod.h`).

```c
struct xy_ctx {
    xy_call_t             *call;
    xy_areg_t             *areg;
    xy_load_t             *load;        // int (*)(char *)
    xy_errno_t            *err;
    xy_strerror_t         *strerror;
    xy_adapter_t          *adapter;
    xy_last_t             *last;
    xy_shutdown_t         *shutdown;
    const char             *module_path; // set by host at load time; read-only
    uint64_t                region_id;   // internal; do not use directly
    /* region management API */
    xy_deny_t              *deny;              // int (*)(const char *, xy_deny_type_t)
    xy_require_claim_t     *require_claim;
    xy_region_each_t       *region_each;
    xy_with_region_t       *with_region;
    xy_current_region_t    *current_region;
    xy_current_region_plen_t *current_region_plen;
    xy_region_exists_t     *region_exists;
    xy_claim_at_t          *claim_at;        // CP-4 (ABI 3)
    xy_region_at_t         *region_at;       // CP-4 (ABI 3)
    xy_call_self_t         *call_self;       // CP-4 (ABI 3)
    /* unload / reload */
    xy_unload_t            *unload;
    xy_reload_t            *reload;
    /* per-region module state; NULL unless xy_region_state_size was exported */
    void                   *region_state;
};
```

There is deliberately **no `pledge`, `intercept` or `set_caller` slot** — those
never existed, and listing them here is what made the README read as though
middleware was available. Note also that there is **no plen data field**: a
module that needs its own region's width calls `xy_current_region_plen()`, which
the framework has already made correct by the time `xy_install()` runs.

Field order from `call` through `region_id` is frozen for ABI compatibility
with existing modules. Fields after `region_id` must match `papi.h`'s `xy_t`
exactly — that pair of structs is `repr(C)`-mirrored in `rust/xylem/src/lib.rs`
and the order is load-bearing.

**Context ABI generation 3.** `XY_CTX_ABI_VER == 3`, `XY_CTX_SIZE == 184`, up
from 2 / 160; the three CP-4 pointers above are the difference. A `_Static_assert`
on `sizeof(struct xy_ctx) == XY_CTX_SIZE` in `xy.h` and a matching `const _: () =
assert!(size_of::<XyCtx>() == XY_CTX_SIZE)` in the Rust mirror each catch a layout
change at compile time; what neither can catch is a **stale binary**. A module
built against generation 2 still exports `xy_ctx_abi()` returning 2, and the host
refuses it at load with a diagnostic naming both generations — it is not a silent
mismatch. `tests/mods/mod_stale_ctx.c` pins that refusal, and `tests/test_ctx_abi.c`
is generation-agnostic (it compares against the live constants, not a literal), so
it keeps working after the next bump.

Because `xy_ctx_abi()` is a **function** that *returns* a descriptor rather than a
variable, `nm -D` cannot tell you a module's generation — it prints the symbol
address, which says nothing about the value. To audit real binaries, call it:

```c
/* preloaded RTLD_GLOBAL, since modules leave xy_*/qsyslog to the host */
uint64_t (*abi)(void) = dlsym(dlopen(path, RTLD_NOW), "xy_ctx_abi");
uint64_t d = abi();                       /* (ver << 32) | size */
```

---

## Region identity: `(id, plen)`, never `id` alone

A region is identified by the **pair** `(id, plen)`:

- `id` is the opaque `uint64_t` prefix value — all 64 bits are address space.
- `plen` is that prefix's length in bits; `0` for the root.

`plen` is **not** packed into `id`, and it is **never inferred from an `id`.**
That is the central invariant of region handling here, because the id is *not*
unique across the tree: the left half of a region has exactly its parent's
numeric id, so `region_id = pos & mask(64 - plen)` gives

```
{ id ,  id | (1 << (63 - plen)) }
```

for the two halves at `plen + 1`. Since one claim is half the remaining
unclaimed bits, *every* level of the hierarchy contains a node whose id equals
its parent's. On the all-zeros path the ambiguity is total — cosmos `(0,0)`,
world 0 `(0,16)`, its left half `(0,17)`, and the cell `(0,64)` **all have
`id == 0`**. `tests/test_region_identity.c` builds exactly that chain.

Consequences, all of which are enforced in code:

- `region_lookup(id, plen)` is keyed on the full pair. The key struct
  (`xy_region_key_t`, 9 bytes) is `__attribute__((packed))` **and that is
  load-bearing**: it is a bytewise-compared fixed-width corm key, so padding
  bytes would differ between the storing and the looking-up call and every
  lookup would silently miss. A `_Static_assert` in `src/libxylem-internal.h`
  guards `sizeof == 9`.
- Module identity keys carry both halves: `"path\0<id-hex><plen-hex>"`.
- `xy_region_each` yields both halves to its callback.
- The region corm tables (`region_hd`, `mod_by_region_hd`) are keyed on
  `region_key_type`, not on a bare `uint64_t`.

### Tree operations

- `XY_REGION_ROOT`: the region `(0, 0)`; ancestor of everything.
- **Ancestry is a `parent`-pointer walk**, not an O(1) mask test. An earlier
  version of this file claimed you could test ancestry by masking `child_id`
  against the ancestor's `plen`; that was never true of the implementation, and
  it cannot be made true here, because the mask test cannot distinguish two
  regions that share an id.
- **Child allocation**: given a parent with `plen` and a requested `bits` width,
  the child's `plen` is `parent.plen + bits` and its id is
  `parent.id | (slot << (64 - child_plen))` where `slot` is the lowest index with
  no existing region at **`(candidate_id, child_plen)`**. Testing the full pair is
  what allows `slot == 0` — the left half, whose id equals its parent's. The
  pre-1.5.0 allocator tested the id alone, so `slot == 0` always collided with
  the parent and the left half of every region was unrepresentable.
- **Maximum cumulative depth**: 64 bits total across all nesting levels.
- **Maximum branching at one level**: `2^bits` children per claim.
- When `plen` reaches 64 there is exactly one slot, so `(0,64)` is a leaf.

### Naming a region

Because an id alone is ambiguous, every public call that must name a region takes
both halves: `xy_with_region(id, plen, ...)`, `xy_region_exists(id, plen)`, and
`xy_region_each`'s callback. To ask "which region am I in", use
`xy_current_region()` **together with** `xy_current_region_plen()`. There is
deliberately no `xy_region_plen(uint64_t id)` getter: a lookup by id alone would
be a query rather than a lookup, and a `uint8_t` return could not distinguish
"root" (`plen == 0`) from "not found".

---

## Error codes

| Code | Value | Meaning |
|---|---|---|
| `XY_OK` | 0 | Success |
| `XY_ERR_NOTFOUND` | -1 | Module or hook not found |
| `XY_ERR_INVALID` | -2 | Invalid argument |
| `XY_ERR_TOOBIG` | -3 | Return type too large / no slot available |
| `XY_ERR_INIT` | -4 | Initialization failed |
| `XY_ERR_EPERM` | -5 | Operation not permitted (pledge or claim violation) |

---

## Usage patterns

### Non-region module (unchanged from before)

```c
// mods/combat_log.c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

XY_DEF(int, on_damage, int, player_id, int, damage) {
    printf("Player %d took %d damage\n", player_id, damage);
    return 0;
}

void xy_install(void) {}
```

### Region moderator (top-level "god")

```c
// mods/mod_universe.c
#include <ttypt/xy-mod.h>

static int universe_claim_handler(const char *module_path,
                                   uint8_t requested, uint8_t *granted,
                                   void *ud) {
    (void)module_path; (void)ud;
    // Allow sub-regions of at most 1 bit (halves only)
    *granted = requested <= 1 ? requested : 1;
    return XY_OK;
}

void xy_install(void) {
    xy_require_claim(universe_claim_handler, NULL);
    xy_load("mods/mod_mortality");      // has xy_claim=1 → auto-claimed into a half
    xy_load("mods/mod_something_else"); // has xy_claim=1 → auto-claimed into the other half
}
```

### Region module (mortality sub-moderator)

```c
// mods/mod_mortality.c
#include <ttypt/xy-mod.h>
#include "game_hooks.h"

// Declare this module as a region module requesting a 1-bit sub-region.
// The host reads this at load time and performs the claim automatically
// before calling xy_install() — no xy_claim() call is needed here.
XY_MODULE_API uint8_t xy_claim = 1;

static int death_claim_handler(const char *module_path,
                                uint8_t requested, uint8_t *granted,
                                void *ud) {
    (void)module_path; (void)ud;
    // Allow sub-regions of at most 2 bits (fourths of this half)
    *granted = requested <= 2 ? requested : 2;
    return XY_OK;
}

XY_DEF(void, on_death, int, entity_id) {
    // Dispatch reaches all sub-handlers in descendant regions automatically
    XY_CALL(NULL, on_death, entity_id);
}

void xy_install(void) {
    // Already operating in our own sub-region — the host claimed it for us.
    xy_require_claim(death_claim_handler, NULL);

    xy_load("mods/mod_loot");    // has xy_claim=2 → auto-claimed into a quarter
    xy_load("mods/mod_respawn"); // has xy_claim=2 → auto-claimed into another quarter
}
```

### Host

```c
// host.c
#include "game_hooks.h"

XY_DEF(void, on_death, int, entity_id);

int main(void) {
    xy_load("mods/mod_universe"); // loads transitively

    // Dispatches into root — reaches all descendant regions automatically
    call_on_death(42);

    xy_shutdown();
    return 0;
}
```
