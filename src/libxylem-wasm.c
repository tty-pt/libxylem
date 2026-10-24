#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../include/ttypt/xy.h"

#define XY_WASM_MAX_HOOKS 256

typedef struct xy_wasm_listener {
	xy_adapter_t *adapter;
	struct xy_wasm_listener *next;
} xy_wasm_listener_t;

typedef struct {
	char name[64];
	xy_wasm_listener_t *listeners;
	int hook_id;
} xy_wasm_hook_slot_t;

static xy_wasm_hook_slot_t g_xy_hooks[XY_WASM_MAX_HOOKS];
static size_t g_xy_hook_count = 0;
static int g_xy_err = XY_OK;
static unsigned g_xy_last_ran = 0;
static void *g_xy_last_retp = NULL;
static xy_adapter_t *g_xy_current_adapter = NULL;

int xy_errno(void) {
	return g_xy_err;
}

const char *xy_strerror(int err) {
	switch (err) {
	case XY_OK: return "Success";
	case XY_ERR_NOTFOUND: return "Hook or listener not found";
	case XY_ERR_INVALID: return "Invalid argument";
	case XY_ERR_TOOBIG: return "Return size exceeds limit";
	default: return "Xylem error";
	}
}

unsigned xy_areg(char *name, xy_adapter_t *adapter) {
	if (!name || !adapter) {
		g_xy_err = XY_ERR_INVALID;
		return (unsigned)-1;
	}
	if (adapter->ret_size > XY_MAX_RET_SIZE) {
		g_xy_err = XY_ERR_TOOBIG;
		return (unsigned)-1;
	}

	for (size_t i = 0; i < g_xy_hook_count; i++) {
		if (strcmp(g_xy_hooks[i].name, name) == 0) {
			adapter->hook_id = g_xy_hooks[i].hook_id;
			/* Append listener at tail */
			xy_wasm_listener_t *l = (xy_wasm_listener_t *)malloc(sizeof(xy_wasm_listener_t));
			if (!l) { g_xy_err = XY_ERR_INIT; return (unsigned)-1; }
			l->adapter = adapter;
			l->next = NULL;
			if (!g_xy_hooks[i].listeners) {
				g_xy_hooks[i].listeners = l;
			} else {
				xy_wasm_listener_t *tail = g_xy_hooks[i].listeners;
				while (tail->next) tail = tail->next;
				tail->next = l;
			}
			g_xy_err = XY_OK;
			return (unsigned)adapter->hook_id;
		}
	}

	if (g_xy_hook_count >= XY_WASM_MAX_HOOKS) {
		g_xy_err = XY_ERR_TOOBIG;
		return (unsigned)-1;
	}

	int id = (int)g_xy_hook_count++;
	strncpy(g_xy_hooks[id].name, name, sizeof(g_xy_hooks[id].name) - 1);
	g_xy_hooks[id].name[sizeof(g_xy_hooks[id].name) - 1] = '\0';
	g_xy_hooks[id].hook_id = id;

	xy_wasm_listener_t *l = (xy_wasm_listener_t *)malloc(sizeof(xy_wasm_listener_t));
	if (!l) { g_xy_err = XY_ERR_INIT; return (unsigned)-1; }
	l->adapter = adapter;
	l->next = NULL;
	g_xy_hooks[id].listeners = l;

	adapter->hook_id = id;
	g_xy_err = XY_OK;
	return (unsigned)id;
}

int xy_call(void *retp, xy_adapter_t *reg, void *arg) {
	if (!reg) {
		g_xy_err = XY_ERR_INVALID;
		return XY_ERR_INVALID;
	}

	int id = reg->hook_id;
	if (id < 0 || (size_t)id >= g_xy_hook_count || strcmp(g_xy_hooks[id].name, reg->name) != 0) {
		id = -1;
		for (size_t i = 0; i < g_xy_hook_count; i++) {
			if (strcmp(g_xy_hooks[i].name, reg->name) == 0) {
				id = (int)i;
				reg->hook_id = id;
				break;
			}
		}
	}

	if (id < 0 || !g_xy_hooks[id].listeners) {
		if (retp && reg->ret_size > 0) {
			memset(retp, 0, reg->ret_size);
		}
		g_xy_err = XY_ERR_NOTFOUND;
		return XY_ERR_NOTFOUND;
	}

	g_xy_current_adapter = reg;
	g_xy_last_retp = retp;
	g_xy_last_ran = 0;

	xy_wasm_listener_t *cur = g_xy_hooks[id].listeners;
	int ran = 0;
	while (cur) {
		if (cur->adapter && cur->adapter->call) {
			cur->adapter->call(retp, NULL, arg);
			ran++;
			g_xy_last_ran = ran;
		}
		cur = cur->next;
	}

	g_xy_err = XY_OK;
	return XY_OK;
}

int xy_call_self(void *retp, xy_adapter_t *reg, void *arg) {
	return xy_call(retp, reg, arg);
}

int xy_last(void *ret) {
	if (!g_xy_last_ran) {
		g_xy_err = XY_ERR_NOTFOUND;
		return XY_ERR_NOTFOUND;
	}
	if (ret && g_xy_last_retp && g_xy_current_adapter) {
		memcpy(ret, g_xy_last_retp, g_xy_current_adapter->ret_size);
	}
	g_xy_err = XY_OK;
	return XY_OK;
}

uint64_t xy_current_region(void) { return 0; }
uint8_t xy_current_region_plen(void) { return 0; }
int xy_with_region(uint64_t id, uint8_t plen, xy_scope_fn_t *fn, void *ud) {
	(void)id; (void)plen;
	if (fn) return fn(ud);
	return 0;
}
int xy_claim_at(uint64_t id, uint8_t plen, xy_claim_handler_fn_t *fn, void *ud) {
	(void)id; (void)plen; (void)fn; (void)ud;
	return 0;
}
int xy_region_exists(uint64_t id, uint8_t plen) {
	(void)id; (void)plen;
	return 0;
}
int xy_deny(const char *what, xy_deny_type_t type) {
	(void)what; (void)type;
	return 0;
}
int xy_load(char *fname) {
#ifdef __wasm__
	if (!fname) return XY_ERR_INVALID;
	return wasm_xy_load_module(fname, strlen(fname));
#else
	(void)fname;
	return 0;
#endif
}
int xy_unload(char *fname) {
#ifdef __wasm__
	if (!fname) return XY_ERR_INVALID;
	return wasm_xy_unload_module(fname, strlen(fname));
#else
	(void)fname;
	return 0;
#endif
}
int xy_reload(char *fname) {
	(void)fname;
	return 0;
}

void xy_init_ctx(struct xy_ctx *ctx) {
	if (!ctx) return;
	memset(ctx, 0, sizeof(*ctx));
	ctx->call = xy_call;
	ctx->call_self = xy_call_self;
	ctx->areg = xy_areg;
	ctx->last = xy_last;
	ctx->load = xy_load;
	ctx->unload = xy_unload;
	ctx->reload = xy_reload;
	ctx->region_exists = xy_region_exists;
	ctx->current_region = xy_current_region;
	ctx->current_region_plen = xy_current_region_plen;
	ctx->with_region = xy_with_region;
	ctx->claim_at = xy_claim_at;
	ctx->deny = xy_deny;
}

struct xy_ctx *get_xy_ptr(void) __attribute__((weak));

void xy_wasm_init(void) {
	if (get_xy_ptr) {
		struct xy_ctx *p = get_xy_ptr();
		if (p) xy_init_ctx(p);
	}
}
AUTO_INIT
void (*xy_wasm_init_p)(void) = xy_wasm_init;
