#ifndef REGIONID_HOOKS_H
#define REGIONID_HOOKS_H

#include <ttypt/xy.h>

/*
 * Hooks called by the host but implemented only inside the CP-3 chain
 * fixtures (mod_regionid_{a,b,c}).  These must be XY_HOOK_DEF, not
 * XY_HOOK_DECL: this is the definition TU, and XY_DECL (the shared-header
 * form) emits only a private static adapter with hook_id = -1 and no
 * registrar, so xy_call() would resolve nothing and return
 * XY_ERR_NOTFOUND.  XY_DEF adds the exported adapter and an AUTO_INIT
 * .init_array constructor that calls xy_areg().
 */
XY_HOOK_DEF(int, ri_hook_a, int, dummy);
XY_HOOK_DEF(int, ri_hook_b, int, dummy);
XY_HOOK_DEF(int, ri_hook_c, int, dummy);

#endif
