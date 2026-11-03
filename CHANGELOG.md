## [1.7.2] - 2026-11-03

- **In-WASM Xylem Hook Dispatcher & Substrate Integration (`src/libxylem-wasm.c`, `include/ttypt/xy-mod.h`)**:
  - Introduced lightweight in-WASM hook registry and dispatcher for isomorphic WebAssembly runtimes.
  - Supported `XY_DECL`, `XY_IMPL`, `XY_LISTENER_TAG`, `xy_call`, `xy_last`, and listener chaining inside WASM without host OS `dlopen`.
  - Exported dynamic host-bridge loader and canonical error reporting (`xy_strerror`, `xy_errno`).
  - Linked `xylem.wasm` with `-Wl,--import-memory -Wl,--global-base=1048576` into Stele's unified linear memory.
  - Standardized `loadNativeAddon` for internal `nativeBinding` initialization with pure WebAssembly fallback.
  - Adopted strict `peerDependencies` (`@tty-pt/stele: ">=1.5.0"`) and `devDependencies` (`@tty-pt/stele: "^1.5.2"`).
  - Migrated build to centralized `make wasm` in `external/mk/include.mk` with post-link `wasm-opt -O3` optimization (dropping binary size to 44 KB).

## [1.5.0]

- **Soname loads resolve against `LD_LIBRARY_PATH` before identity is
  stored.** `module_load_path()` appended the extension and tried `realpath()`
  only, which never searches the loader path -- so a bare `"libaxil-auth"`
  stayed a bare `"libaxil-auth.so"` while `dlopen()` mapped the real file
  elsewhere. On platforms without `dlinfo()` (macOS, OpenBSD) hook locality
  falls back to comparing that stored string against `dladdr()`'s full mapped
  path, found a mismatch, and dropped *every hook* of the module: sessions
  never resolved and `require_login` silently granted on OpenBSD while
  path-loaded modules kept working (confirmed live: wrong-password POST
  rendered the full HTML form from `mods/auth` while `POST /song/add`
  without a cookie returned 415 instead of 401). The resolver now searches
  `LD_LIBRARY_PATH` (`DYLD_LIBRARY_PATH` on macOS) exactly as `dlopen()` does,
  then the compiled-in system defaults (`/lib`, `/usr/lib`, `/usr/local/lib`)
  covering daemons that run with no `LD_LIBRARY_PATH` at all, and stores the
  canonical path; unresolvable names keep the old bare-name behaviour. Covered by `test_objectpath` (`test_soname_resolves_via_ld_library_path`,
  `test_unresolvable_keeps_bare_name`).

- **Region identity is `(id, plen)`, never `id` alone.** The id is only *half* a
  region's key, and it is not unique across the tree: the left half of a region
  has exactly its parent's numeric id, so cosmos `(0,0)`, world 0 `(0,16)`, its
  left half `(0,17)` and the cell `(0,64)` **all have `id == 0`**. `plen` (prefix
  length in bits) is now carried alongside the id everywhere, and is never
  inferred from it.
  - `region_lookup()`, both region corm tables (`region_hd`, `mod_by_region_hd`)
    and the module identity key (`"path\0<id-hex><plen-hex>"`) are all keyed on
    the full pair. `xy_region_key_t` is 9 packed bytes and **must stay packed** —
    it is a bytewise-compared fixed-width corm key, so padding bytes would differ
    between the storing and the looking-up call and every lookup would silently
    miss. Guarded by `_Static_assert(sizeof(xy_region_key_t) == 9)`.
  - **`region_alloc_slot` no longer refuses same-id children.** Its free-slot test
    used the id alone while starting at `s == 0`, so the left half of every
    region — the `s == 0` slot, whose id equals the parent's — was structurally
    unrepresentable. It now tests `(candidate_id, child_plen)`.
  - Ancestry is a `parent`-pointer walk. The previously documented O(1)
    mask-against-ancestor-`plen` test was never true of the implementation and
    cannot be made true, since two ancestors may share an id.
  - Module identity keys disambiguate the same `.so` loaded into two regions that
    differ only in width.
- **`xy_region_each` reports each child's width.** The callback signature is now
  `xy_region_each_fn_t(uint64_t child_id, uint8_t plen, void *ud)`; it was
  `(uint64_t child_id, void *ud)`. **Source-breaking for any callback.**
