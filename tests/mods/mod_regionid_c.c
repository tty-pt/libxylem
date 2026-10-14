/*
 * mod_regionid_c: leaf of the CP-3 region-identity gate chain.
 *
 * Loaded by mod_regionid_b from inside (0,17), and claims the remaining 47
 * bits, which takes it to plen 64 — a leaf cell.  Its region is (0,64), the
 * fourth region sharing id == 0.
 *
 * 17 + 47 == 64 is what makes this the end of the chain: region_alloc_slot()
 * with cplen == 64 has exactly one slot, so no further claim is possible.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, ri_hook_c, int, dummy);

/* The remaining 47 bits: (0,17) -> (0,64). */
XY_MODULE_API uint8_t xy_claim = 47;

/* See mod_regionid_a.c for why the host reads this back via dlsym. */
XY_MODULE_API uint8_t ri_seen_plen = 0xFF;

XY_MODULE_API int ri_hook_c(int dummy) {
	(void)dummy;
	return 303;
}

XY_MODULE_API void xy_install(void) {
	/* Leaf: nothing further is loaded. */
	ri_seen_plen = xy_current_region_plen();
}
