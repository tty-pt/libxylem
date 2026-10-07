#ifndef LIBXY_INTERNAL_H
#define LIBXY_INTERNAL_H

#define _GNU_SOURCE

#include "../include/ttypt/xy.h"

#ifdef _WIN32
#  include <windows.h>
extern DWORD xy_err_tls;
#  define XY_SET_ERR(e) TlsSetValue(xy_err_tls, (LPVOID)(intptr_t)(e))
#  define XY_GET_ERR() ((int)(intptr_t)TlsGetValue(xy_err_tls))
#else
extern int xy_err_val;
#  define XY_SET_ERR(e) (xy_err_val = (e))
#  define XY_GET_ERR() (xy_err_val)
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <ttypt/qsys.h>
#include <ttypt/corm.h>

#include "papi.h"

#define MOD_MASK    0x7FFF
#define SICA_MASK   0x7FFF
#define REGION_MASK 0x7FFF

#ifndef likely
#  define likely(x)   __builtin_expect(!!(x), 1)
#  define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct xy_runtime_s {
	unsigned        mod_hd;
	unsigned        mod_by_region_hd;
	unsigned        sica_hd;
	unsigned        path_intern_hd;
	unsigned        region_hd;
	unsigned        hook_id_hd;
	int             xy_hook_id_counter;
	xy_adapter_t **xy_adapter_by_id;
	int             xy_adapter_by_id_cap;
	uint32_t        xy_ptr_type;
	uint32_t        xy_mod_entry_type;
	uint32_t        xy_region_entry_type;
	uint32_t        region_key_type;
	uint32_t        xy_int_type;
	uint32_t        mod_key_type_id;
	int             xy_inited;
	int             xy_mod_count;
} xy_runtime_t;

extern xy_runtime_t xy_rt;

#define mod_hd                (xy_rt.mod_hd)
#define mod_by_region_hd      (xy_rt.mod_by_region_hd)
#define sica_hd               (xy_rt.sica_hd)
#define path_intern_hd        (xy_rt.path_intern_hd)
#define region_hd             (xy_rt.region_hd)
#define hook_id_hd            (xy_rt.hook_id_hd)
#define xy_hook_id_counter   (xy_rt.xy_hook_id_counter)
#define xy_adapter_by_id     (xy_rt.xy_adapter_by_id)
#define xy_adapter_by_id_cap (xy_rt.xy_adapter_by_id_cap)
#define xy_ptr_type          (xy_rt.xy_ptr_type)
#define xy_mod_entry_type    (xy_rt.xy_mod_entry_type)
#define xy_region_entry_type (xy_rt.xy_region_entry_type)
#define region_key_type      (xy_rt.region_key_type)
#define xy_int_type          (xy_rt.xy_int_type)
#define mod_key_type          (xy_rt.mod_key_type_id)
#define xy_inited            (xy_rt.xy_inited)
#define xy_mod_count         (xy_rt.xy_mod_count)

typedef xy_t* (*get_xy_func_t)(void);

#define XY_FN_NOT_FOUND ((void *)(uintptr_t)1)

extern xy_t xy;
extern __thread unsigned            xy_last_ran;
extern __thread char                xy_last_retbuf[XY_MAX_RET_SIZE];
extern __thread void               *xy_last_retp;
extern __thread uint64_t            xy_current_region_id;
extern __thread xy_region_entry_t *xy_current_region_entry;
extern __thread xy_mod_entry_t    *xy_loading_mod;
extern __thread xy_t              *xy_pending_ctx;

void set_current_region(uint64_t id, xy_region_entry_t *entry);
void *corm_ptr(const void *value);
void *module_lookup_symbol_raw(void *handle, const char *symbol);
int module_lookup_symbol_fn(void *handle, const char *symbol, void *fn_out, size_t fn_size);
/* True when sym is defined in handle's own object, not merely visible
 * through its DT_NEEDED dependencies (which dlsym follows on glibc, dyld
 * and OpenBSD).  Takes the module entry, not the bare handle: platforms
 * without dlinfo()/RTLD_DI_LINKMAP (macOS, OpenBSD) identify the object
 * by the file it was loaded from instead. */
