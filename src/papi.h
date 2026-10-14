#ifndef XY_PAPI_H
#define XY_PAPI_H

#include "../include/ttypt/xy.h"
#include <stdint.h>
#include <stddef.h>	/* offsetof — the xy_t/xy_ctx mirror asserts below */

/* Forward declaration — full definition follows below */
typedef struct xy_t_s xy_t;

typedef struct xy_dispatch_slot {
	struct xy_mod_entry *me;
	void                 *cb;
} xy_dispatch_slot_t;

typedef struct xy_hook_dispatch_cache {
	xy_dispatch_slot_t   *slots;
	int                   count;
	int                   cap;
	uint64_t              region_gen;
} xy_hook_dispatch_cache_t;

/* -------------------------------------------------------------------------
 * Internal region structures
 * ------------------------------------------------------------------------- */

/* A singly-linked list node for denied hook names or module paths */
typedef struct xy_deny_entry {
	char *value;                  /* hook name (owned) or interned module path */
	struct xy_deny_entry *next;
} xy_deny_entry_t;

/*
 * Internal region descriptor.
 *
 * A region's identity is the PAIR (id, plen) — never the id alone.  Both
 * halves live here, and both are used as the key of the region hash table,
 * because distinct regions may share an id and differ only in width: the root
 * is (0,0) while a child granted the leftmost 16 bits is (0,16).  There is no
 * plen-in-id trick to recover the width from the id, so plen must never be
 * inferred from an id — pass it alongside.
 *
 * id is an opaque uint64_t prefix value; all 64 bits are address space.
 *
 * The root region has id=XY_REGION_ROOT (0) and plen=0; it is an ancestor
 * of everything.  Its plen of 0 is a real, distinct width — not "unset".
 *
 * Ancestry check: follow the parent pointers from the entry to root
 * (see region_ancestor_chain in libxylem.c).  O(depth), no hash lookup.
 * Masking an id to a candidate ancestor's plen is NOT sufficient: several
 * ancestors can share an id and differ only in plen, so an id-only mask
 * cannot tell them apart.
 *
 * depth: number of edges from root to this node (incremented by 1 per claim,
 * regardless of bit width).
 *
 * plen: cumulative prefix length in bits (parent->plen + granted_bits).
 *
 * child_bits: the width granted when this region was claimed.  Used to
 * identify immediate children in xy_region_each.
 */
typedef struct xy_region_entry {
	uint64_t                  id;
	uint8_t                   depth;
	uint8_t                   plen;        /* cumulative prefix length in bits */
	uint8_t                   child_bits;  /* width granted at claim time */
	const char               *owner_path;  /* module that owns this region */

	xy_deny_entry_t         *denied_hooks;   /* hook names blocked here */
	xy_deny_entry_t         *denied_modules; /* interned module paths blocked here */
	unsigned                  denied_hooks_set;   /* corm hash set: hook name -> sentinel */
	unsigned                  denied_modules_set; /* corm hash set: interned path ptr -> sentinel */

	/* Cached flags — set whenever the corresponding field becomes non-NULL/non-zero.
	 * Propagated upward through the ancestor chain so xy_call can skip the
	 * region_ancestor_chain walk entirely when the subtree is clean.
	 * Packed into one byte so the hot-path check is a single load + AND. */
	uint8_t                   subtree_flags;
#define XY_SUBTREE_HAS_DENY         0x01u
#define XY_SUBTREE_SECURITY_MASK    XY_SUBTREE_HAS_DENY
#define XY_SUBTREE_ANY_MASK         XY_SUBTREE_HAS_DENY
	uint64_t                  dispatch_gen;

	xy_claim_handler_fn_t   *claim_handler;
	void                     *claim_handler_ud;
	uint8_t                   require_claim;  /* if set, xy_load rejects modules without xy_claim symbol */

	struct xy_region_entry  *parent;         /* direct parent; NULL for root */

	/* Intrusive list of immediate child regions (prepend on claim) */
	struct xy_region_entry  *children_head;
	struct xy_region_entry  *sibling_next;

	/* Intrusive list of modules assigned to this region (append on load, preserves order) */
	struct xy_mod_entry     *mods_head;
	struct xy_mod_entry     *mods_tail;

	/* T1.2: flat subtree module vector, cached in DFS order over descendant
	 * regions. Rebuilt lazily when subtree_mods_dirty is set; all mutators
	 * (xy_load, xy_unload, xy_reload, xy_claim) walk up from the
	 * affected region and mark ancestors dirty. Fast path iterates this
	 * array with a single linear scan — no per-call DFS stack. */
	struct xy_mod_entry    **subtree_mods;
	int                       subtree_mods_count;
	int                       subtree_mods_cap;
	uint8_t                   subtree_mods_dirty;
	/* Per-hook dispatch vectors for this region's subtree, built lazily
	 * from subtree_mods and invalidated by the global dispatch epoch. */
	xy_hook_dispatch_cache_t *hook_dispatch;
	int                        hook_dispatch_cap;
} xy_region_entry_t;

