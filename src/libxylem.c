#include "libxylem-internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#ifndef O_BINARY
#define O_BINARY 0	/* MinGW open() is text-mode by default; binary for pipes/files */
#endif
#ifndef _WIN32
#include <dlfcn.h>	/* dladdr/Dl_info only — address→module introspection,
			 * distinct from the dlopen/dlsym/dlclose/dlerror quad
			 * that now goes through libqsys's portable wrappers. */
/* <link.h> and dlinfo()/RTLD_DI_LINKMAP exist only on the ELF/BSD linkers.
 * macOS has no <link.h> and its dlfcn.h declares neither dlinfo() nor
 * RTLD_DI_LINKMAP; OpenBSD has no <link.h> either.  Both still give us
 * dladdr(), so module identity falls back to comparing the file the loader
 * mapped (see module_same_file / module_symbol_is_local).
 *
 * The platform list is spelled out instead of testing
 * #if defined(RTLD_DI_LINKMAP): glibc declares that as an enum member,
 * which the preprocessor never sees. */
#if defined(__linux__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__DragonFly__)
#include <link.h>	/* struct link_map — only where dlinfo() exists */
#define XY_HAVE_DLINFO 1
#if defined(__FreeBSD__) && defined(__FreeBSD_version) && \
    __FreeBSD_version >= 1300000
/* FreeBSD >= 13 split the fields: l_base is the mapped base address that
 * dladdr() reports as dli_fbase, l_addr became the load offset.  Older
 * FreeBSD (and glibc/musl/NetBSD/DragonFly) put the base in l_addr. */
#define XY_LINKMAP_BASE(lm) ((void *)(lm)->l_base)
#else
#define XY_LINKMAP_BASE(lm) ((void *)(lm)->l_addr)
#endif
#endif
#endif

enum opts {
	OPT_DETACH = 1,
};

xy_t xy;
xy_runtime_t xy_rt = {
	.mod_key_type_id = CM_MISS,
};

#ifdef _WIN32
DWORD xy_err_tls = TLS_OUT_OF_INDEXES;
#else
int xy_err_val;
#endif

/* T1.5: Per-call mutable state extracted from xy_last_adapter to
 * avoid the 88-byte memcpy on fast path. xy_last() reads these instead
 * of the full struct. */
__thread unsigned xy_last_ran;
__thread char     xy_last_retbuf[XY_MAX_RET_SIZE];
__thread void    *xy_last_retp;

/* Thread-local current region — id and a direct pointer kept in sync.
 * xy_current_region_entry may be NULL before the first runtime init;
 * code that needs the entry must guard or call region_ensure_root() first. */
__thread uint64_t           xy_current_region_id = XY_REGION_ROOT;
__thread xy_region_entry_t *xy_current_region_entry = NULL;

/* Thread-local pointer to the currently-loading module entry.
 * Set during xy_install() so child xy_load() calls can register their
 * parent_entry for cascade-unload tracking. */
__thread xy_mod_entry_t *xy_loading_mod = NULL;

/* Thread-local xy context pointer self-registered by a Rust module's
 * .init_array constructor (via xy_self_init_ctx).  The constructor runs
 * inside dlopen(), before mod_load_bind_xy executes.  mod_load_bind_xy
 * reads this as a fallback when get_xy_ptr is absent or unreachable, then
 * clears it.  One pointer per thread is sufficient because dlopen is
 * synchronous and modules are loaded one at a time per thread. */
__thread xy_t *xy_pending_ctx = NULL;

void
xy_self_init_ctx(struct xy_ctx *ctx)
{
	xy_pending_ctx = (xy_t *)ctx;
}
/* Keep the id/pointer pair in sync atomically (within a thread). */
void
set_current_region(uint64_t id, xy_region_entry_t *entry)
{
	xy_current_region_id = id;
	xy_current_region_entry = entry;
}

/* -------------------------------------------------------------------------
 * Region ID helpers
 *
 * Region IDs are plain opaque uint64_t prefix values — all 64 bits are
 * the address.  The prefix length (plen) lives in xy_region_entry_t, not
 * in the ID itself.
 *
 * XY_REGION_ROOT = 0: plen=0 in its entry, matches everything.
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

void *
corm_ptr(const void *value)
{
	return value ? *(void * const *) value : NULL;
}

void *
module_lookup_symbol_raw(void *handle, const char *symbol)
{
	return qsys_dlsym(handle, symbol);
}

int
module_lookup_symbol_fn(void *handle, const char *symbol, void *fn_out, size_t fn_size)
{
	void *sym = module_lookup_symbol_raw(handle, symbol);
	if (!sym)
		return 0;
	memcpy(fn_out, &sym, fn_size);
	return 1;
}

void
xy_zero_ret(void *retp, const xy_adapter_t *reg)
{
	if (retp && reg)
		memset(retp, 0, reg->ret_size);
}

int
module_ensure_hook_impl_words(xy_mod_entry_t *me, int hook_id)
{
	if (!me || hook_id < 0)
		return -1;
	int needed_words = (hook_id / 64) + 1;
	if (needed_words <= me->hook_impl_words)
		return 0;
	uint64_t *tmp = realloc(me->hook_impl_bits,
	                        (size_t)needed_words * sizeof(*tmp));
	if (unlikely(!tmp))
		return -1;
	for (int i = me->hook_impl_words; i < needed_words; i++)
		tmp[i] = 0;
	me->hook_impl_bits = tmp;
	me->hook_impl_words = needed_words;
	return 0;
}

void
module_mark_hook_implemented(xy_mod_entry_t *me, int hook_id)
{
	if (!me || hook_id < 0)
		return;
	if (module_ensure_hook_impl_words(me, hook_id) < 0)
		return;
	me->hook_impl_bits[hook_id / 64] |= (uint64_t)1 << (hook_id % 64);
}

int
module_ensure_fn_cache_cap(xy_mod_entry_t *me, int needed)
{
	if (!me || needed <= me->fn_cache_cap)
		return 0;
	void **tmp = realloc(me->fn_cache, needed * sizeof(void *));
	if (unlikely(!tmp))
		return -1;
	for (int i = me->fn_cache_cap; i < needed; i++)
		tmp[i] = NULL;
	me->fn_cache = tmp;
	me->fn_cache_cap = needed;
	return 0;
}

/*
 * Look up an xy_region_entry_t by its FULL identity (id, plen).
 *
 * The key carries both halves because distinct regions may share an id and
 * differ only in width — the root is (0,0) while a child granted the
 * leftmost bits is (0,16).  A lookup by id alone would be ambiguous, so there
 * is deliberately no such overload: callers that hold only an id are doing a
 * query, not resolving a region, and must say which width they mean.
 *
 * Returns NULL if no region with that exact identity exists.
 */
xy_region_entry_t *
region_lookup(uint64_t id, uint8_t plen)
{
	xy_region_key_t k;
	region_key(&k, id, plen);
	return (xy_region_entry_t *)corm_ptr(corm_get(region_hd, &k));
}

const char *
module_path_intern(char *path)
{
	if (!path)
		return NULL;
	if (!path_intern_hd)
		path_intern_hd = corm_open(NULL, NULL, CM_STR, xy_ptr_type, MOD_MASK, 0);
	if (!path_intern_hd) {
		free(path);
		return NULL;
	}
	const void *existing = corm_get(path_intern_hd, path);
	if (existing) {
		const char *interned = corm_ptr(existing);
		free(path);
		return interned;
	}
	corm_put(path_intern_hd, path, &path);
	return path;
}

void
path_intern_free_all(void)
{
	if (!path_intern_hd)
		return;
	corm_close(path_intern_hd);
	path_intern_hd = 0;
}

/*
 * Build an ancestor chain for region_id, root-first.
 * Walks the parent pointer chain from region_id up to root,
 * collecting all ancestors (including the region itself),
 * then reverses so root comes first.
 * Returns number of entries filled.
 */