- **New: `uint8_t xy_current_region_plen(void)`** — the width of the caller's
  current region, from the thread-local entry. No hash lookup, no ambiguity. This
  is how a module learns its own region's width (valid in `xy_install()`, which
  runs after claim promotion).
- **New: `int xy_region_exists(uint64_t id, uint8_t plen)`** — `XY_OK` /
  `XY_ERR_NOTFOUND`. The honest existence-and-addressability predicate.
- **Changed: `xy_with_region(uint64_t id, uint8_t plen, fn, ud)`** — gained
  `plen`. **Source-breaking.** It now fails with `XY_ERR_NOTFOUND` unless that
  exact `(id, plen)` pair exists.
- **Removed: `xy_region_plen(uint64_t id)`.** It cannot survive the identity
  change: a lookup by id alone is a *query*, not a lookup, and a `uint8_t` return
  cannot distinguish "root" (`plen == 0`) from "not found". Use
  `xy_current_region_plen()` for the current region, or `xy_region_exists(id, plen)`
  to test a specific pair.
- **CP-4: engine-driven region creation, cover lookup, and exact-region
  dispatch.** Three new calls, plus a context ABI bump.
  - **New: `int xy_claim_at(uint64_t id, uint8_t plen, xy_claim_handler_fn_t *fn,
    void *ud)`** — the engine-driven counterpart to `xy_claim()`: the engine
    carves out `(id, plen)` with no module loaded, and it becomes the caller's
    current region so a following `xy_load()` lands inside it. The new region
    attaches to the **nearest existing ancestor** (largest width below `plen` that
    prefix-covers the point, root always qualifying) and **no intermediate
    ancestors are invented**, so a multi-bit jump is legal and cheap — claiming
    `(0,64)` straight from the root creates one region, not four.
  - **New: `uint64_t xy_region_at(uint64_t prefix_id, uint8_t plen, uint8_t
    *region_plen)`** — the "which region is this point in?" primitive, returning
    the deepest region that actually **covers** the point (generally not the
    deepest that *exists*). The covering region's width comes back as well, which
    is required rather than cosmetic: `(0,0)`, `(0,16)`, `(0,32)` and `(0,48)`
    all share `id == 0`, so an id-only return is unusable. `region_plen` is set to
    0 on every failure path and may be NULL. Returns `XY_REGION_INVALID` if none;
    the root covers every point, so the only way to miss is a `plen` no region
    reaches.
  - **New: `int xy_call_self(void *retp, xy_adapter_t *adapter, void *args)`** —
    the exact-region counterpart of `xy_call()`, reaching only the modules loaded
    *directly* into the current region: no descendant region, and no ancestor.
    On a three-deep chain the same walk visits **3** listeners in this scope and
    **7** via `xy_call()`, which is the whole reason it exists — it gives each
    region one observable turn instead of re-running every level's subtree once
    per step. Deny checks and `xy_last()` predecessor semantics are identical to
    `xy_call()`'s. Returns `XY_ERR_NOTFOUND` when no listener in the exact region
    ran, which is **not** the same as a listener returning zero; read
    `xy_errno()` to tell them apart. (As with `xy_call()`, `xy_last()` carries
    only the *last* runner's return value.)
  - **Covering is a HIGH-bit mask.** `(a, plen_a)` covers `(id, plen)` when
    `plen_a <= plen` and `(id & high-bit-mask(plen_a)) == a`. Masking the low
    bits instead nests the sibling regions `(1<<48,16)`, `(2<<48,16)` and
    `(3<<48,16)` under `(0,16)` rather than under the root — the opposite of the
    tree their ids imply.
  - **A same-id child is legal.** The width rejection is `plen <=` the nearest
    covering ancestor's width, never "id equals the ancestor's id" — claiming
    `(0,16)` under the root `(0,0)` widens the region and is accepted, because
    `(0,0)` and `(0,16)` sharing an id is the 1.5.0 identity invariant rather than
    an error. An exact `(id, plen)` match is idempotent and becomes the current
    region, and a non-NULL `fn` **reconfigures** an existing region's claim
    handler rather than being silently ignored.
- **Fixed: hook deny was inverted — an ancestor's deny was silently inert.**
  `xy_dispatch()` gated its ancestor-deny walk on the **caller's own**
  `subtree_flags`, then walked the **ancestor** chain for denies: opposite ends of
  the same chain, and `region_propagate_deny()` maintains that bit *upward*, so
  `root->subtree_flags` is the one entry summarising "somewhere in this tree
  there is a deny". For any caller that was not the root the bit was clear and the
  whole deny block was skipped; for the root it happened to be set, so its own deny
  fired. Both documented behaviours were therefore wrong simultaneously and in
  opposite directions: a root hook-deny let a descendant's dispatch through with
  `XY_OK` instead of `XY_ERR_EPERM`, while refusing the root's own dispatch, which
  `xy.h` explicitly says it must not ("a deny applies to sub-regions only"). The
  fix reads the flag from `anc_chain[0]` and bounds the hook walk to
  `i < anc_n - 1` to exclude the caller's own region. No test had ever exercised
  `xy_deny()` — `grep -rn xy_deny tests/*.c` was empty across the whole suite.
- **Context ABI generation 2 → 3** (`XY_CTX_ABI_VER` 2 → 3, `XY_CTX_SIZE`
  160 → 184). The three new function pointers are the whole difference; they sit
  after `region_exists` and before `unload`, and the field order is load-bearing
  across `struct xy_ctx`, `struct xy_t_s` in `src/papi.h`, and `XyCtx` in the
  Rust bindings. **Every module must be rebuilt** — a module built against
  generation 2 still exports `xy_ctx_abi()` returning 2, and the host refuses it at
  load with a diagnostic naming both generations. Note that `xy_ctx_abi()` is a
  *function* returning a descriptor, so `nm -D` cannot tell you a module's
  generation: it prints the symbol address, which says nothing about the value.
  `tests/test_ctx_abi.c` is generation-agnostic (it compares against the live
  constants rather than a literal) so it keeps working after the next bump;
  `tests/mods/mod_stale_ctx.c` is likewise built for the *current* generation and
  refuses at load by construction rather than by a hardcoded number.
- **Rust bindings updated to generation 3**, including safe wrappers
  `claim_at`, `region_at` and `call_self`. `region_at` returns `Option<u64>`
  (`None` for `XY_REGION_INVALID`), which is the ergonomic shape the C return
  value is really describing. The `const _: () = assert!(size_of::<XyCtx>() ==
  XY_CTX_SIZE)` tripwire now has something to check against the C header, so the
  crate compiles clean under `cargo build` for the first time.
- **Tests**: new `tests/test_claim_at` (13 tests) with four fixtures
  (`mod_ca_{root,p1,p1child,p2}`) covers every Phase 0 case — the
  `0/16/32/48/64` leftmost ladder, the `16->64` jump, idempotent re-claim,
  misaligned rejection, `plen > 64`, the same-id child, a non-canonical id,
  deepest-cover `xy_region_at`, dispatch scope, the coarse-to-fine walk (3 vs 7),
  and deny. Three assertions **discriminate** rather than merely returning an
  error: the sibling layout (a low-bit mask gives the root 2 children instead of
  5), the jump (no invented intermediate rungs), and the 3-vs-7 walk. Listeners
  are verified through per-module exported counters read with
  `dlsym(RTLD_NOLOAD)`, since a zero return cannot distinguish "ran and returned
  zero" from "nothing ran".

- **`xy_ctx` gained `current_region_plen` and `region_exists`** (after
  `current_region`), plus `unload`/`reload`/`region_state`. Field order is
  load-bearing and mirrored by `struct xy_t_s` in `src/papi.h` and by `XyCtx` in
  the Rust bindings. There is deliberately **no plen data field**, which is why
  `_xy_init()` does not take a plen argument.
- **Docs corrected against the headers**: `xy_region_each`'s signature, and the
  "Region ID encoding" ancestry claim, in `docs/api.md` and `README.md`. The
  phantom `xy_intercept` / `xy_pledge` / `xy_set_caller` sections — which had no
  implementation and no reserved ABI slot — are struck from both, with the 1.1.2
  entries annotated above. `xy_my_region()` is now documented as returning only
  half an identity.
- **Tests**: `tests/test_region_identity` builds the `(0,0) → (0,16) → (0,17) →
  (0,64)` chain through the public claim path alone and asserts all four regions
  coexist, are individually addressable, report their own width, and form distinct
  dispatch contexts. It is red against the pre-1.5.0 allocator, which put the
  first claim at `1<<62` instead of `id == 0`. Three pre-existing test bugs were
  also fixed (two zero-listener dispatch assertions, one `xy_reload` assumption),
  and fixing the last of those unmasked two further wrong load-order assumptions
  in the cascade tests.
