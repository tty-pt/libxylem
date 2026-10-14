#ifndef CLAIMAT_HOOKS_H
#define CLAIMAT_HOOKS_H

#include <ttypt/xy.h>

/*
 * Hooks called by the host but implemented only inside the CP-4 scope
 * fixtures (mod_ca_*).  These must be XY_HOOK_DEF, not XY_HOOK_DECL:
 * XY_DECL emits only a private static adapter with hook_id = -1 and no
 * registrar, so nothing would resolve and every dispatch would return
 * XY_ERR_NOTFOUND.  XY_DEF adds the exported adapter and the AUTO_INIT
 * .init_array constructor that calls xy_areg().
 */
XY_HOOK_DEF(int, ca_probe, int, tag);

#endif