/*
 * Build an ancestor chain for the given entry, root-first.
 * start must be non-NULL; the hash lookup is the caller's responsibility.
 * Returns number of entries filled.
 */
int
region_ancestor_chain(xy_region_entry_t *start,
                      xy_region_entry_t **chain, int chain_cap)
{
	int n = 0;
	xy_region_entry_t *e = start;
	while (e && n < chain_cap) {
		chain[n++] = e;
		e = e->parent;
	}
	/* Reverse so root (parent==NULL) is first */
	for (int a = 0, b = n - 1; a < b; a++, b--) {
		xy_region_entry_t *tmp = chain[a];
		chain[a] = chain[b];
		chain[b] = tmp;
	}
	return n;
}

/* Propagate a subtree flag upward from entry to root. */
void
region_propagate_deny(xy_region_entry_t *entry)
{
	for (xy_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_flags |= XY_SUBTREE_HAS_DENY;
}

static void
deny_list_free(xy_deny_entry_t *head)
{
	while (head) {
		xy_deny_entry_t *next = head->next;
		free(head);
		head = next;
	}
}

static void
deny_list_free_owned(xy_deny_entry_t *head)
{
	while (head) {
		xy_deny_entry_t *next = head->next;
		free(head->value);
		free(head);
		head = next;
	}
}

void
region_entry_free(xy_region_entry_t *e)
{
	if (!e) return;
	deny_list_free_owned(e->denied_hooks);
	deny_list_free(e->denied_modules);
	if (e->denied_hooks_set)
		corm_close(e->denied_hooks_set);
	if (e->denied_modules_set)
		corm_close(e->denied_modules_set);
	for (int i = 0; i < e->hook_dispatch_cap; i++)
		free(e->hook_dispatch[i].slots);
	free(e->hook_dispatch);
	free(e->subtree_mods);
	free(e);
}

void
region_dispatch_gen_bump(xy_region_entry_t *entry)
{
	for (xy_region_entry_t *e = entry; e; e = e->parent) {
		e->dispatch_gen++;
		if (unlikely(e->dispatch_gen == 0))
			e->dispatch_gen = 1;
	}
}

/* T1.2: mark the subtree module cache dirty along the ancestor chain from
 * `entry` (inclusive) up to the root. Called whenever a module is added or
 * removed from any region, or when a new child region is claimed. Also bumps
 * the per-region dispatch generation along the ancestor path. O(depth). */
void
region_mark_subtree_dirty(xy_region_entry_t *entry)
{
	region_dispatch_gen_bump(entry);
	for (xy_region_entry_t *e = entry; e; e = e->parent)
		e->subtree_mods_dirty = 1;
}

static int
region_subtree_mods_push(xy_region_entry_t *root, xy_mod_entry_t *me)
{
	if (root->subtree_mods_count >= root->subtree_mods_cap) {
		int new_cap = root->subtree_mods_cap ? root->subtree_mods_cap * 2 : 8;
		xy_mod_entry_t **tmp = realloc(root->subtree_mods,
		                                (size_t)new_cap * sizeof(*tmp));
		if (unlikely(!tmp))
			return -1;
		root->subtree_mods = tmp;
		root->subtree_mods_cap = new_cap;
	}

	root->subtree_mods[root->subtree_mods_count++] = me;
	return 0;
}

static int
region_collect_subtree_mods(xy_region_entry_t *root, xy_region_entry_t *node)
{
	for (xy_mod_entry_t *me = node->mods_head; me; me = me->region_next) {
		if (region_subtree_mods_push(root, me) < 0)
			return -1;
	}

	for (xy_region_entry_t *child = node->children_head;
	     child; child = child->sibling_next) {
		if (region_collect_subtree_mods(root, child) < 0)
			return -1;
	}

	return 0;
}

/* Rebuild region->subtree_mods[] in DFS order (region's own modules first,
 * then each child's subtree recursively). Returns 0 on success, -1 on
 * allocation failure. */
int
region_rebuild_subtree_mods(xy_region_entry_t *root)
{
	if (!root)
		return 0;
	root->subtree_mods_count = 0;
	if (region_collect_subtree_mods(root, root) < 0)
		return -1;

	root->subtree_mods_dirty = 0;
	return 0;
}

/* Ensure the root region entry exists */
void
region_ensure_root(void)
{
	xy_region_entry_t *root = region_lookup(XY_REGION_ROOT, 0);
	if (!root) {
		root = calloc(1, sizeof(*root));
		root->id         = XY_REGION_ROOT;
		root->depth      = 0;
		root->child_bits = 0;
		root->plen       = 0;
		root->owner_path = NULL; /* host owns root */
		root->dispatch_gen = 1;
		root->parent     = NULL; /* root has no parent */
		{
			xy_region_key_t k;
			region_key(&k, root->id, root->plen);
			corm_put(region_hd, &k, &root);
		}
	}
	/* Sync the thread-local pointer to the root entry.
	 *
	 * The test must NOT be `xy_current_region_id == XY_REGION_ROOT`: a child
	 * granted the leftmost bits also has id 0, so that condition would reset
	 * the current region to root when entering (0,16).  Compare the width
	 * instead — only the root region has plen 0. */
	if (!xy_current_region_entry || region_current_plen() == 0) {
		xy_current_region_entry = root;
		xy_current_region_id    = root->id;
	}
}

/* -------------------------------------------------------------------------
 * Module key helpers
 *
 * mod_hd is keyed by "path\0<region_hex><plen_hex>" to allow the same .so
 * in multiple regions (each gets its own xy_mod_entry_t).  The width is part
 * of the key because two regions may share an id — (0,16) and (0,17) — and
 * the same .so in both must be two independent module entries.
 * ------------------------------------------------------------------------- */

void
mod_key(char *buf, size_t buf_len, const char *path,
        uint64_t region_id, uint8_t plen)
{
	snprintf(buf, buf_len, "%s%c%016llx%02x",
	         path, '\0', (unsigned long long)region_id, (unsigned)plen);
}

/* Key length includes the embedded NUL — fixed 19-byte suffix:
 * NUL + 16 hex digits (id) + 2 hex digits (plen). */
#define MOD_KEY_SUFFIX_LEN 19

static size_t
mod_key_len(const char *path)
{
	return strlen(path) + MOD_KEY_SUFFIX_LEN;
}

static char *module_search_path_list(const char *list, const char *leaf,
                                     size_t leaflen);

/*
 * Resolve the actual shared-library path used for identity and dlopen.
 * We keep module_path in xy_t as the caller's original fname, but module
 * identity is keyed by the resolved file so aliases like "../mods/song" and
 * "mods/song" collapse to the same entry.
 *
 * A bare soname ("libaxil-auth", no slash) is additionally searched for in
 * LD_LIBRARY_PATH (DYLD_LIBRARY_PATH on macOS), mirroring what dlopen()
 * itself does. Without this the stored identity stays a bare "name.so"
 * while dladdr() reports the full mapped path, and the no-dlinfo locality
 * fallback (macOS, OpenBSD) compares the two spellings, finds a mismatch
 * and drops every hook of the module. Unresolvable names keep the old
 * behaviour exactly: the bare "name.so" is returned for dlopen to fail on.
 */
char *
module_load_path(const char *fname)
{
#ifdef _WIN32
	const char *ext = ".dll";
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = malloc(flen + elen + 1);
	if (!buf)
		return NULL;
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);

	DWORD need = GetFullPathNameA(buf, 0, NULL, NULL);
	if (need == 0)
		return buf;

	char *full = malloc((size_t)need + 1);
	if (!full) {
		free(buf);
		return NULL;
	}
	if (!GetFullPathNameA(buf, need + 1, full, NULL)) {
		free(buf);
		free(full);
		return NULL;
	}
	free(buf);
	return full;
#else
	const char *ext = ".so";
	size_t flen = strlen(fname);
	size_t elen = strlen(ext);
	char *buf = malloc(flen + elen + 1);
	if (!buf)
		return NULL;
	memcpy(buf, fname, flen);
	memcpy(buf + flen, ext, elen + 1);
	char *resolved = realpath(buf, NULL);
	if (resolved) {
		free(buf);
		return resolved;
	}
	/* realpath() does not search the loader path: a bare soname that
	 * only exists under LD_LIBRARY_PATH (the normal case for external
	 * libraries) would otherwise stay unresolved here while dlopen()
	 * finds it fine, leaving identity as a bare name no dladdr()
	 * spelling can match. Search the same directories dlopen()
	 * searches, in order: the environment first, then the compiled-in
	 * system defaults (a daemonized server commonly runs without
	 * LD_LIBRARY_PATH at all -- observed live: dlopen resolved via the
	 * defaults while getenv found nothing, so env-only search still
	 * dropped every hook). DT_RPATH/RUNPATH entries are deliberately
	 * not parsed here; env plus defaults covers real deployments. */
	if (!strchr(buf, '/')) {
		static const char *const ld_vars[] = {
			"LD_LIBRARY_PATH",
			"DYLD_LIBRARY_PATH",
		};
		/* Empty entries mean CWD; realpath() above already covered it. */
		static const char *const ld_defaults[] = {
			"/lib",
			"/usr/lib",
			"/usr/local/lib",
		};
		for (size_t v = 0;
		     v < sizeof(ld_vars) / sizeof(ld_vars[0]); v++) {
			const char *ld = getenv(ld_vars[v]);
			if (!ld || !*ld)
				continue;
			resolved = module_search_path_list(
			        ld, buf, flen + elen + 1);
			if (resolved) {
				free(buf);
				return resolved;
			}
		}
		for (size_t d = 0;
		     d < sizeof(ld_defaults) / sizeof(ld_defaults[0]); d++) {
			resolved = module_search_path_list(
			        ld_defaults[d], buf, flen + elen + 1);
			if (resolved) {
				free(buf);
				return resolved;
			}
		}
	}
	return buf;
#endif
}

