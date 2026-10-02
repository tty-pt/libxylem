#ifndef XY_MOD_H
#define XY_MOD_H

/**
 * @file xy-mod.h
 * @brief Module-side implementation glue for the xy module system.
 *
 * Included by module sources to get the injected xy context and the
 * XY_IMPL / XY_CALL expansion machinery.
 */

#include "xy.h"
static struct xy_ctx xy;

struct xy_ctx;

XY_MODULE_API __attribute__((weak)) struct xy_ctx *
get_xy_ptr(void)
{
	return &xy;
}

/* The host fills `xy` with sizeof(struct xy_ctx) bytes as *it* was compiled.
 * If this module was compiled against a different header the object may be
 * smaller and that write overruns it into adjacent BSS.  Exporting the ABI
 * descriptor lets the host refuse instead of corrupting memory; see
 * XY_CTX_ABI_VER in xy.h. */
XY_MODULE_API __attribute__((weak)) uint64_t
xy_ctx_abi(void)
{
	return XY_CTX_ABI_DESC;
}

/* In module context, XY_CALL routes through the injected xy context so
 * dispatch goes through xy.call and uses the module's assigned region. */
#undef XY_CALL
#define XY_CALL(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	xy.call(retp, &fname##_adapter, &args); \
}

/**
 * @brief Load a module into the caller's current region.
 *
 * @param fname     Path to .so / .dll
 */
#define xy_load(fname) \
	xy_mod_load_(&xy, (fname))
static inline UNUSED int
xy_mod_load_(struct xy_ctx *_n, char *f)
{ return _n->load(f); }

/**
 * @brief Deny a hook or module within the caller's current region.
 *
 * The deny applies to the caller's current region and all its descendants.
 *
 * @param what       Hook name (XY_DENY_HOOK) or module path (XY_DENY_MODULE)
 * @param type       XY_DENY_HOOK or XY_DENY_MODULE
 */
#define xy_deny(what, type) \
	xy_mod_deny_(&xy, (what), (type))
static inline UNUSED int
xy_mod_deny_(struct xy_ctx *_n, const char *w, xy_deny_type_t t)
{ return _n->deny(w, t); }

/**
 * @brief Set or clear the claim gate on the caller's current region.
 *
 * Non-NULL fn: enables the gate — subsequent xy_load() calls require an
 * xy_claim symbol from the module.
 * NULL fn: clears the gate — subsequent xy_load() calls need no xy_claim.
 *
 * @param fn  Claim handler function, or NULL to clear.
 * @param ud  User data passed to fn (ignored when fn is NULL).
 */
#define xy_require_claim(fn, ud) \
	xy_mod_require_claim_(&xy, (fn), (ud))
static inline UNUSED int
xy_mod_require_claim_(struct xy_ctx *_n, xy_claim_handler_fn_t *fn, void *ud)
{ return _n->require_claim(fn, ud); }

/**
 * @brief Enumerate immediate child regions of the caller's current region.
 *
 * @param fn  Callback: fn(child_id, plen, ud). Return XY_OK to continue.
 * @param ud  User data passed to fn
 */
#define xy_region_each(fn, ud) \
	xy_mod_region_each_(&xy, (fn), (ud))
typedef int xy_mod_region_each_fn_t(uint64_t child_id, uint8_t plen, void *ud);
typedef int xy_mod_region_each_t(struct xy_ctx *_n,
                                    xy_mod_region_each_fn_t *fn,
                                    void *ud);
static inline UNUSED int
xy_mod_region_each_(struct xy_ctx *_n, xy_mod_region_each_fn_t *fn, void *ud)
{ return _n->region_each((xy_region_each_fn_t *)fn, ud); }

/**
 * @brief Run @p fn with the caller's current region temporarily set to the
 * region identified by (@p region_id, @p plen).
 *
 * Nested hook calls inside @p fn inherit that region.
 *
 * @return XY_ERR_NOTFOUND if no region with that exact (id, plen) exists.
 */
#define xy_with_region(region_id, plen, fn, ud) \
	xy_mod_with_region_(&xy, (region_id), (plen), (fn), (ud))
static inline UNUSED int
xy_mod_with_region_(struct xy_ctx *_n, uint64_t region_id, uint8_t plen,
                     xy_scope_fn_t *fn, void *ud)
{ return _n->with_region(region_id, plen, fn, ud); }

/** @brief Return the id half of the current region identity for this thread. */
#define xy_current_region() (xy.current_region())

/**
 * @brief Return the plen half of the current region identity.
 *
 * Reads the thread-local entry directly — no lookup, no ambiguity.
 * 0 for the root region.
 */
#define xy_current_region_plen() \
	xy_mod_current_region_plen_(&xy)
static inline UNUSED uint8_t xy_mod_current_region_plen_(struct xy_ctx *_n)
{ return _n->current_region_plen ? _n->current_region_plen() : 0; }