int module_symbol_is_local(const xy_mod_entry_t *me, void *sym);
/* True when both paths name the same existing file, by string or by
 * st_dev/st_ino (the loader may report a canonicalised spelling of the
 * path we passed to dlopen).  Also the module-identity fallback used on
 * platforms without dlinfo(); exported for test_objectpath. */
int module_same_file(const char *a, const char *b);
/* Canonical path used for module identity and dlopen: appends the platform
 * extension and resolves via realpath(), searching LD_LIBRARY_PATH (and
 * DYLD_LIBRARY_PATH) for bare sonames exactly as dlopen() does, so the
 * stored identity matches what dladdr() later reports. Exported for
 * test_objectpath; callers own the returned string. */
char *module_load_path(const char *fname);
void xy_zero_ret(void *retp, const xy_adapter_t *reg);
static inline void __attribute__((always_inline))
xy_set_last_ret(const void *retp, size_t ret_size)
{
	if (likely(retp && ret_size > 0)) {
		if (likely(ret_size <= sizeof(uint64_t))) {
			uint64_t v = 0;
			memcpy(&v, retp, ret_size);
			memcpy(xy_last_retbuf, &v, sizeof(uint64_t));
		} else {
			memcpy(xy_last_retbuf, retp, ret_size);
		}
		xy_last_retp = xy_last_retbuf;
	} else {
		xy_last_retp = NULL;
	}
}

static inline int __attribute__((always_inline))
module_has_hook_implemented(const xy_mod_entry_t *me, int hook_id)
{
	if (unlikely(!me || hook_id < 0))
		return 0;
	int word = hook_id / 64;
	if (unlikely(word >= me->hook_impl_words))
		return 0;
	return (me->hook_impl_bits[word] & ((uint64_t)1 << (hook_id % 64))) != 0;
}
void module_mark_hook_implemented(xy_mod_entry_t *me, int hook_id);
int module_ensure_fn_cache_cap(xy_mod_entry_t *me, int needed);
/* ---- Region identity: (id, plen), never id alone ---- */

/* Build the corm key for a region identity.  Distinct regions may share an
 * id and differ only in plen (root (0,0) vs a left-half child (0,16)), so
 * plen is part of the key and is never inferred from an id. */
_Static_assert(sizeof(xy_region_key_t) == 9,
	"xy_region_key_t must be exactly 9 packed bytes: it is a bytewise-compared "
	"corm key, so padding bytes would differ between store and lookup and "
	"silently turn every region lookup into a miss");
static inline void
region_key(xy_region_key_t *k, uint64_t id, uint8_t plen)
{
	k->id = id;
	k->plen = plen;
}

/* Look up a region by full identity.  Returns NULL if no region with that
 * exact (id, plen) exists — an id that exists at another width is a
 * different region and is likewise not found. */
xy_region_entry_t *region_lookup(uint64_t id, uint8_t plen);

/* Containment mask for a prefix of @p plen bits.
 *
 * A region id carries its prefix in the HIGH bits above plen: region_alloc_slot()
 * builds a child as `parent->id | (s << (64 - cplen))`, so the slot index lands
 * above the parent's width and the low plen bits stay zero.  Containment must
 * therefore mask the HIGH plen bits, and this is not a stylistic choice —
 * masking the low bits agrees for the root (whose mask is 0) and then silently
 * diverges: it reports region_alloc_slot's siblings as nested, i.e.
 * (1<<48,16) "inside" (0,16), because both have zero low bits.  Two siblings
 * must never be parent and child.
 *
 * plen 0 masks to 0, which is what makes the root a prefix of everything and
 * gives `(id & region_mask(0)) == root->id` unconditionally. */
static inline uint64_t
region_mask(uint8_t plen)
{
	if (plen == 0)
		return 0;
	if (plen >= 64)
		return ~(uint64_t)0;
	return ~(uint64_t)0 << (64 - plen);
}

/* Nearest existing ancestor of (id, plen): the region of largest plen_A < plen
 * with (id & region_mask(plen_A)) == A->id.  The root always qualifies.  NULL
 * only when the root is absent, which xy_runtime_ensure() rules out. */
xy_region_entry_t *region_nearest_ancestor(uint64_t id, uint8_t plen);