/* Search one colon-separated directory list for `leaf` (already suffixed),
 * returning the canonical path of the first hit, or NULL. Shared by the
 * environment and the compiled-in defaults so both spellings resolve
 * identically. */
static char *
module_search_path_list(const char *list, const char *leaf, size_t leaflen)
{
	const char *p = list;

	while (*p) {
		const char *end = strchr(p, ':');
		size_t dlen = end ? (size_t)(end - p) : strlen(p);
		char *resolved = NULL;

		if (dlen > 0) {
			size_t clen = dlen + 1 + leaflen;
			char *cand = malloc(clen);
			if (!cand)
				return NULL;
			memcpy(cand, p, dlen);
			cand[dlen] = '/';
			memcpy(cand + dlen + 1, leaf, leaflen);
			resolved = realpath(cand, NULL);
			free(cand);
			if (resolved)
				return resolved;
		}
		p = end ? end + 1 : p + dlen;
	}
	return NULL;
}

static void module_rekey_region_index(xy_mod_entry_t *me,
                                      uint64_t parent_id, uint8_t parent_plen,
                                      uint64_t child_id, uint8_t child_plen);
static void module_rekey_loaded_entry(xy_mod_entry_t *me, const char *old_key,
                                      const char *load_path,
                                      uint64_t parent_id, uint8_t parent_plen,
                                      uint64_t child_id, uint8_t child_plen);

void
module_lookup_result_free(xy_lookup_result_t *lookup)
{
	if (!lookup)
		return;
	free(lookup->key);
	lookup->key = NULL;
	free(lookup->load_path);
	lookup->load_path = NULL;
}

xy_lookup_result_t
module_lookup_from_fname(const char *fname, uint64_t region_id, uint8_t plen)
{
	xy_lookup_result_t out = {0};
	out.load_path = module_load_path(fname);
	if (!out.load_path) {
		out.err = XY_ERR_INVALID;
		return out;
	}
	size_t key_len = mod_key_len(out.load_path);
	out.key = malloc(key_len);
	if (!out.key) {
		free(out.load_path);
		out.load_path = NULL;
		out.err = XY_ERR_INVALID;
		return out;
	}
	mod_key(out.key, key_len, out.load_path, region_id, plen);
	out.entry = corm_ptr(corm_get(mod_hd, out.key));
	out.err = XY_OK;
	return out;
}

void
module_rekey_for_claim(const char *caller,
                       uint64_t parent_id, uint8_t parent_plen,
                       uint64_t child_id, uint8_t child_plen)
{
	xy_lookup_result_t lookup = module_lookup_from_fname(caller, parent_id,
	                                                    parent_plen);
	if (!lookup.load_path)
		return;

	if (lookup.entry)
		module_rekey_loaded_entry(lookup.entry, lookup.key, lookup.load_path,
		                          parent_id, parent_plen,
		                          child_id, child_plen);
	module_lookup_result_free(&lookup);
}

static void
module_rekey_region_index(xy_mod_entry_t *me,
                          uint64_t parent_id, uint8_t parent_plen,
                          uint64_t child_id, uint8_t child_plen)
{
	/* mod_by_region_hd is keyed by region identity and CM_MULTIVALUE;
	 * collect all entries for the parent identity, delete them all,
	 * re-insert all except me under the parent, then insert me under the
	 * child identity.  Keying by (id, plen) matters here: a parent at
	 * (0,16) and a sibling at (0,17) share an id, so an id-only key would
	 * drag the sibling's entries along. */
#define MOD_BY_REGION_MAX 512
	xy_mod_entry_t *others[MOD_BY_REGION_MAX];
	int nothers = 0;
	xy_region_key_t pk, ck;
	region_key(&pk, parent_id, parent_plen);
	region_key(&ck, child_id, child_plen);
	uint32_t cur = corm_iter(mod_by_region_hd, &pk, 0);
	const void *ckey, *cv;
	while (corm_next(&ckey, &cv, cur)) {
		xy_mod_entry_t *e = corm_ptr(cv);
		if (e && e != me && nothers < MOD_BY_REGION_MAX)
			others[nothers++] = e;
	}
	corm_del_all(mod_by_region_hd, &pk);
	for (int oi = 0; oi < nothers; oi++)
		corm_put(mod_by_region_hd, &pk, &others[oi]);
	corm_put(mod_by_region_hd, &ck, &me);
#undef MOD_BY_REGION_MAX
}

static void
module_rekey_loaded_entry(xy_mod_entry_t *me, const char *old_key,
                          const char *load_path,
                          uint64_t parent_id, uint8_t parent_plen,
                          uint64_t child_id, uint8_t child_plen)
{
	if (!me || !old_key || !load_path) {
		return;
	}

	size_t new_key_len = mod_key_len(load_path);
	char *new_key = malloc(new_key_len);
	if (!new_key)
		return;
	mod_key(new_key, new_key_len, load_path, child_id, child_plen);
	me->region_id   = child_id;
	me->region_plen = child_plen;
	corm_put(mod_hd, new_key, &me);
	corm_del(mod_hd, old_key);
	memcpy(me->mod_key, new_key, new_key_len);
	free(new_key);
	module_rekey_region_index(me, parent_id, parent_plen, child_id, child_plen);
	if (me->ctx)
		me->ctx->region_id = child_id;
}

static void *
module_base_from_symbol(void *sym)
{
#ifdef _WIN32
	HMODULE mod = NULL;
	if (!sym)
		return NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR) sym, &mod))
		return NULL;
	return (void *) mod;
#else
	Dl_info di;
	if (!sym || !dladdr(sym, &di))
		return NULL;
	return di.dli_fbase;
#endif
}

