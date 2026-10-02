/* mod_ctx_abi_stub.h — deliberately does NOT define xy_ctx_abi().
 *
 * mod_no_abi.c includes this instead of mod_ctx_abi.h so the fixture can be a
 * fully modern module (correct struct xy_ctx via xy-mod.h) that nonetheless
 * fails to export the handshake — the "old module dropped in" case.
 */
#ifndef MOD_CTX_ABI_STUB_H
#define MOD_CTX_ABI_STUB_H

/* Intentionally empty. See mod_no_abi.c. */

#endif
