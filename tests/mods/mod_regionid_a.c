/*
 * mod_regionid_a: root of the CP-3 region-identity gate chain.
 *
 * Claims 16 bits out of the region it is loaded into, so from the root
 * (0,0) it is promoted into the child (0,16)  [ST.md §15.1, §21].
 *
 * All three modules in this chain have id == 0 and differ only in plen:
 *
 *     (0,0) root
 *        └─ (0,16)  this module,      claim 16
 *             └─ (0,17) mod_regionid_b, claim  1
 *                  └─ (0,64) mod_regionid_c, claim 47
 *
 * That is the whole point of the chain: the left half of a region has the
 * *same numeric id* as its parent, so id alone cannot name a region.
 *
 * xy_install() runs AFTER _xy_claim_for_load() has switched the thread-local
 * region to the child, so xy_current_region_plen() reports this module's own
 * width.  It re-arms the claim gate on that new region and loads the next
 * module, which is what builds the rest of the chain through the *existing*
 * public claim path — no CP-4 creation API is involved.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, ri_hook_a, int, dummy);

/* Request 16 bits — half the cosmos, so the child lands on the left half
 * of the parent and therefore shares its id. */
XY_MODULE_API uint8_t xy_claim = 16;

/* What xy_current_region_plen() reported during xy_install.  The host reads
 * this back with dlsym(RTLD_NOLOAD); RTLD_NODELETE keeps the .so mapped. */
XY_MODULE_API uint8_t ri_seen_plen = 0xFF;

/* The claim gate has to be re-armed by hand on each new region: a claim
 * handler is per-region state, so the handler installed on the parent does
 * not apply to the child the module was just promoted into. */
static int permissive_handler(const char *path, uint8_t req,
                              uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return XY_OK;
}

XY_MODULE_API int ri_hook_a(int dummy) {
	(void)dummy;
	return 101;
}

XY_MODULE_API void xy_install(void) {
	ri_seen_plen = xy_current_region_plen();
	xy_require_claim(permissive_handler, NULL);
	xy_load("tests/mods/mod_regionid_b");
}
