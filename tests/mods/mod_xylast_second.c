/*
 * mod_xylast_second — second module dispatched for probe_value. Exercises
 * both halves of libxylem 3a2d32e ("xy.last fix"):
 *
 *   1. Reads xy_last() mid-dispatch and records what it saw (exposed via
 *      second_observed()). Before the fix this was always XY_ERR_NOTFOUND;
 *      it must now be mod_xylast_first's return.
 *   2. Immediately afterwards, nest-calls inner_probe (a different hook,
 *      single listener in mod_xylast_inner) *before* returning. This is
 *      the second, independent bug the same commit closed: the nested
 *      xy_call resets the TLS xy_last state for its own dispatch and never
 *      restores it on return, so whoever reads xy_last() right after this
 *      module finishes must not see the nested call's result. That
 *      restoration is asserted from the *next* listener (mod_xylast_third),
 *      not from here, because the outer loop only republishes the correct
 *      state once this module's own dispatch_call returns.
 *
 * See tests/test_xy_last_dispatch.c for the assertions.
 */
#include "../../src/papi.h"
#include "mod_ctx_abi.h"

/* Local-only declaration of a hook implemented elsewhere (mod_xylast_inner);
 * mirrors mod_ptr_args_caller.c's use of ptr_lookup. Must NOT be combined
 * with XY_IMPL/XY_LISTENER for the same name in this TU. */
XY_HOOK_DECL(int, inner_probe, int, y);

xy_t xy;

static int observed_rc = -999;
static int observed_val = -999;

XY_MODULE_API int
probe_value(int x)
{
	observed_rc = xy_last(&observed_val);

	/* Nested dispatch into an unrelated hook. Must not leak into what
	 * mod_xylast_third sees via xy_last() once this call returns. */
	inner_probe(7);

	return x + 200;
}

XY_MODULE_API int
second_observed(int unused)
{
	(void) unused;
	return observed_rc == XY_OK ? (1000 + observed_val) : -1;
}

XY_MODULE_API void
xy_install(void)
{
}

XY_MODULE_API xy_t *
get_xy_ptr(void)
{
	return &xy;
}