static void *
module_handle_base(void *handle)
{
#ifdef _WIN32
	return handle;
#else
	void *any_sym = module_lookup_symbol_raw(handle, "xy_install");
	if (!any_sym) any_sym = module_lookup_symbol_raw(handle, "get_xy_ptr");
	return module_base_from_symbol(any_sym);
#endif
}

/* Do two paths name the same file?  The loader normally echoes back the exact
 * string handed to dlopen, so strcmp covers the common case — including a
 * reload tmp copy that has already been unlinked.  The stat compare covers the
 * rest: dyld canonicalises /tmp -> /private/tmp and resolves symlinks, so the
 * path it reports can differ from ours while the file is the same. */
int
module_same_file(const char *a, const char *b)
{
	if (!a || !b)
		return 0;
	if (strcmp(a, b) == 0)
		return 1;
	struct stat sa, sb;
	if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
		return 0;
	return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/*
 * A dlsym(handle, name) hit does not mean this module defines the symbol.
 * On glibc the search continues through the handle's DT_NEEDED
 * dependencies, so a module that merely links a library exporting a
 * hook-named symbol would be misdetected as implementing that hook and
 * given a dispatch slot pointing at the dependency's code — every
 * adapter-level call then runs the implementation once per dependent
 * plus once for the real module. (Seen live: nd links -laxil-tty, so
 * nd's entry resolved axil_tty_attach/axil_tty_input/axil_tty_active
 * and on_axil_tick through its dependency, duplicating negotiation
 * frames, PTY input and tick delivery.) Confirm the defining object is
 * the handle's own file before claiming it, with an exact compare so
 * symlinks, /lib→/usr/lib and tmp-copy reloads all agree.
 *
 * How the defining object is identified depends on the platform:
 *
 *   - Windows: GetProcAddress is already module-local, the base compare
 *     is belt and braces.
 *   - glibc, musl, FreeBSD, NetBSD, DragonFly: dlinfo() hands back the
 *     handle's link_map, whose load base is compared against the symbol's
 *     dli_fbase.
 *   - macOS and OpenBSD: no dlinfo() and no <link.h>, so the object is
 *     identified by the file dladdr() says the address came from, matched
 *     against the file this module was dlopen()'d from.
 *
 * A failed lookup means "not proven local" (0), never "assume local" —
 * wrongly claiming locality is what duplicates dispatch, dropping a hook
 * only means it stays unresolved.
 */
int
module_symbol_is_local(const xy_mod_entry_t *me, void *sym)
{
	if (!me || !me->handle || !sym)
		return 0;
#ifdef _WIN32
	return module_base_from_symbol(sym) == me->handle;
#elif defined(XY_HAVE_DLINFO)
	struct link_map *lm = NULL;
	if (dlinfo(me->handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm)
		return 0;
	Dl_info di;
	if (!dladdr(sym, &di) || !di.dli_fbase)
		return 0;
	return di.dli_fbase == XY_LINKMAP_BASE(lm);
#else
	Dl_info di;
	if (!dladdr(sym, &di) || !di.dli_fname)
		return 0;
	/* A reload maps a .xylem-XXXXXX.so copy, so the tmp path — not the
	 * stable one — is the file this handle actually refers to. */
	const char *own = me->tmp_load_path ? me->tmp_load_path : me->load_path;
	return module_same_file(di.dli_fname, own);
#endif
}

void
module_remove_denies(xy_region_entry_t *re, const char *path)
{
	if (!re || !path)
		return;
	xy_deny_entry_t **pp = &re->denied_modules;
	while (*pp) {
		if ((*pp)->value == path) {
			xy_deny_entry_t *dead = *pp;
			*pp = dead->next;
			if (re->denied_modules_set)
				corm_del(re->denied_modules_set, &dead->value);
			free(dead);
		} else {
			pp = &(*pp)->next;
		}
	}
}

int
module_is_denied(xy_region_entry_t *re, const char *path)
{
	if (!re || !path)
		return 0;
	if (re->denied_modules_set && corm_get(re->denied_modules_set, &path))
		return 1;
	for (xy_deny_entry_t *d = re->denied_modules; d; d = d->next) {
		if (d->value == path)
			return 1;
	}
	return 0;
}

void
module_region_detach(xy_mod_entry_t *entry)
{
	xy_region_entry_t *re = entry ? entry->region_entry : NULL;
	if (!re)
		return;

	if (entry->region_prev)
		entry->region_prev->region_next = entry->region_next;
	else
		re->mods_head = entry->region_next;

	if (entry->region_next)
		entry->region_next->region_prev = entry->region_prev;
	else
		re->mods_tail = entry->region_prev;

	entry->region_prev = NULL;
	entry->region_next = NULL;
	region_mark_subtree_dirty(re);
}

void
module_region_insert_after(xy_region_entry_t *re, xy_mod_entry_t *entry,
                           xy_mod_entry_t *after)
{
	if (!re || !entry)
		return;

	entry->region_entry = re;
	if (!after) {
		entry->region_prev = NULL;
		entry->region_next = re->mods_head;
		if (re->mods_head)
			re->mods_head->region_prev = entry;
		else
			re->mods_tail = entry;
		re->mods_head = entry;
	} else {
		entry->region_prev = after;
		entry->region_next = after->region_next;
		if (after->region_next)
			after->region_next->region_prev = entry;
		else
			re->mods_tail = entry;
		after->region_next = entry;
	}
	region_mark_subtree_dirty(re);
}

void
module_region_append(xy_region_entry_t *re, xy_mod_entry_t *entry)
{
	module_region_insert_after(re, entry, re ? re->mods_tail : NULL);
}

void
module_clear_fn_cache_for_handle(xy_mod_entry_t *entry)
{
	if (!entry || !entry->handle)
		return;
	void *base = module_handle_base(entry->handle);
	if (!base)
		return;

	unsigned c = corm_iter(mod_hd, NULL, 0);
	const void *key, *value;
	while (corm_next(&key, &value, c)) {
		xy_mod_entry_t *m = corm_ptr(value);
		if (!m || m == entry) continue;
		for (int i = 0; i < m->fn_cache_cap; i++) {
			void *fp = m->fn_cache[i];
			if (!fp || fp == XY_FN_NOT_FOUND) continue;
			if (module_base_from_symbol(fp) == base)
				m->fn_cache[i] = NULL; /* force re-resolve */
		}
	}
}

static void
module_cleanup_region_state(xy_mod_entry_t *entry)
{
	if (!entry || !entry->region_state)
		return;

	typedef void xy_region_cleanup_t(void *state);
	xy_region_cleanup_t *cleanup_fn = NULL;
	module_lookup_symbol_fn(entry->handle, "xy_region_cleanup",
	                        &cleanup_fn, sizeof(cleanup_fn));
	if (cleanup_fn)
		cleanup_fn(entry->region_state);
	free(entry->region_state);
	entry->region_state = NULL;
}

int xy_reloading = 0;

void
module_free_entry(xy_mod_entry_t *entry, int close_handle)
{
	void *handle;
	char *tmp = NULL;

	if (!entry)
		return;

	handle = entry->handle;
	tmp = entry->tmp_load_path;
	module_cleanup_region_state(entry);
	free(entry->fn_cache);
	free(entry->hook_impl_bits);
	free(entry->mod_key);
	free(entry);

	if (close_handle && handle)
		qsys_dlclose(handle);
	if (tmp) {
		unlink(tmp);
		free(tmp);
	}
}

void
mod_load_restore_context(xy_load_txn_t *tx)
{
	if (!tx || !tx->context_active)
		return;
	set_current_region(tx->prev_region_id, tx->prev_region_entry);
	tx->context_active = 0;
}

int
mod_load_abort(xy_load_txn_t *tx, int err)
{
	char *tmp = NULL;

	if (!tx)
		return err;

	mod_load_restore_context(tx);
	if (tx->mod_entry) {
		module_cleanup_region_state(tx->mod_entry);
		if (tx->mod_entry->region_entry) {
			xy_region_entry_t *rre = tx->mod_entry->region_entry;
			if (tx->mod_entry->region_prev)
				tx->mod_entry->region_prev->region_next = tx->mod_entry->region_next;
			else
				rre->mods_head = tx->mod_entry->region_next;
			if (tx->mod_entry->region_next)
				tx->mod_entry->region_next->region_prev = tx->mod_entry->region_prev;
			else
				rre->mods_tail = tx->mod_entry->region_prev;
			region_mark_subtree_dirty(rre);
			tx->mod_entry->region_entry = NULL;
		}
	}
	if (tx->published_entry && tx->mod_entry) {
		xy_region_key_t rk;
		region_key(&rk, tx->mod_entry->region_id, tx->mod_entry->region_plen);
		corm_del(mod_by_region_hd, &rk);
		corm_del(mod_hd, tx->mod_entry->mod_key);
	}
	if (tx->mod_entry) {
		tmp = tx->mod_entry->tmp_load_path;
		free(tx->mod_entry->fn_cache);
		free(tx->mod_entry->hook_impl_bits);
		free(tx->mod_entry->mod_key);
		free(tx->mod_entry);
		if (tmp) {
			unlink(tmp);
			free(tmp);
		}
	} else if (tx->stable_key) {
		free(tx->stable_key);
	}
	if (tx->stable_fname)
		free(tx->stable_fname);
	if (tx->tmp_load_path) {
		unlink(tx->tmp_load_path);
		free(tx->tmp_load_path);
		tx->tmp_load_path = NULL;
	}
	module_lookup_result_free(&tx->lookup);
	if (tx->handle) {
		qsys_dlclose(tx->handle);
		tx->handle = NULL;
	}
	return err;
}

static int
copy_one(const char *src, const char *tmpl_in, char **out_tmp)
{
	char tmpl[512];
	int out_fd;
	int in_fd = -1;
	int rc = -1;

	if (strlen(tmpl_in) >= sizeof(tmpl))
		return -1;
	strcpy(tmpl, tmpl_in);
	out_fd = qsys_mkstemps(tmpl, 3);
	if (out_fd < 0)
		return -1;
	in_fd = open(src, O_RDONLY | O_BINARY);
	if (in_fd < 0)
		goto out;
	{
		char buf[8192];
		ssize_t n;
		while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
			char *p = buf;
			ssize_t w;
			while (n > 0) {
				w = write(out_fd, p, (size_t)n);
				if (w <= 0)
					goto out;
				p += w;
				n -= w;
			}
		}
		if (n < 0)
			goto out;
	}
#ifndef _WIN32
	if (fchmod(out_fd, 0755) != 0)
		goto out;
#endif
	if (qsys_fsync(out_fd) != 0)
		goto out;
#ifndef _WIN32
	{
		char *slash = strrchr(tmpl, '/');
		if (slash) {
			char dir[512];
			size_t dlen = (size_t)(slash - tmpl);
			if (dlen < sizeof(dir)) {
				memcpy(dir, tmpl, dlen);
				dir[dlen] = '\0';
				int dir_fd = open(dir, O_DIRECTORY);
				if (dir_fd >= 0) {
					fsync(dir_fd);
					close(dir_fd);
				}
			}
		}
	}
#endif
	rc = 0;
	*out_tmp = strdup(tmpl);
out:
	if (in_fd >= 0)
		close(in_fd);
	close(out_fd);
	if (rc != 0)
		unlink(tmpl);
	return rc;
}

static int
copy_to_tmp(const char *src, char **out_tmp)
{
	char tmpl2[512];
	const char *slash = strrchr(src, '/');

	if (slash) {
		size_t dir_len = (size_t)(slash - src);
		if (dir_len + 32 < sizeof(tmpl2)) {
			memcpy(tmpl2, src, dir_len);
			tmpl2[dir_len] = '\0';
			strcat(tmpl2, "/.xylem-XXXXXX.so");
			if (copy_one(src, tmpl2, out_tmp) == 0)
				return 0;
		}
	}
	if (copy_one(src, "/tmp/xylem-XXXXXX.so", out_tmp) == 0)
		return 0;
	if (copy_one(src, "./tmp/xylem-XXXXXX.so", out_tmp) == 0)
		return 0;
	return -1;
}

int
mod_load_open_handle(xy_load_txn_t *tx, char *fname)
{
	char *tmp = NULL;

	tx->lookup = module_lookup_from_fname(fname, tx->inherited_region_id,
	                                      tx->inherited_region_plen);
	if (tx->lookup.err != XY_OK)
		return tx->lookup.err;
	if (!tx->lookup.load_path)
		return XY_ERR_INVALID;

	if (xy_reloading && copy_to_tmp(tx->lookup.load_path, &tmp) == 0) {
		tx->handle = qsys_dlopen(tmp, QSYS_RTLD_NODELETE);
		if (!tx->handle) {
			WARN("_mod_load failed loading '%s' (tmp %s): %s\n",
			     fname, tmp, qsys_dlerror());
			unlink(tmp);
			free(tmp);
			module_lookup_result_free(&tx->lookup);
			return XY_ERR_NOTFOUND;
		}
		tx->tmp_load_path = tmp;
	} else {
		tx->handle = qsys_dlopen(tx->lookup.load_path, QSYS_RTLD_NODELETE);
		if (!tx->handle) {
			WARN("_mod_load failed loading '%s': %s\n", fname, qsys_dlerror());
			module_lookup_result_free(&tx->lookup);
			return XY_ERR_NOTFOUND;
		}
	}

	return XY_OK;
}

int
mod_load_try_reuse_existing(xy_load_txn_t *tx)
{
	xy_mod_entry_t *existing = tx->lookup.entry;
	if (!existing)
		return 0;

	existing->refcount++;
	qsys_dlclose(tx->handle);
	tx->handle = NULL;
	if (tx->tmp_load_path) {
		unlink(tx->tmp_load_path);
		free(tx->tmp_load_path);
		tx->tmp_load_path = NULL;
	}
	module_lookup_result_free(&tx->lookup);
	return 1;
}

int
mod_load_alloc_entry(xy_load_txn_t *tx, char *fname)
{
	tx->stable_fname = strdup(fname);
	if (!tx->stable_fname || !tx->lookup.key)
		return XY_ERR_INVALID;
	tx->stable_key = tx->lookup.key;
	tx->lookup.key = NULL;

	tx->interned_load_path = module_path_intern(tx->lookup.load_path);
	tx->lookup.load_path = NULL;
	if (!tx->interned_load_path)
		return XY_ERR_INVALID;

	tx->mod_entry = calloc(1, sizeof(*tx->mod_entry));
	if (!tx->mod_entry)
		return XY_ERR_INVALID;

	tx->mod_entry->handle        = tx->handle;
	tx->mod_entry->region_id     = tx->inherited_region_id;
	tx->mod_entry->region_plen   = tx->inherited_region_plen;
	tx->mod_entry->refcount      = 1;
	tx->mod_entry->parent_entry  = xy_loading_mod;
	tx->mod_entry->mod_key       = tx->stable_key;
	tx->mod_entry->load_path     = tx->interned_load_path;
	tx->mod_entry->tmp_load_path = tx->tmp_load_path;
	tx->tmp_load_path = NULL;

	return XY_OK;
}

int
mod_load_publish_entry(xy_load_txn_t *tx)
{
	corm_put(mod_hd, tx->stable_key, &tx->mod_entry);
	{
		xy_region_key_t rk;
		region_key(&rk, tx->mod_entry->region_id, tx->mod_entry->region_plen);
		corm_put(mod_by_region_hd, &rk, &tx->mod_entry);
	}
	tx->published_entry = 1;
	return XY_OK;
}

/* Refuse to write into a module context we cannot prove is big enough.
 *
 * _xy_init() below writes sizeof(struct xy_ctx) bytes — the size the *host*
 * was compiled with — into a static owned by the module.  A module built
 * against an older header has a smaller object, and the tail of the write
 * lands in whatever the linker placed next to it in the module's BSS.  That
 * is not an error the loader can see: the load succeeds, the function
 * pointers both versions share all work, and the damage surfaces later in an
 * unrelated library as a bad pointer.  It has happened once already
 * (libaxil-tty's static mux_map overwritten with a pointer's low half, then a
 * SIGSEGV on the first HTTP request), so the check is a hard gate rather than
 * a warning.
 *
 * A module that does not export xy_ctx_abi() at all is refused too: such a
 * module necessarily predates the symbol, which means it predates this
 * contract, so there is nothing to check its size against.
 */
static int
mod_load_check_ctx_abi(xy_load_txn_t *tx)
{
	typedef uint64_t xy_ctx_abi_fn_t(void);
	const uint64_t host = XY_CTX_ABI_DESC;
	xy_ctx_abi_fn_t *abi_fn = NULL;
	uint64_t mod = 0;

	/* module_lookup_symbol_fn() yields the symbol's *address*, so the
	 * descriptor has to be produced by calling through it. */
	if (module_lookup_symbol_fn(tx->handle, "xy_ctx_abi", &abi_fn,
	                            sizeof(abi_fn)) && abi_fn)
		mod = abi_fn();

	if (!abi_fn) {
		WARN("xy_load: refusing %s: it does not export xy_ctx_abi() — it "
		     "predates the context ABI contract (host wants ABI %u, size %u). "
		     "Rebuild the module.\n",
		     tx->stable_fname, (unsigned)(host >> 32), (unsigned)host);
		XY_SET_ERR(XY_ERR_ABI);
		return XY_ERR_ABI;
	}
	if (mod != host) {
		WARN("xy_load: refusing %s: context ABI mismatch — module is "
		     "ABI %u/size %u, host is ABI %u/size %u. Rebuild the module "
		     "against the current ttypt/xy.h.\n",
		     tx->stable_fname,
		     (unsigned)(mod  >> 32), (unsigned)(mod  & 0xffffffffu),
		     (unsigned)(host >> 32), (unsigned)(host & 0xffffffffu));
		XY_SET_ERR(XY_ERR_ABI);
		return XY_ERR_ABI;
	}
	return XY_OK;
}

int
mod_load_bind_xy(xy_load_txn_t *tx)
{
	get_xy_func_t get_xy = NULL;
	xy_t *ctx = NULL;
	int ret;

	module_lookup_symbol_fn(tx->handle, "get_xy_ptr", &get_xy, sizeof(get_xy));
#ifdef XY_DEBUG_LOG
	{
		FILE *dbg = fopen("/tmp/xy_bind.log", "a");
		if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
		if (dbg) {
			fprintf(dbg, "mod_load_bind_xy: fname=%s get_xy=%p\n",
				tx->stable_fname, (void *)(uintptr_t)get_xy);
			fclose(dbg);
		}
	}
#endif
	if (get_xy) {
		ctx = get_xy();
#ifdef XY_DEBUG_LOG
		{
			FILE *dbg = fopen("/tmp/xy_bind.log", "a");
			if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
			if (dbg) {
				fprintf(dbg, "mod_load_bind_xy: get_xy() => %p\n",
					(void *)ctx);
				fclose(dbg);
			}
		}
#endif
	}

	/* Fallback: Rust cdylibs call xy_self_init_ctx() from a .init_array
	 * constructor (inside dlopen) to push their xy pointer to us before
	 * mod_load_bind_xy runs. Use that when get_xy_ptr lookup fails. */
	if (!ctx && xy_pending_ctx) {
#ifdef XY_DEBUG_LOG
		FILE *dbg = fopen("/tmp/xy_bind.log", "a");
		if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
		if (dbg) {
			fprintf(dbg, "mod_load_bind_xy: using xy_pending_ctx=%p\n",
				(void *)xy_pending_ctx);
			fclose(dbg);
		}
#endif
		ctx = xy_pending_ctx;
	}

	xy_pending_ctx = NULL;

	if (!ctx) {
#ifdef XY_DEBUG_LOG
		FILE *dbg = fopen("/tmp/xy_bind.log", "a");
		if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
		if (dbg) {
			fprintf(dbg, "mod_load_bind_xy: no xy for %s\n",
				tx->stable_fname);
			fclose(dbg);
		}
#endif
		return XY_OK;
	}

#ifdef XY_DEBUG_LOG
	{
		FILE *dbg = fopen("/tmp/xy_bind.log", "a");
		if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
		if (dbg) {
			fprintf(dbg,
				"mod_load_bind_xy: calling _xy_init on %p for %s\n",
				(void *)ctx, tx->stable_fname);
			fclose(dbg);
		}
	}
#endif
	ret = mod_load_check_ctx_abi(tx);
	if (ret != XY_OK)
		return ret;
	_xy_init(ctx, tx->stable_fname, tx->inherited_region_id);
#ifdef XY_DEBUG_LOG
	{
		FILE *dbg = fopen("/tmp/xy_bind.log", "a");
		if (!dbg) dbg = fopen("tmp/xy_bind.log", "a");
		if (dbg) {
			fprintf(dbg, "mod_load_bind_xy: xy.call=%p for %s\n",
				(void *)(uintptr_t)ctx->call, tx->stable_fname);
			fclose(dbg);
		}
	}
#endif
	tx->mod_entry->ctx = ctx;
	return XY_OK;
}

int
mod_load_enter_context(xy_load_txn_t *tx)
{
	tx->inherited_reg = region_lookup(tx->inherited_region_id,
	                                  tx->inherited_region_plen);
	tx->prev_region_id = xy_current_region_id;
	tx->prev_region_entry = xy_current_region_entry;
	set_current_region(tx->inherited_region_id, tx->inherited_reg);
	tx->context_active = 1;
	return XY_OK;
}

int
mod_load_claim_if_needed(xy_load_txn_t *tx)
{
	uint8_t *claim_sym = module_lookup_symbol_raw(tx->handle, "xy_claim");
	if (tx->inherited_reg && tx->inherited_reg->require_claim && !claim_sym)
		return XY_ERR_EPERM;

	if (!claim_sym || !tx->inherited_reg)
		return XY_OK;
	if (!tx->inherited_reg->claim_handler && !tx->inherited_reg->require_claim)
		return XY_OK;

	return _xy_claim_for_load(tx->stable_fname, tx->inherited_region_id,
	                          tx->inherited_region_plen,
	                          *claim_sym, tx->handle);
}

int
mod_load_run_install(xy_load_txn_t *tx)
{
	xy_mod_entry_t *prev_loading_mod = xy_loading_mod;
	int ret;

	xy_loading_mod = tx->mod_entry;
	ret = _mod_run(tx->handle, "xy_install");
	xy_loading_mod = prev_loading_mod;
	return ret;
}

void
module_remove_path_owned_entries(xy_region_entry_t *re, const char *path,
                                 void *handle UNUSED)
{
	if (!re || !path)
		return;

	module_remove_denies(re, path);
}

/*
 * Variable-length key type for mod_hd.
 *
 * Keys are "path\0<16-hex-region>" — the embedded NUL means CM_STR would
 * truncate to just the path, making all regions of the same .so collide.
 * corm_mreg registers a type whose measure callback returns the full key
 * length (strlen stops at the NUL, giving path length; adding
 * MOD_KEY_SUFFIX_LEN accounts for the NUL + 16 hex chars).
 * corm then hashes/compares via memcmp over the full blob, correctly
 * distinguishing (path, region_A) from (path, region_B).
 */
size_t
mod_key_measure(const void *data)
{
	return strlen((const char *)data) + MOD_KEY_SUFFIX_LEN;
}

/* -------------------------------------------------------------------------
 * Public API: error
 * ------------------------------------------------------------------------- */

int xy_errno(void) {
	return XY_GET_ERR();
}

const char *xy_strerror(int err) {
	switch (err) {
	case XY_OK:           return "success";
	case XY_ERR_NOTFOUND: return "not found";
	case XY_ERR_INVALID:  return "invalid argument";
	case XY_ERR_TOOBIG:   return "return type too large";
	case XY_ERR_INIT:     return "initialization failed";
	case XY_ERR_EPERM:    return "operation not permitted";
	case XY_ERR_ABI:      return "module context ABI mismatch (rebuild module)";
	default:               return "unknown error";
	}
}

/* -------------------------------------------------------------------------
 * xy_on_claim — register a claim handler on the caller's current region
 * ------------------------------------------------------------------------- */

int xy_require_claim(xy_claim_handler_fn_t *fn, void *ud) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	xy_region_entry_t *reg = region_lookup_current();
	if (!reg) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	if (!fn) {
		/* NULL handler clears the require-claim gate */
		reg->claim_handler    = NULL;
		reg->claim_handler_ud = NULL;
		reg->require_claim    = 0;
	} else {
		reg->claim_handler    = fn;
		reg->claim_handler_ud = ud;
			reg->require_claim    = 1;
		}
	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * xy_claim — claim a child region under the caller's current region
 * ------------------------------------------------------------------------- */

/*
 * Find the first free slot of 'bits' width under parent entry.
 *
 * A slot is free if no existing region has that (id, plen) identity.  The
 * width is part of the collision test, which is what makes the s == 0
 * iteration usable: it used to lose to the parent itself, because an
 * id-only lookup of parent->id matched the parent and made the leftmost half
 * of every region look taken.  Under (id, plen) identity a child at
 * (parent->id, parent->plen + bits) is a genuinely distinct region, so the
 * leftmost half is allocated like any other slot.
 *
 * Returns the child ID (plain 64-bit prefix value), or XY_REGION_INVALID.
 */
static uint64_t
region_alloc_slot(const xy_region_entry_t *parent, uint8_t bits)
{
	uint8_t  cplen = parent->plen + bits;

	if (cplen > 64)
		return XY_REGION_INVALID;

	uint64_t num_slots = (cplen == 64) ? (uint64_t)0 /* wraps */ : (uint64_t)1 << bits;
	/* When cplen==64, there is exactly 1 slot (the full 64-bit value);
	 * handle via slot_shift==0 special case below. */
	uint64_t slot_shift = (cplen < 64) ? (64 - cplen) : 0;

	/* Iterate all possible slot offsets: 0 .. (1<<bits)-1.
	 * For cplen==64, num_slots wrapped to 0 so we use a do-while for
	 * exactly one iteration. */
	uint64_t s = 0;
	do {
		uint64_t candidate_id = parent->id | (s << slot_shift);

		/* O(1) collision check via the region hash map, on full identity */
		if (!region_lookup(candidate_id, cplen))
			return candidate_id;
		s++;
	} while (s != num_slots);

	return XY_REGION_INVALID;
}

/* -------------------------------------------------------------------------
 * _xy_claim_for_load — internal helper: perform a claim on behalf of a
 * module being loaded.  Called from _mod_load when the module has an
 * xy_claim data symbol and the parent region has require_claim set.
 *
 * caller     — stable module path (stable_fname)
 * parent_id  — region the module is being loaded into
 * bits       — value read from the module's xy_claim symbol
 * handle     — dlopen handle so we can update the live xy_t
 * ------------------------------------------------------------------------- */
int
_xy_claim_for_load(const char *caller, uint64_t parent_id, uint8_t parent_plen,
                    uint8_t bits, void *handle)
{
	(void)handle;
	if (bits == 0 || bits > 64) {
		XY_SET_ERR(XY_ERR_INVALID);
		return XY_ERR_INVALID;
	}

	xy_region_entry_t *parent = region_lookup(parent_id, parent_plen);
	if (!parent) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	/* Invoke the parent's claim handler */
	if (!parent->claim_handler) {
		XY_SET_ERR(XY_ERR_EPERM);
		return XY_ERR_EPERM;
	}

	uint8_t granted = 0;
	int hret = parent->claim_handler(caller ? caller : "",
	                                  bits, &granted,
	                                  parent->claim_handler_ud);
	if (hret != XY_OK) {
		XY_SET_ERR(XY_ERR_EPERM);
		return XY_ERR_EPERM;
	}
	if (granted == 0 || granted > 64) {
		XY_SET_ERR(XY_ERR_EPERM);
		return XY_ERR_EPERM;
	}

	/* Find a free slot */
	uint64_t child_id = region_alloc_slot(parent, granted);
	if (child_id == XY_REGION_INVALID) {
		XY_SET_ERR(XY_ERR_TOOBIG);
		return XY_ERR_TOOBIG;
	}

	/* Create the child region entry */
	xy_region_entry_t *child = calloc(1, sizeof(*child));
	child->id         = child_id;
	child->plen       = parent->plen + granted;
	child->depth      = parent->depth + 1;
	child->child_bits = granted;
	child->owner_path = caller;
	child->dispatch_gen = 1;
	child->parent     = parent;
	{
		xy_region_key_t ck;
		region_key(&ck, child->id, child->plen);
		corm_put(region_hd, &ck, &child);
	}

	/* Wire child into parent's children list */
	child->sibling_next   = (xy_region_entry_t *)parent->children_head;
	parent->children_head = child;
	region_mark_subtree_dirty(parent);

	/* Update the thread-local region context so xy_install sees child */
	set_current_region(child_id, child);

	/* Update the mod entry and re-key it under the new region */
	if (caller) {
		module_rekey_for_claim(caller, parent_id, parent_plen,
		                       child_id, child->plen);
	}

	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * Region deny API
 * ------------------------------------------------------------------------- */

int xy_deny(const char *what, xy_deny_type_t type) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	xy_region_entry_t *reg = region_lookup_current();
	if (!reg) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	xy_deny_entry_t *node = malloc(sizeof(*node));
	node->value = strdup(what);

	if (type == XY_DENY_HOOK) {
		node->next = reg->denied_hooks;
		reg->denied_hooks = node;
		if (!reg->denied_hooks_set)
			reg->denied_hooks_set = corm_open(NULL, NULL, CM_STR,
			                                  xy_ptr_type, MOD_MASK, 0);
		{
			void *sentinel = (void *)(uintptr_t)1;
			corm_put(reg->denied_hooks_set, node->value, &sentinel);
		}
	} else {
			char *path = module_load_path(what);
			if (!path) {
				free(node);
				XY_SET_ERR(XY_ERR_INVALID);
				return XY_ERR_INVALID;
			}
			const char *interned = module_path_intern(path);
			if (!interned) {
				free(node);
				XY_SET_ERR(XY_ERR_INVALID);
				return XY_ERR_INVALID;
			}
		node->value = (char *)interned;
		node->next = reg->denied_modules;
		reg->denied_modules = node;
		if (!reg->denied_modules_set)
			reg->denied_modules_set = corm_open(NULL, NULL, xy_ptr_type,
			                                    xy_ptr_type, MOD_MASK, 0);
		{
			void *sentinel = (void *)(uintptr_t)1;
			corm_put(reg->denied_modules_set, &node->value, &sentinel);
		}
	}
	region_propagate_deny(reg);
	region_dispatch_gen_bump(reg);

	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * xy_region_each — enumerate immediate children of caller's current region
 * ------------------------------------------------------------------------- */

int xy_region_each(xy_region_each_fn_t *fn, void *ud) {
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	xy_region_entry_t *parent = region_lookup_current();
	if (!parent) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	int ret = XY_OK;
	for (xy_region_entry_t *e = parent->children_head; e; e = e->sibling_next) {
		ret = fn(e->id, e->plen, ud);
		if (ret != XY_OK) break;
	}
	XY_SET_ERR(XY_OK);
	return ret;
}

uint64_t
xy_current_region(void)
{
	uint64_t region_id;
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return XY_REGION_INVALID;
	}
	region_id = xy_current_region_id;
	return region_id;
}

int
xy_with_region(uint64_t region_id, uint8_t plen, xy_scope_fn_t *fn, void *ud)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	if (!fn) {
		XY_SET_ERR(XY_ERR_INVALID);
		return XY_ERR_INVALID;
	}

	xy_region_entry_t *target = region_lookup(region_id, plen);
	if (!target) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	uint64_t prev_region_id = xy_current_region_id;
	xy_region_entry_t *prev_region_entry = xy_current_region_entry;
	set_current_region(region_id, target);

	int ret = fn(ud);

	set_current_region(prev_region_id, prev_region_entry);
	XY_SET_ERR(ret == XY_OK ? XY_OK : ret);
	return ret;
}