/* The caller's current region entry from the thread-local, with no hash
 * lookup.  NULL before the first region_ensure_root(); use
 * region_lookup_current() where the root must already exist. */
static inline xy_region_entry_t *
region_lookup_current(void)
{
	return xy_current_region_entry;
}

/* plen half of the caller's current region identity; 0 for the root and for
 * the pre-init state, which is correct for both (root's plen really is 0). */
static inline uint8_t
region_current_plen(void)
{
	xy_region_entry_t *e = xy_current_region_entry;
	return e ? e->plen : 0;
}

const char *module_path_intern(char *path);
void path_intern_free_all(void);
int region_ancestor_chain(xy_region_entry_t *start,
                          xy_region_entry_t **chain, int chain_cap);
void region_propagate_deny(xy_region_entry_t *entry);
void region_entry_free(xy_region_entry_t *e);
void region_dispatch_gen_bump(xy_region_entry_t *entry);
void region_mark_subtree_dirty(xy_region_entry_t *entry);
int region_rebuild_subtree_mods(xy_region_entry_t *root);
void region_ensure_root(void);
void mod_key(char *buf, size_t buf_len, const char *path,
             uint64_t region_id, uint8_t plen);
size_t mod_key_measure(const void *data);

typedef struct {
	xy_mod_entry_t *entry;
	char            *key;
	char            *load_path;
	int              err;
} xy_lookup_result_t;

void module_lookup_result_free(xy_lookup_result_t *lookup);
xy_lookup_result_t module_lookup_from_fname(const char *fname, uint64_t region_id,
                                           uint8_t plen);
void module_rekey_for_claim(const char *caller,
                            uint64_t parent_id, uint8_t parent_plen,
                            uint64_t child_id, uint8_t child_plen);
void module_remove_denies(xy_region_entry_t *re, const char *path);
int module_is_denied(xy_region_entry_t *re, const char *path);
void module_region_detach(xy_mod_entry_t *entry);
void module_region_insert_after(xy_region_entry_t *re, xy_mod_entry_t *entry,
                                xy_mod_entry_t *after);
void module_region_append(xy_region_entry_t *re, xy_mod_entry_t *entry);
void module_clear_fn_cache_for_handle(xy_mod_entry_t *entry);
void module_free_entry(xy_mod_entry_t *entry, int close_handle);

typedef struct {
	xy_lookup_result_t lookup;
	void               *handle;
	char               *stable_fname;
	char               *stable_key;
	const char         *interned_load_path;
	char               *tmp_load_path;
	xy_mod_entry_t    *mod_entry;
	xy_region_entry_t *inherited_reg;
	uint64_t            inherited_region_id;
	uint8_t             inherited_region_plen;
	uint64_t            prev_region_id;
	xy_region_entry_t *prev_region_entry;
	int                 published_entry;
	int                 context_active;
} xy_load_txn_t;

void mod_load_restore_context(xy_load_txn_t *tx);
int mod_load_abort(xy_load_txn_t *tx, int err);
int mod_load_open_handle(xy_load_txn_t *tx, char *fname);
int mod_load_try_reuse_existing(xy_load_txn_t *tx);
int mod_load_alloc_entry(xy_load_txn_t *tx, char *fname);
int mod_load_publish_entry(xy_load_txn_t *tx);
int mod_load_bind_xy(xy_load_txn_t *tx);
int mod_load_enter_context(xy_load_txn_t *tx);
int mod_load_claim_if_needed(xy_load_txn_t *tx);
int mod_load_run_install(xy_load_txn_t *tx);
void module_remove_path_owned_entries(xy_region_entry_t *re, const char *path,
                                      void *handle);

int _mod_unload(char *fname, uint64_t region_id, uint8_t plen);
int _mod_run(void *sl, const char *symbol);
int _xy_claim_for_load(const char *caller, uint64_t parent_id, uint8_t parent_plen,
                        uint8_t bits, void *sl);
/* xy_ctx carries no plen data field: a module that needs its own region's
 * width calls xy_current_region_plen(), which dispatch has already made
 * correct for the module being called. */
void _xy_init(void *ptr, const char *fname, uint64_t region_id);
int fn_cache_prewarm(xy_mod_entry_t *me);
int xy_runtime_ensure(void);

#endif
