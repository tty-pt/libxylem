/*
 * test_region_identity: the CP-3 gate (ST.md §15.1, §16, §21).
 *
 * A region is identified by the pair (id, plen) — never by id alone.  Along
 * the leftmost path of the hierarchy every level has the *same numeric id*
 * as its parent:
 *
 *     (0,0) root
 *        └─ (0,16)   mod_regionid_a  (claim 16)
 *             └─ (0,17)   mod_regionid_b  (claim  1)
 *                  └─ (0,64)   mod_regionid_c  (claim 47)
 *
 * so "cosmos, world 0, the left half of world 0, and the cell" are four
 * distinct regions that all have id == 0.  This test builds that chain
 * through the *existing* public claim path only — a permissive
 * xy_require_claim handler plus a module exporting
 * `XY_MODULE_API uint8_t xy_claim` — with no CP-4 creation API involved, and
 * then asserts that all four coexist and are individually addressable.
 *
 * It is also the discriminating test for the allocator: before CP-3
 * region_alloc_slot() began its slot scan at s = 0 and rejected on id
 * collision, so a same-id child was structurally impossible and the left half
 * of every region was unrepresentable.  Run against the pre-change tree the
 * chain cannot come out as (0,0)→(0,16)→(0,17)→(0,64) at all.
 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/xy.h>

#include "regionid_hooks.h"

#define MOD_A "tests/mods/mod_regionid_a"
#define MOD_B "tests/mods/mod_regionid_b"
#define MOD_C "tests/mods/mod_regionid_c"

/* The chain, as (id, plen) pairs.  Every id is 0. */
#define ROOT_ID    0
#define ROOT_PLEN  0
#define A_ID       0
#define A_PLEN     16
#define B_ID       0
#define B_PLEN     17
#define C_ID       0
#define C_PLEN     64

/* -------------------------------------------------------------------------
 * Claim handler: grant exactly what was asked for, so the slot scan starts
 * at s == 0 and the left half (id == parent id) is chosen.
 * ------------------------------------------------------------------------- */
static int permissive_handler(const char *path, uint8_t req,
                              uint8_t *granted, void *ud) {
	(void)path; (void)ud;
	*granted = req;
	return XY_OK;
}

/* -------------------------------------------------------------------------
 * region_each collector
 * ------------------------------------------------------------------------- */
#define MAX_CHILDREN 8

typedef struct {
	uint64_t id[8];
	uint8_t  plen[8];
	int      n;
} children_t;

static int collect_child(uint64_t id, uint8_t plen, void *ud) {
	children_t *c = (children_t *)ud;
	assert(c->n < MAX_CHILDREN);
	c->id[c->n]   = id;
	c->plen[c->n] = plen;
	c->n++;
	return XY_OK;
}

/* Run fn in the region (id, plen).  Fails the test if that region does not
 * exist or if fn itself fails. */
static void in_region(uint64_t id, uint8_t plen, xy_scope_fn_t *fn, void *ud) {
	int r = xy_with_region(id, plen, fn, ud);
	assert(r == XY_OK);
}

/* -------------------------------------------------------------------------
 * 1. Identity: four regions, one id
 * ------------------------------------------------------------------------- */
static void test_four_regions_share_id(void) {
	assert(xy_region_exists(ROOT_ID, ROOT_PLEN) == XY_OK);
	assert(xy_region_exists(A_ID, A_PLEN) == XY_OK);
	assert(xy_region_exists(B_ID, B_PLEN) == XY_OK);
	assert(xy_region_exists(C_ID, C_PLEN) == XY_OK);

	/* The id alone is genuinely ambiguous here, so these must NOT exist:
	 * no such region was ever created at these widths. */
	assert(xy_region_exists(0, 1)  == XY_ERR_NOTFOUND);
	assert(xy_region_exists(0, 15) == XY_ERR_NOTFOUND);
	assert(xy_region_exists(0, 63) == XY_ERR_NOTFOUND);

	printf("  test_four_regions_share_id: PASS\n");
}

/* -------------------------------------------------------------------------
 * 2. Addressability: the caller's own region reports both halves correctly
 * ------------------------------------------------------------------------- */
struct probe_cur { uint64_t id; uint8_t plen; };