uint8_t
xy_current_region_plen(void)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return 0;
	}
	/* Straight off the thread-local entry — the current region is already
	 * resolved, so there is nothing to look up and no ambiguity. */
	XY_SET_ERR(XY_OK);
	return region_current_plen();
}

int
xy_region_exists(uint64_t region_id, uint8_t plen)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}
	/* Both halves are required: an id that exists at another width names a
	 * different region and must not be reported as found. */
	if (!region_lookup(region_id, plen)) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}
	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * xy_claim_at / xy_region_at — engine-driven region creation and lookup
 * ------------------------------------------------------------------------- */

/* Depth-first search for the nearest existing ancestor of (id, plen).
 *
 * The region of the largest width that prefix-covers the request wins.  The
 * walk prunes on width: plen increases strictly along every root-to-leaf path,
 * so a node at plen >= the requested width has no narrower descendant and its
 * whole subtree can be skipped.  Containment is checked per node rather than
 * inherited, because a descendant's prefix can match where its parent's does
 * not — (2<<48,16) sits inside root (0,0) but not inside (0,16), and only the
 * per-node test tells those apart.
 *
 * O(visited regions), and in practice near O(depth) thanks to the prune. */
static xy_region_entry_t *
region_find_ancestor_rec(xy_region_entry_t *node, uint64_t id, uint8_t plen,
                         xy_region_entry_t *best)
{
	if (node->plen >= plen)
		return best;

	if ((id & region_mask(node->plen)) == node->id &&
	    (!best || node->plen > best->plen))
		best = node;

	for (xy_region_entry_t *c = node->children_head; c; c = c->sibling_next)
		best = region_find_ancestor_rec(c, id, plen, best);

	return best;
}