/**
 * @brief Test whether the region identified by (@p region_id, @p plen)
 * exists and is addressable.
 *
 * @return XY_OK if that exact region exists, XY_ERR_NOTFOUND otherwise.
 */
#define xy_region_exists(region_id, plen) \
	xy_mod_region_exists_(&xy, (region_id), (plen))
static inline UNUSED int
xy_mod_region_exists_(struct xy_ctx *_n, uint64_t region_id, uint8_t plen)
{ return _n->region_exists ? _n->region_exists(region_id, plen) : XY_ERR_NOTFOUND; }

/**
 * @brief Create the region identified by (@p id, @p plen), or reuse it.
 *
 * On success the region is the module's current region, so a following
 * xy_load() lands inside it.  Rejects plen > 64, a misaligned @p id, and a
 * @p plen that is not strictly wider than the region's nearest existing
 * ancestor; an exact match is idempotent.  The rejection is about WIDTH, never
 * about @p id equalling the ancestor's -- claiming (0,16) under the root (0,0)
 * widens the region and is legal.  @p id carries its prefix in the high bits
 * above @p plen and its width is never inferred from it -- pass both halves.
 */
#define xy_claim_at(id, plen, fn, ud) \
	xy_mod_claim_at_(&xy, (id), (plen), (fn), (ud))
static inline UNUSED int
xy_mod_claim_at_(struct xy_ctx *_n, uint64_t id, uint8_t plen,
                 xy_claim_handler_fn_t *fn, void *ud)
{ return _n->claim_at ? _n->claim_at(id, plen, fn, ud) : XY_ERR_INVALID; }

/**
 * @brief Find the deepest existing region whose prefix covers a point.
 *
 * @param region_plen Out-param for the covering region's width; set to 0 on
 *                    every failure path.  May be NULL.
 * @return The covering region's id half, or XY_REGION_INVALID if none.
 */
#define xy_region_at(prefix_id, plen, region_plen) \
	xy_mod_region_at_(&xy, (prefix_id), (plen), (region_plen))
static inline UNUSED uint64_t
xy_mod_region_at_(struct xy_ctx *_n, uint64_t prefix_id, uint8_t plen,
                  uint8_t *region_plen)
{
	if (_n->region_at)
		return _n->region_at(prefix_id, plen, region_plen);
	if (region_plen)
		*region_plen = 0;
	return XY_REGION_INVALID;
}

/**
 * @brief Dispatch to the current region's own modules only.
 *
 * The exact-region counterpart of XY_CALL(), which reaches the current
 * region's whole subtree.  Use it for a coarse-to-fine ancestor walk, where
 * each region should get one turn rather than its whole subtree.  Deny checks
 * and xy.last() predecessor semantics match XY_CALL().
 */
#undef XY_CALL_SELF
#define XY_CALL_SELF(retp, fname, ...) { \
	struct fname##_args args = { __VA_ARGS__ }; \
	xy.call_self(retp, &fname##_adapter, &args); \
}

/**
 * @brief Unload a module from the caller's current region.
 *
 * @param fname  Path passed to xy_load() (without .so/.dll suffix)
 */
#define xy_unload(fname) \
	xy_mod_unload_(&xy, (fname))
static inline UNUSED int
xy_mod_unload_(struct xy_ctx *_n, char *f)
{ return _n->unload(f); }

/**
 * @brief Reload a module in place in the caller's current region.
 *
 * @param fname  Path passed to xy_load() (without .so/.dll suffix)
 */
#define xy_reload(fname) \
	xy_mod_reload_(&xy, (fname))
static inline UNUSED int
xy_mod_reload_(struct xy_ctx *_n, char *f)
{ return _n->reload(f); }

/**
 * @brief Return the region ID assigned to this module (diagnostic/internal).
 */
#define xy_my_region() (xy.region_id)

/* -------------------------------------------------------------------------
 * Per-region module state helpers
 *
 * Usage:
 *
 *   XY_REGION_STATE {
 *       int    counter;
 *       char  *label;
 *   };
 *   XY_REGION_INIT;
 *
 *   // In any hook or xy_install:
 *   XY_RS->counter++;
 *
 * XY_REGION_STATE  — begins the struct definition (struct xy_rs_s { ... })
 * XY_REGION_INIT   — emits xy_region_state_size() after the struct closing
 *                     brace; place on the line after the closing brace.
 * XY_RS            — typed pointer to this module's current region state.
 *                     Valid only during hook dispatch or xy_install.
 * ------------------------------------------------------------------------- */

/** Begin per-region state struct declaration. */
#define XY_REGION_STATE struct xy_rs_s

/** Emit the xy_region_state_size() export after the struct definition. */
#define XY_REGION_INIT \
	XY_MODULE_API size_t xy_region_state_size(void) \
	{ return sizeof(struct xy_rs_s); }

/** Typed pointer to the current region's state block. */
#define XY_RS ((struct xy_rs_s *)xy.region_state)

#endif