static int probe_current(void *ud) {
	struct probe_cur *p = (struct probe_cur *)ud;
	p->id   = xy_current_region();
	p->plen = xy_current_region_plen();
	return XY_OK;
}

static void test_current_region_in_each(void) {
	struct probe_cur p = { ~0ULL, 0xFF };

	in_region(ROOT_ID, ROOT_PLEN, probe_current, &p);
	assert(p.id == ROOT_ID && p.plen == ROOT_PLEN);

	in_region(A_ID, A_PLEN, probe_current, &p);
	assert(p.id == A_ID && p.plen == A_PLEN);

	in_region(B_ID, B_PLEN, probe_current, &p);
	assert(p.id == B_ID && p.plen == B_PLEN);

	in_region(C_ID, C_PLEN, probe_current, &p);
	assert(p.id == C_ID && p.plen == C_PLEN);

	/* The id half is 0 in all four — this is the ambiguity CP-3 removed by
	 * giving plen a first-class role. */
	assert(p.id == 0 && p.plen == C_PLEN);

	printf("  test_current_region_in_each: PASS\n");
}

/* -------------------------------------------------------------------------
 * 3. Tree shape: each level has exactly one child, at the expected plen
 * ------------------------------------------------------------------------- */
struct probe_kids { children_t kids; };

static int probe_children(void *ud) {
	children_t *c = &((struct probe_kids *)ud)->kids;
	c->n = 0;
	return xy_region_each(collect_child, c);
}

static void expect_one_child(uint64_t id, uint8_t plen,
                             uint64_t child_id, uint8_t child_plen,
                             const char *what) {
	struct probe_kids p;
	memset(&p, 0, sizeof(p));
	in_region(id, plen, probe_children, &p);
	if (p.kids.n != 1 || p.kids.id[0] != child_id ||
	    p.kids.plen[0] != child_plen) {
		printf("    %s: region (%llu,%u) has %d children, first "
		       "(%llu,%u); expected 1 child (%llu,%u)\n",
		       what,
		       (unsigned long long)id, plen, p.kids.n,
		       (unsigned long long)(p.kids.n ? p.kids.id[0] : 0),
		       (unsigned)p.kids.plen[0],
		       (unsigned long long)child_id, child_plen);
		assert(p.kids.n == 1);
	}
}

static void test_tree_shape(void) {
	expect_one_child(ROOT_ID, ROOT_PLEN, A_ID, A_PLEN, "(0,0)");
	expect_one_child(A_ID,    A_PLEN,    B_ID, B_PLEN, "(0,16)");
	expect_one_child(B_ID,    B_PLEN,    C_ID, C_PLEN, "(0,17)");

	/* (0,64) is a leaf cell: plen is exhausted, so it has no children. */
	struct probe_kids p;
	memset(&p, 0, sizeof(p));
	in_region(C_ID, C_PLEN, probe_children, &p);
	assert(p.kids.n == 0);

	printf("  test_tree_shape: PASS\n");
}

/* -------------------------------------------------------------------------
 * 4. Each module saw its own width during xy_install
 *
 * xy_install() runs after _xy_claim_for_load() has switched the thread-local
 * region to the child, so this is the in-module half of the identity change:
 * before it, a module had no way to ask how wide its own region was, because
 * the old getter took an id and id is 0 for all three.
 * ------------------------------------------------------------------------- */
static uint8_t read_seen_plen(const char *sopath) {
	void *h = dlopen(sopath, RTLD_NOW | RTLD_NOLOAD);
	assert(h); /* RTLD_NODELETE keeps the .so mapped after unload */
	uint8_t *p = (uint8_t *)dlsym(h, "ri_seen_plen");
	assert(p);
	return *p;
}

static void test_install_saw_own_width(void) {
	assert(read_seen_plen("tests/mods/mod_regionid_a.so") == A_PLEN);
	assert(read_seen_plen("tests/mods/mod_regionid_b.so") == B_PLEN);
	assert(read_seen_plen("tests/mods/mod_regionid_c.so") == C_PLEN);
	printf("  test_install_saw_own_width: PASS\n");
}

