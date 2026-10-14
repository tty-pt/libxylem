/* mod_no_abi.c — a correct module that fails to declare its context ABI.
 *
 * Companion to mod_stale_ctx.c: that one declares a *wrong* descriptor, this
 * one declares none.  A module that does not export xy_ctx_abi() necessarily
 * predates the symbol, and so predates the contract that makes the size
 * checkable — there is nothing to check it against, so it must be refused for
 * the same reason.
 *
 * Note it exports get_xy_ptr by hand: a module with no get_xy_ptr at all never
 * reaches the ABI check, because mod_load_bind_xy returns early when it has no
 * context to write into.  Its context object is the right size (it uses the
 * host mirror xy_t, which is statically asserted to match struct xy_ctx), so
 * it would work perfectly if only it said so.  Refusing it is a deliberate
 * cost, and this fixture is what keeps that cost honest.
 */
#include <ttypt/xy.h>
#include "../../src/papi.h"
#include "mod_ctx_abi_stub.h"

xy_t xy;

XY_MODULE_API xy_t *get_xy_ptr(void)
{
	return &xy;
}

XY_MODULE_API int on_tick(int dt)
{
	return dt + 1;
}

XY_MODULE_API void xy_install(void)
{
}
