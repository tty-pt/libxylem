/* mod_ctx_abi.h — the ABI handshake for hand-rolled test fixtures.
 *
 * Fixtures that use xy-mod.h get xy_ctx_abi() for free.  The ones that
 * deliberately hand-roll `get_xy_ptr` (to model an external module that only
 * knows the bare API) must declare their context ABI themselves, exactly as a
 * real external module must.
 *
 * Without this the host refuses the load: it cannot prove the fixture's
 * xy_t/xy_ctx object is as large as the host's, and writing into a smaller one
 * overruns it into adjacent BSS.  See XY_CTX_ABI_VER in include/ttypt/xy.h.
 */
#ifndef MOD_CTX_ABI_H
#define MOD_CTX_ABI_H

#include <ttypt/xy.h>

XY_MODULE_API uint64_t xy_ctx_abi(void);

uint64_t xy_ctx_abi(void)
{
	return XY_CTX_ABI_DESC;
}

#endif
