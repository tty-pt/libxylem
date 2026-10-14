#ifndef REGION_STATE_HOOKS_H
#define REGION_STATE_HOOKS_H

#include <ttypt/xy.h>

/*
 * These must be XY_HOOK_DEF, not XY_HOOK_DECL.
 *
 * XY_HOOK_DECL is an alias of XY_DECL — the *shared-header* form.  It emits
 * only a private static adapter plus the call shim, with hook_id = -1, and no
 * registrar.  xy_call() resolves a hook id via corm_get(hook_id_hd, reg->name),
 * so an unregistered hook is XY_ERR_NOTFOUND and no listener ever runs.
 *
 * XY_HOOK_DEF is an alias of XY_DEF, which additionally emits the
 * default-visibility <name>_adapter symbol and an AUTO_INIT .init_array
 * constructor calling xy_areg(name, &<name>_adapter).  test_region_state.c is
 * the definition TU for these hooks, so it needs the self-registering form.
 */
XY_HOOK_DEF(int, rs_increment, int, dummy);
XY_HOOK_DEF(int, rs_get,       int, dummy);

#endif
