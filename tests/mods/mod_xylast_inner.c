/*
 * mod_xylast_inner — sole listener for inner_probe, the hook
 * mod_xylast_second nests into from inside its own probe_value handler.
 * Part of the regression coverage for libxylem 3a2d32e ("xy.last fix");
 * see tests/test_xy_last_dispatch.c.
 */
#include "../../src/papi.h"

xy_t xy;

XY_MODULE_API int
inner_probe(int y)
{
	return y + 999;
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
