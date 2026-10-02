/*
 * mod_ca_p2: CP-4 dispatch-scope fixture, loaded into planet 2 (2<<48,16), the sibling of planet 1.
 *
 * Answers ca_probe() and counts its own invocations.  xy_call() leaves only
 * the LAST runner's value in retp, so the host cannot learn *who* ran from a
 * dispatch alone; these counters are read back with dlsym(RTLD_NOLOAD), which
 * is what makes the subtree-vs-exact-region distinction observable.
 *
 * No XY_MODULE_API uint8_t xy_claim here: CP-4 builds its regions with
 * xy_claim_at() directly and never arms the per-region claim gate, so these
 * modules load flat into whatever region they are pointed at.
 */
#include <ttypt/xy-mod.h>

XY_LISTENER(int, ca_probe, int, tag);

/* How many times this module has run ca_probe().  RTLD_NODELETE in the host
 * keeps the .so mapped so the symbol stays readable. */
XY_MODULE_API unsigned ca_calls = 0;

XY_MODULE_API int ca_probe(int tag) {
	(void)tag;
	ca_calls++;
	return 104;
}

XY_MODULE_API void xy_install(void) {
}
