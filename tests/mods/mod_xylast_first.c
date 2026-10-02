/*
 * mod_xylast_first — first module dispatched for probe_value. Has no
 * predecessor in the chain, so (unlike second/third) it never calls
 * xy_last(). Part of the regression coverage for libxylem 3a2d32e
 * ("xy.last fix"); see tests/test_xy_last_dispatch.c.
 */
#include "../../src/papi.h"

xy_t xy;

XY_MODULE_API int
probe_value(int x)
{
	return x + 100;
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