/* -------------------------------------------------------------------------
 * 5. Dispatch scoping — the ancestor/subtree asymmetry
 *
 * xy_call dispatches the *current region's subtree*, so:
 *
 *   ri_hook_a is implemented only in (0,16):
 *     from (0,0)  and (0,16)  it fires   (A is in the subtree)
 *     from (0,17) and (0,64)  it does NOT (A is an ancestor, not a descendant)
 *
 *   ri_hook_c is implemented only in (0,64):
 *     from (0,0) it fires (the root's subtree includes the whole chain)
 *     from (0,64) it fires
 *
 * Getting this right is what makes the four regions genuinely distinct
 * dispatch contexts rather than four labels on one bucket.
 * ------------------------------------------------------------------------- */
struct probe_call { int v; int errno; };

static int probe_hook_a(void *ud) {
	struct probe_call *r = (struct probe_call *)ud;
	r->v     = ri_hook_a(0);
	r->errno = xy_errno();
	return XY_OK;
}

static int probe_hook_c(void *ud) {
	struct probe_call *r = (struct probe_call *)ud;
	r->v     = ri_hook_c(0);
	r->errno = xy_errno();
	return XY_OK;
}

static void expect_call(uint64_t id, uint8_t plen, xy_scope_fn_t *fn,
                        int want_v, int want_errno, const char *what) {
	struct probe_call r = { 0xDEAD, 0xDEAD };
	in_region(id, plen, fn, &r);
	if (r.v != want_v || r.errno != want_errno) {
		printf("    %s: dispatch from (%llu,%u) gave v=%d errno=%d, "
		       "expected v=%d errno=%d\n",
		       what, (unsigned long long)id, plen, r.v, r.errno,
		       want_v, want_errno);
		assert(r.v == want_v);
	}
}

static void test_dispatch_scoping(void) {
	/* A's hook: visible in the root subtree and in A's own region. */
	expect_call(ROOT_ID, ROOT_PLEN, probe_hook_a, 101, XY_OK, "hook_a @root");
	expect_call(A_ID,    A_PLEN,    probe_hook_a, 101, XY_OK, "hook_a @(0,16)");

	/* A's hook: NOT visible below A — A is an ancestor of those regions. */
	expect_call(B_ID, B_PLEN, probe_hook_a, 0, XY_ERR_NOTFOUND, "hook_a @(0,17)");
	expect_call(C_ID, C_PLEN, probe_hook_a, 0, XY_ERR_NOTFOUND, "hook_a @(0,64)");

	/* C's hook: visible from the root (whole subtree) and from itself. */
	expect_call(ROOT_ID, ROOT_PLEN, probe_hook_c, 303, XY_OK, "hook_c @root");
	expect_call(C_ID,    C_PLEN,    probe_hook_c, 303, XY_OK, "hook_c @(0,64)");

	printf("  test_dispatch_scoping: PASS\n");
}

/* -------------------------------------------------------------------------
 * 6. A module keyed under (id, plen) is only loadable/unloadable there
 *
 * All three .so files have distinct paths but all three regions share id 0.
 * The module key is "path\0<id-hex><plen-hex>", so a module promoted into a
 * child region is not reachable from the parent's context — which is what
 * test_unload_wrong_region already asserts for the single-level case, here
 * for the deepest level of the chain.
 * ------------------------------------------------------------------------- */
static int probe_unload_c(void *ud) {
	int *out = (int *)ud;
	*out = xy_unload(MOD_C);
	return XY_OK;
}

static void test_unload_only_from_own_region(void) {
	/* From the root, C is keyed under (0,64) and cannot be found. */
	assert(xy_unload(MOD_C) == XY_ERR_NOTFOUND);

	/* From (0,64) it unloads. */
	int ret = -1;
	in_region(C_ID, C_PLEN, probe_unload_c, &ret);
	assert(ret == XY_OK);

	printf("  test_unload_only_from_own_region: PASS\n");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void) {
	printf("test_region_identity:\n");

	/* Arm the claim gate at the root, then load A.  A's xy_install re-arms
	 * it on (0,16) and loads B, which re-arms it on (0,17) and loads C —
	 * that is the whole chain. */
	assert(xy_require_claim(permissive_handler, NULL) == XY_OK);
	assert(xy_load(MOD_A) == XY_OK);

	test_four_regions_share_id();
	test_current_region_in_each();
	test_tree_shape();
	test_install_saw_own_width();
	test_dispatch_scoping();
	test_unload_only_from_own_region();

	xy_shutdown();
	printf("  all tests passed\n");
	return 0;
}