xy_region_entry_t *
region_nearest_ancestor(uint64_t id, uint8_t plen)
{
	xy_region_entry_t *root = region_lookup(XY_REGION_ROOT, 0);
	if (!root)
		return NULL;
	return region_find_ancestor_rec(root, id, plen, NULL);
}

int
xy_claim_at(uint64_t id, uint8_t plen,
            xy_claim_handler_fn_t *fn, void *ud)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		XY_SET_ERR(enter_ret);
		return enter_ret;
	}

	if (plen > 64) {
		XY_SET_ERR(XY_ERR_TOOBIG);
		return XY_ERR_TOOBIG;
	}
	/* Misaligned: bits set below the prefix width.  A canonical id has
	 * exactly its high plen bits set and nothing else — planet 2 is
	 * 2<<48 at plen 16, while 0x1234_5678_9abc_d000 is not a plen-16 id. */
	if ((id & region_mask(plen)) != id) {
		XY_SET_ERR(XY_ERR_INVALID);
		return XY_ERR_INVALID;
	}

	/* Idempotent: an exact (id, plen) match succeeds and becomes current.
	 * Checked before the "no new bits" reasoning below, because the root
	 * (0,0) is its own best match and must re-claim cleanly. */
	xy_region_entry_t *existing = region_lookup(id, plen);
	if (existing) {
		if (fn) {
			existing->claim_handler = fn;
			existing->claim_handler_ud = ud;
		}
		set_current_region(id, existing);
		XY_SET_ERR(XY_OK);
		return XY_OK;
	}

	xy_region_entry_t *parent = region_nearest_ancestor(id, plen);
	if (!parent) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_ERR_NOTFOUND;
	}

	/* "No new bits" guard.  Unreachable while region_nearest_ancestor()
	 * considers only plen_A < plen, and kept anyway: if that search is ever
	 * relaxed this fails loudly instead of silently creating a duplicate
	 * identity for corm_put to resolve arbitrarily. */
	if (plen <= parent->plen) {
		XY_SET_ERR(XY_ERR_INVALID);
		return XY_ERR_INVALID;
	}

	xy_region_entry_t *child = calloc(1, sizeof(*child));
	if (!child) {
		XY_SET_ERR(XY_ERR_TOOBIG);
		return XY_ERR_TOOBIG;
	}
	child->id         = id;
	child->plen       = plen;
	child->depth      = parent->depth + 1;
	child->child_bits = (uint8_t)(plen - parent->plen);
	/* The engine owns this region; no module claimed it. */
	child->owner_path = NULL;
	child->dispatch_gen = 1;
	child->parent     = parent;
	child->claim_handler     = fn;
	child->claim_handler_ud  = ud;

	{
		xy_region_key_t k;
		region_key(&k, id, plen);
		corm_put(region_hd, &k, &child);
	}

	child->sibling_next   = (xy_region_entry_t *)parent->children_head;
	parent->children_head = child;

	/* Marks every ancestor's subtree_mods_dirty and bumps dispatch_gen up
	 * the same chain, so this one call covers both of ST.md §4.5(1)'s
	 * region_mark_subtree_dirty(A) and region_dispatch_gen_bump(A). */
	region_mark_subtree_dirty(parent);

	set_current_region(id, child);

	XY_SET_ERR(XY_OK);
	return XY_OK;
}

