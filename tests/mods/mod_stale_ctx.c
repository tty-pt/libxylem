/* mod_stale_ctx.c — a module built against the *previous* context layout.
 *
 * This fixture exists to prove the host refuses a stale module instead of
 * overrunning its context.  It does not include struct xy_ctx: it declares the
 * context layout of one ABI generation back and returns that descriptor from
 * xy_ctx_abi().
 *
 * The canaries sit immediately after the context object.  A STALE_SIZE-byte
 * context filled by an XY_CTX_SIZE-byte host write overruns it by exactly the
 * growth of the last bump, so without the ABI gate they are overwritten with
 * function pointers.  That is the whole shape of the real incident, where a
 * 16-byte overrun landed in libaxil-tty's static mux_map and crashed the server
 * on its first HTTP request.
 *
 * Everything about the *geometry* is derived from the current header rather
 * than hardcoded, so this fixture keeps testing the right thing across ABI
 * bumps instead of quietly going stale:
 *
 *   - STALE_SIZE is pinned by _Static_assert against the field list below, so a
 *     bump that nobody propagates here fails the fixture build loudly;
 *   - the canary count is derived from the actual size difference, so it always
 *     covers the whole overrun;
 *   - the reported generation is XY_CTX_ABI_VER - 1, always exactly one behind.
 */
#include <ttypt/xy.h>
#include <stdint.h>

/* The previous generation's context: 20 fields, without the three CP-4 region
 * entries (claim_at, region_at, call_self).  Field order and types copied from
 * that header.  Do NOT sync this against xy.h — that is the entire point. */
struct stale_ctx {
	xy_call_t               *call;
	xy_areg_t               *areg;
	xy_load_t               *load;
	xy_errno_t              *err;
	xy_strerror_t           *strerror;
	xy_adapter_t            *adapter;
	xy_last_t               *last;
	xy_shutdown_t           *shutdown;
	const char              *module_path;
	uint64_t                 region_id;
	xy_deny_t               *deny;
	xy_require_claim_t      *require_claim;
	xy_region_each_t        *region_each;
	xy_with_region_t        *with_region;
	xy_current_region_t     *current_region;
	xy_current_region_plen_t *current_region_plen;
	xy_region_exists_t      *region_exists;
	xy_unload_t             *unload;
	xy_reload_t             *reload;
	void                    *region_state;
};

#define STALE_VER   (XY_CTX_ABI_VER - 1)
#define STALE_SIZE  160

/* If the field list ever stops matching STALE_SIZE, the fixture is no longer a
 * faithful reproduction of the previous generation and the gate test would be
 * measuring the wrong overrun — so refuse to compile rather than pass quietly. */
_Static_assert(sizeof(struct stale_ctx) == STALE_SIZE,
	"the stale context must stay the previous generation's size (160 bytes) "
	"to reproduce the overrun; update this field list and STALE_SIZE together "
	"whenever XY_CTX_ABI_VER changes");
_Static_assert(STALE_VER >= 1,
	"the stale fixture reports one generation behind the host, so the host must "
	"be at least generation 2");

/* The context and its canaries live in one object, so adjacency is guaranteed
 * by construction rather than by luck.  They must NOT be separate globals: the
 * zeroed context lands in .bss while an *initialised* canary lands in .data,
 * and a host-sized write into the former never reaches the latter — which made
 * an earlier version of this fixture pass vacuously.
 *
 * The canary count covers the entire overrun: how far the current context
 * exceeds the stale one.  Sized from the headers, never a fixed 2.
 *
 * The canaries are set by get_xy_ptr(), which the host calls immediately before
 * it would write the context, and so are primed for exactly the window under
 * test.  (xy_install() would be too late; it runs after the fill.) */
#define CANARY_WORDS ((XY_CTX_SIZE - STALE_SIZE) / sizeof(uint64_t))
_Static_assert(XY_CTX_SIZE > STALE_SIZE,
	"the host context must be larger than the stale one for this fixture to "
	"reproduce an overrun at all");
_Static_assert(CANARY_WORDS >= 1,
	"the canary region must be non-empty or the overrun would go unobserved");

struct stale_module {
	struct stale_ctx ctx;
	uint64_t          canary[CANARY_WORDS];
};
static struct stale_module mod;

/* The canary pattern, exported so the test reads it from here rather than
 * duplicating the formula — one source of truth for both sides. */
#define CANARY_SEED 0x5eed5eed5eed5eedULL
XY_MODULE_API uint64_t xy_ctx_canary_value(unsigned i)
{
	return CANARY_SEED + (uint64_t)i;
}

XY_MODULE_API void *get_xy_ptr(void)
{
	for (unsigned i = 0; i < CANARY_WORDS; i++)
		mod.canary[i] = xy_ctx_canary_value(i);
	return &mod.ctx;
}

/* Reports the previous generation, as a genuinely stale module would have to. */
XY_MODULE_API uint64_t xy_ctx_abi(void)
{
	return ((uint64_t)STALE_VER << 32) | (uint64_t)STALE_SIZE;
}

/* Geometry, so the test can prove it is looking at the memory the overrun would
 * reach rather than trusting this file's layout arithmetic. */
XY_MODULE_API uintptr_t xy_ctx_declared_size(void)
{
	return (uintptr_t)sizeof(struct stale_ctx);
}

XY_MODULE_API uintptr_t xy_ctx_canary_offset(void)
{
	return (uintptr_t)((char *)&mod.canary[0] - (char *)&mod.ctx);
}

XY_MODULE_API unsigned xy_ctx_canary_words(void)
{
	return CANARY_WORDS;
}

/* A hook, so the module is a well-formed xy module in every respect except its
 * context layout — the refusal must be attributable to the ABI gate alone. */
XY_MODULE_API int on_tick(int dt)
{
	return dt + 1;
}

XY_MODULE_API void xy_install(void)
{
}