/*
 * mod_xylast_third — third module dispatched for probe_value. Reads
 * xy_last() mid-dispatch and must see mod_xylast_second's return (205),
 * not the result of the inner_probe nested call second made internally
 * (1006) nor NOTFOUND. See tests/test_xy_last_dispatch.c.
 */
#include "../../src/papi.h"

xy_t xy;

static int observed_rc = -999;
static int observed_val = -999;

XY_MODULE_API int
probe_value(int x)
{
	observed_rc = xy_last(&observed_val);
	return x + 300;
}

XY_MODULE_API int
third_observed(int unused)
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
