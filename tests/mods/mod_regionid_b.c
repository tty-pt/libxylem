/*
 * mod_regionid_b: middle of the CP-3 region-identity gate chain.
 *
 * Loaded by mod_regionid_a from inside (0,16), and claims 1 more bit, so it
 * is promoted into (0,17).  Again id == 0: this is the left half of its
 * parent, which is exactly the case the pre-change allocator refused.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, ri_hook_b, int, dummy);

/* One more bit: (0,16) -> (0,17). */
XY_MODULE_API uint8_t xy_claim = 1;

/* See mod_regionid_a.c for why the host reads this back via dlsym. */
XY_MODULE_API uint8_t ri_seen_plen = 0xFF;

static int permissive_handler(const char *path, uint8_t req,
                              uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return XY_OK;
}

XY_MODULE_API int ri_hook_b(int dummy) {
	(void)dummy;
	return 202;
}

XY_MODULE_API void xy_install(void) {
	ri_seen_plen = xy_current_region_plen();
	xy_require_claim(permissive_handler, NULL);
	xy_load("tests/mods/mod_regionid_c");
}