/* Deepest existing region whose prefix covers (prefix_id, plen). */
static xy_region_entry_t *
region_find_cover_rec(xy_region_entry_t *node, uint64_t id, uint8_t plen,
                      xy_region_entry_t *best)
{
	/* A region wider than the query point cannot contain it. */
	if (node->plen > plen)
		return best;

	if ((id & region_mask(node->plen)) == node->id &&
	    (!best || node->plen > best->plen))
		best = node;

	for (xy_region_entry_t *c = node->children_head; c; c = c->sibling_next)
		best = region_find_cover_rec(c, id, plen, best);

	return best;
}

uint64_t
xy_region_at(uint64_t prefix_id, uint8_t plen, uint8_t *region_plen)
{
	int enter_ret = xy_runtime_ensure();
	if (enter_ret != XY_OK) {
		if (region_plen)
			*region_plen = 0;
		XY_SET_ERR(enter_ret);
		return XY_REGION_INVALID;
	}

	/* Deterministic 0 on every failure path, so a caller that ignores the
	 * return value reads a defined width rather than stack noise. */
	if (region_plen)
		*region_plen = 0;

	if (plen > 64) {
		XY_SET_ERR(XY_ERR_TOOBIG);
		return XY_REGION_INVALID;
	}

	xy_region_entry_t *root = region_lookup(XY_REGION_ROOT, 0);
	xy_region_entry_t *hit = root
		? region_find_cover_rec(root, prefix_id, plen, NULL)
		: NULL;
	if (!hit) {
		XY_SET_ERR(XY_ERR_NOTFOUND);
		return XY_REGION_INVALID;
	}

	if (region_plen)
		*region_plen = hit->plen;

	XY_SET_ERR(XY_OK);
	return hit->id;
}

/* -------------------------------------------------------------------------
 * Module loading
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * xy_shutdown
 * ------------------------------------------------------------------------- */