/*
 * Per-module metadata stored in mod_hd.
 * Keyed by "path\0<region_hex><plen_hex>" so the same .so can be loaded into
 * several regions, including two that share an id but differ in plen
 * (e.g. (0,16) and (0,17)) — those are distinct modules with independent
 * per-region state.
 *
 * Field order is tuned so the hot dispatch cluster (ctx, region_next,
 * fn_cache, fn_cache_cap, region_state, handle, region_entry) fits in the
 * first cache line; cold load/unload bookkeeping lives after.
 */
typedef struct xy_mod_entry {
    /* ---- HOT: touched on every dispatch iteration ---- */
    xy_t   *ctx;
    /* Doubly-linked list within the owning region's mods_head list (O(1) removal) */
    struct xy_mod_entry *region_next;
    /* Per-hook function-pointer cache.
     * fn_cache[hook_id] == NULL  → not yet resolved (lazy dlsym on first call).
     * fn_cache[hook_id] == XY_FN_NOT_FOUND → dlsym returned NULL for this hook.
     * Otherwise → the cached function pointer. */
    void   **fn_cache;
    int      fn_cache_cap;
    /* Per-region module state: allocated by framework after xy_install if the
     * module exports xy_region_state_size().  Freed (after optional
     * xy_region_cleanup() call) on unload. */
    void *region_state;
    const char *load_path;
    void    *handle;
    /* Direct pointer to the owning region entry — avoids hash lookup in dispatch. */
    struct xy_region_entry *region_entry;

    /* ---- COLD: load / unload / bookkeeping ---- */
    uint64_t region_id;
    uint8_t  region_plen;
    struct xy_mod_entry *region_prev;
    /* Reference count: incremented on each xy_load for the same (path, region);
     * xy_unload only actually unloads when refcount reaches zero. */
    int refcount;
    /* Module that loaded this one (NULL if loaded by host).
     * Used for cascade-unload of children. */
    struct xy_mod_entry *parent_entry;
    uint64_t *hook_impl_bits;
    int       hook_impl_words;
    /* Owned copy of the composite hash key ("path\0<region_hex><plen_hex>")
     * for removal */
    char *mod_key;
    char *tmp_load_path;
} xy_mod_entry_t;

/* -------------------------------------------------------------------------
 * Internal xy_t (host-side function table, injected into each module)
 *
 * Field order through region_id must stay identical to struct xy_ctx in
 * xy.h — modules are compiled against xy.h but the host fills xy_t.
 * ------------------------------------------------------------------------- */

typedef struct xy_t_s {
	xy_call_t               *call;
	xy_areg_t               *areg;
	xy_load_t               *load;
	xy_errno_t              *err;
	xy_strerror_t           *strerror;
	xy_adapter_t            *adapter;
	xy_last_t               *last;
	xy_shutdown_t           *shutdown;
	const char               *module_path;
	uint64_t                  region_id;
	/* region management API */
	xy_deny_t               *deny;
	xy_require_claim_t      *require_claim;
	xy_region_each_t        *region_each;
	xy_with_region_t        *with_region;
	xy_current_region_t     *current_region;
	xy_current_region_plen_t *current_region_plen;
	xy_region_exists_t       *region_exists;
	xy_claim_at_t            *claim_at;
	xy_region_at_t           *region_at;
	xy_call_self_t           *call_self;
	/* unload / reload */
	xy_unload_t             *unload;
	xy_reload_t             *reload;
	/* per-region module state — set by framework before each dispatch */
	void                     *region_state;
} xy_t;

/* xy_t is the host-side mirror of the module-side struct xy_ctx, and _xy_init()
 * writes one as the other.  A field added to one and not the other is a silent
 * 8-byte skew of every field after it, so pin them here where it is free. */
_Static_assert(sizeof(xy_t) == sizeof(struct xy_ctx),
	"xy_t (papi.h) and struct xy_ctx (xy.h) have diverged — mirror the "
	"missing/extra field(s) in the same position");
_Static_assert(offsetof(xy_t, region_state) == offsetof(struct xy_ctx, region_state),
	"xy_t and struct xy_ctx agree in size but not in field order");
_Static_assert(offsetof(xy_t, current_region_plen)
	== offsetof(struct xy_ctx, current_region_plen),
	"xy_t and struct xy_ctx agree in size but not in field order");

extern xy_t xy;

#endif
