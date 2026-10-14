/*
 * test_claim_at: the CP-4 gate (ST.md §8 Phase 0, §4.5(1)(3)(4), §16).
 *
 * CP-3 made a region the pair (id, plen) and proved four regions can share
 * id == 0.  CP-4 adds the three calls that make the pair *creatable* and
 * *queryable* at runtime:
 *
 *   xy_claim_at()  — create the region (id, plen) under its nearest ancestor
 *   xy_region_at() — which region is this point in?
 *   xy_call_self() — dispatch to the current region's OWN modules only
 *
 * The tree this test builds, all through the public API:
 *
 *   (0,0)  root                            [pre-existing]
 *    |
 *    +-- (0,16) -- (0,32) -- (0,48) -- (0,64)     the leftmost ladder: every
 *    |                                             level shares id == 0
 *    +-- (1<<48,16)  planet 1
 *    |     +-- (1<<48 | 1<<32, 32)   planet 1's left child
 *    +-- (2<<48,16)  planet 2                       SIBLING of planet 1
 *    +-- (3<<48,16) -- (3<<48,64)     a jump: 16 -> 64, skipping 32 and 48
 *
 * Three of these are discriminating, i.e. they FAIL against a build that
 * gets the corresponding idea wrong rather than merely returning an error:
 *
 *  - Planet 1 and planet 2 must be children of the ROOT, not of (0,16).
 *    Only the high-bit containment mask gets this right.  A low-bit mask
 *    agrees for the root (whose mask is 0) and then reports region_alloc_slot's
 *    own siblings as nested, so (1<<48,16) comes out "inside" (0,16) and the
 *    root ends up with a single child.
 *
 *  - The (3<<48,16) -> (3<<48,64) jump must attach directly under the plen-16
 *    parent, inventing no (3<<48,32) or (3<<48,48).  §4.3(1) makes a multi-bit
 *    jump legal, so a build that materialises intermediates fails here.
 *
 *  - The coarse-to-fine walk must run each of the three path modules exactly
 *    once (3 runs).  A subtree-scope walk down the same path runs 7: planet 2
 *    gets dragged in by the root's subtree, and planet 1 and its child are
 *    re-run at every level.  That 3-vs-7 gap is the whole reason
 *    xy_call_self() exists.
 *
 * Two traps worth naming, both of which make a test pass while testing nothing:
 *
 *  - A zero return does NOT prove a listener ran -- an empty dispatch also
 *    returns 0.  Every dispatch assertion here is on the per-module counters,
 *    read back with dlsym(RTLD_NOLOAD), plus the status xy_call_self()
 *    actually returns (XY_ERR_NOTFOUND for an empty region).  xy_call() leaves
 *    only the LAST runner's value in retp, so counters are the only way to
 *    learn *who* ran.
 *
 *  - xy_load() appends the .so suffix itself, so load paths are given without
 *    it, while the dlsym handles need it.
 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/xy.h>

#include "claimat_hooks.h"

/* ---- The tree ---------------------------------------------------------- */
#define P1_ID    (1ULL << 48)                    /* planet 1              */
#define P1C_ID   ((1ULL << 48) | (1ULL << 32))   /* planet 1's left child */
#define P2_ID    (2ULL << 48)                    /* planet 2              */
#define P3_ID    (3ULL << 48)                    /* the jumping planet    */
#define P3_DEEP  (3ULL << 48)                    /* claimed straight at 64 */

#define MOD_ROOT    "tests/mods/mod_ca_root"
#define MOD_P1      "tests/mods/mod_ca_p1"
#define MOD_P1CHILD "tests/mods/mod_ca_p1child"
#define MOD_P2      "tests/mods/mod_ca_p2"

/* -------------------------------------------------------------------------
 * xy_with_region trampoline (ST.md §16: one documented shape, used everywhere)
 * ------------------------------------------------------------------------- */
static int load_trampoline(void *ud)
{
	return xy_load((char *)ud);
}

static void load_into(uint64_t id, uint8_t plen, const char *path)
{
	assert(xy_with_region(id, plen, load_trampoline, (void *)path) == XY_OK);
}

/* ---- Per-module run counters ------------------------------------------ */
typedef struct {
	unsigned root, p1, p1child, p2;
} ca_counts_t;

static unsigned counter_of(const char *so_path)
{
	void *h = dlopen(so_path, RTLD_NOLOAD | RTLD_NOW);
	unsigned *v;

	assert(h != NULL);            /* the module must already be loaded */
	v = (unsigned *)dlsym(h, "ca_calls");
	assert(v != NULL);
	return *v;
}

static void snapshot(ca_counts_t *c)
{
	c->root    = counter_of(MOD_ROOT ".so");
	c->p1      = counter_of(MOD_P1 ".so");
	c->p1child = counter_of(MOD_P1CHILD ".so");
	c->p2      = counter_of(MOD_P2 ".so");
}

static unsigned total(const ca_counts_t *c)
{
	return c->root + c->p1 + c->p1child + c->p2;
}

static void delta(const ca_counts_t *a, const ca_counts_t *b, ca_counts_t *d)
{
	d->root    = b->root    - a->root;
	d->p1      = b->p1      - a->p1;
	d->p1child = b->p1child - a->p1child;
	d->p2      = b->p2      - a->p2;
}

static void show(const char *what, const ca_counts_t *d)
{
	printf("  %-34s root +%u | p1 +%u | p1child +%u | p2 +%u\n",
	       what, d->root, d->p1, d->p1child, d->p2);
}

/* ---- Miscellaneous file-scope trampolines ------------------------------ */
static int read_current_trampoline(void *ud)
{
	struct { uint64_t id; uint8_t plen; } *p = ud;

	p->id   = xy_current_region();
	p->plen = xy_current_region_plen();
	return XY_OK;
}

static int deny_probe_trampoline(void *ud)
{
	*(int *)ud = xy_deny("ca_probe", XY_DENY_HOOK);
	return XY_OK;
}

/* ---- Dispatch helpers -------------------------------------------------- */
/* The host TU deliberately calls the extern dispatch functions rather than
 * the XY_CALL / XY_CALL_SELF macros: those are statement macros living in
 * xy-mod.h (they go through the module-side `xy` context pointer), and this
 * is the host, which has the real functions.  XY_HOOK_DEF in claimat_hooks.h
 * exported ca_probe_adapter, so the same adapter drives both scopes. */
struct dispatch_req {
	int    self_only;
	int    status;
	ca_counts_t before, after;
};

/* xy_errno() must be read *inside* the trampoline.  snapshot() below calls
 * dlopen/dlsym straight after the dispatch, and those reset the thread-local
 * error, so a read taken after xy_with_region() returns has already been
 * overwritten -- which is how an earlier draft of this test came to assert on
 * 0 where it meant XY_ERR_NOTFOUND. */
static int last_dispatch_errno;

static int dispatch_trampoline(void *ud)
{
	struct dispatch_req *r = (struct dispatch_req *)ud;
	struct ca_probe_args args = { 0 };

	r->status = r->self_only
		? xy_call_self(NULL, &ca_probe_adapter, &args)
		: xy_call(NULL, &ca_probe_adapter, &args);
	last_dispatch_errno = xy_errno();
	snapshot(&r->after);
	return XY_OK;
}

/* Dispatch in (id, plen) and report both the status and who ran. */
static int dispatch_in(uint64_t id, uint8_t plen, int self_only,
                       ca_counts_t *d)
{
	struct dispatch_req r;

	r.self_only = self_only;
	snapshot(&r.before);
	assert(xy_with_region(id, plen, dispatch_trampoline, &r) == XY_OK);
	if (d)
		delta(&r.before, &r.after, d);
	return r.status;
}

/* ---- region_each collector -------------------------------------------- */
#define MAX_KIDS 8

typedef struct {
	uint64_t id[MAX_KIDS];
	uint8_t  plen[MAX_KIDS];
	int      n;
} kids_t;

static int collect_kid(uint64_t id, uint8_t plen, void *ud)
{
	kids_t *k = (kids_t *)ud;

	assert(k->n < MAX_KIDS);
	k->id[k->n]   = id;
	k->plen[k->n] = plen;
	k->n++;
	return XY_OK;
}

struct kids_req { kids_t *k; };

static int kids_trampoline(void *ud)
{
	struct kids_req *r = (struct kids_req *)ud;

	r->k->n = 0;
	return xy_region_each(collect_kid, r->k);
}

/* The immediate children of (id, plen). */
static kids_t children_of(uint64_t id, uint8_t plen)
{
	kids_t k = { { 0 }, { 0 }, 0 };
	struct kids_req r = { &k };

	assert(xy_with_region(id, plen, kids_trampoline, &r) == XY_OK);
	return k;
}

static int has_kid(const kids_t *k, uint64_t id, uint8_t plen)
{
	for (int i = 0; i < k->n; i++)
		if (k->id[i] == id && k->plen[i] == plen)
			return 1;
	return 0;
}

/* =========================================================================
 * Build the whole tree once, in dependency order, so every later test can
 * assert against a complete tree instead of whatever its predecessors happened
 * to have created.  All the per-test claim calls that remain are therefore
 * idempotent re-claims, which is itself part of what is being tested.
 * ========================================================================= */
static void build_tree(void)
{
	/* the leftmost ladder, five regions sharing id 0 */
	assert(xy_claim_at(0, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0, 32, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0, 48, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0, 64, NULL, NULL) == XY_OK);

	/* planet 1 and its left child; note P1C_ID's high 16 bits are exactly
	 * P1_ID, so the child shares its parent's id at a wider prefix */
	assert(xy_claim_at(P1_ID, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(P1C_ID, 32, NULL, NULL) == XY_OK);

	/* planet 2, planet 1's sibling */
	assert(xy_claim_at(P2_ID, 16, NULL, NULL) == XY_OK);

	/* the jumping planet: 16 straight to 64, skipping 32 and 48 */
	assert(xy_claim_at(P3_ID, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(P3_DEEP, 64, NULL, NULL) == XY_OK);

	/* plen 64 with every bit set, the widest legal point */
	assert(xy_claim_at(0xFFFFFFFFFFFFFFFFULL, 64, NULL, NULL) == XY_OK);
}

/* =========================================================================
 * 1. The leftmost ladder: (0,0) -> (0,16) -> (0,32) -> (0,48) -> (0,64)
 *    Five regions, one id.  This is CP-3's invariant driven through the new
 *    creation API rather than through the claim gate.
 * ========================================================================= */
static void test_nested_prefix_ladder(void)
{
	static const uint8_t widths[] = { 16, 32, 48, 64 };

	assert(xy_region_exists(0, 0) == XY_OK);
	for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++)
		assert(xy_claim_at(0, widths[i], NULL, NULL) == XY_OK);
	for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++)
		assert(xy_region_exists(0, widths[i]) == XY_OK);

	/* Each rung is the *only* child of the rung above it. */
	for (size_t i = 0; i + 1 < sizeof widths / sizeof widths[0]; i++) {
		kids_t k = children_of(0, widths[i]);
		assert(k.n == 1);
		assert(k.id[0] == 0 && k.plen[0] == widths[i + 1]);
	}
	/* The deepest rung has no children. */
	assert(children_of(0, 64).n == 0);

	printf("  test_nested_prefix_ladder: PASS (5 regions share id 0)\n");
}

/* =========================================================================
 * 2. The jump that skips levels, and the sibling-ness of the planets.
 * ========================================================================= */
static void test_jump_skips_levels(void)
{
	kids_t root;

	assert(xy_claim_at(P3_ID, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(P3_DEEP, 64, NULL, NULL) == XY_OK);

	/* The jump attached straight under the plen-16 parent... */
	kids_t p3 = children_of(P3_ID, 16);
	assert(p3.n == 1);
	assert(p3.id[0] == P3_DEEP && p3.plen[0] == 64);

	/* ...and invented nothing in between. */
	assert(xy_region_exists(P3_ID, 32) == XY_ERR_NOTFOUND);
	assert(xy_region_exists(P3_ID, 48) == XY_ERR_NOTFOUND);

	/* THE DISCRIMINATING ASSERTION.  Every planet, plus the (0,16) rung and
	 * the all-ones plen-64 point, is a direct child of the root.  A low-bit
	 * containment mask nests the planets under (0,16) instead, because
	 * (1<<48,16) and (0,16) share zero low bits -- and then this root has
	 * two children, not five. */
	root = children_of(0, 0);
	printf("  root has %d children:", root.n);
	for (int i = 0; i < root.n; i++)
		printf(" (%#llx,%u)", (unsigned long long)root.id[i], root.plen[i]);
	printf("\n");
	assert(root.n == 5);
	assert(has_kid(&root, 0, 16));
	assert(has_kid(&root, P1_ID, 16));
	assert(has_kid(&root, P2_ID, 16));
	assert(has_kid(&root, P3_ID, 16));

	/* Planet 1 and planet 2 are siblings: neither is inside the other. */
	kids_t p1 = children_of(P1_ID, 16);
	assert(p1.n == 1);
	assert(p1.id[0] == P1C_ID && p1.plen[0] == 32);
	assert(!has_kid(&p1, P2_ID, 16));

	printf("  test_jump_skips_levels: PASS (16->64 jump, no intermediates, "
	       "5 root children)\n");
}

/* =========================================================================
 * 3. Idempotent re-claim adds nothing, and the root re-claims cleanly.
 * ========================================================================= */
static void test_idempotent_reclaim(void)
{
	kids_t before, after;
	uint8_t plen = 0xFF;

	before = children_of(0, 0);
	assert(xy_claim_at(0, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0, 16, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0, 16, NULL, NULL) == XY_OK);
	after = children_of(0, 0);
	assert(before.n == after.n);

	/* Re-claiming a region also makes it current, and re-claiming the root
	 * works even though the root is its own nearest ancestor. */
	assert(xy_claim_at(P1C_ID, 32, NULL, NULL) == XY_OK);
	assert(xy_current_region() == P1C_ID);
	assert(xy_current_region_plen() == 32);
	assert(xy_claim_at(0, 0, NULL, NULL) == XY_OK);
	assert(xy_current_region() == XY_REGION_ROOT);
	assert(xy_current_region_plen() == 0);

	/* And the out-param of region_at agrees with what we just claimed. */
	assert(xy_region_at(P1C_ID, 32, &plen) == P1C_ID && plen == 32);

	printf("  test_idempotent_reclaim: PASS (no duplicate entries)\n");
}

/* =========================================================================
 * 4. Misaligned ids are rejected and nothing is created.
 * ========================================================================= */
static void test_rejects_misaligned(void)
{
	/* 1<<16 sets a bit far below the plen-16 prefix, so it cannot be a
	 * canonical plen-16 id.  0x1234_5678_9abc_d000 is the same idea with
	 * more noise in the low half. */
	static const uint64_t bad[] = {
		1ULL << 16,
		0x123456789abcd000ULL,
		0xFFFFFFFFFFFFFFFFULL,
	};

	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		int r = xy_claim_at(bad[i], 16, NULL, NULL);

		assert(r == XY_ERR_INVALID);
		assert(xy_errno() == XY_ERR_INVALID);
		assert(xy_region_exists(bad[i], 16) == XY_ERR_NOTFOUND);
	}

	printf("  test_rejects_misaligned: PASS\n");
}

/* =========================================================================
 * 5. plen > 64 is rejected, including the 8-bit wraparound values.
 * ========================================================================= */
static void test_rejects_plen_over_64(void)
{
	static const uint8_t bad[] = { 65, 100, 128, 255 };

	for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
		int r = xy_claim_at(0, bad[i], NULL, NULL);

		assert(r == XY_ERR_TOOBIG);
		assert(xy_errno() == XY_ERR_TOOBIG);
		assert(xy_region_exists(0, bad[i]) == XY_ERR_NOTFOUND);
	}

	/* plen == 64 itself is legal: the mask is all ones, so every id is
	 * aligned and the point can never be rejected for misalignment. */
	assert(xy_claim_at(0xFFFFFFFFFFFFFFFFULL, 64, NULL, NULL) == XY_OK);
	assert(xy_claim_at(0xFFFFFFFFFFFFFFFFULL, 64, NULL, NULL) == XY_OK);

	printf("  test_rejects_plen_over_64: PASS\n");
}

/* =========================================================================
 * 6. A same-id child is ACCEPTED, not rejected.
 *
 * ST.md §4.5(1) as written says "reject id == A->id (no new bits)", which
 * would reject (0,16) under (0,0) -- CP-3's flagship left-half child, the
 * exact case the whole region-identity rework exists for.  §16 restates it
 * correctly as "plen == plen_A with id == A->id", which is unreachable while
 * the ancestor search only considers plen_A < plen.
 *
 * So the implementation accepts the same-id child, and this test pins that
 * deliberately: if a future reader "fixes" xy_claim_at() to match §4.5(1)
 * literally, this is the assertion that fails.
 * ========================================================================= */
static void test_same_id_child_accepted(void)
{
	/* (0,16) under (0,0): same numeric id, one rung wider. */
	assert(xy_claim_at(0, 16, NULL, NULL) == XY_OK);
	assert(xy_region_exists(0, 16) == XY_OK);
	assert(xy_region_exists(0, 0) == XY_OK);

	/* Same again one rung down, and on the planet side: P1C_ID's high 16
	 * bits are exactly P1_ID, so P1C is a same-id child of P1. */
	assert(xy_claim_at(P1C_ID, 32, NULL, NULL) == XY_OK);
	kids_t p1 = children_of(P1_ID, 16);
	assert(p1.n == 1 && p1.id[0] == P1C_ID && p1.plen[0] == 32);

	printf("  test_same_id_child_accepted: PASS\n");
}

/* =========================================================================
 * 7. Planet 2: a non-canonical id whose lowest set bit implies a wrong width.
 *
 * 2<<48 at plen 16 has its lowest set bit at 49, so inferring the width from
 * the id would say 50.  Nothing may infer it: the width is 16 because that is
 * what was claimed, and (2<<48,50) must not exist.
 * ========================================================================= */
static void test_non_canonical_id(void)
{
	uint8_t plen = 0xFF;

	assert(xy_claim_at(P2_ID, 16, NULL, NULL) == XY_OK);
	assert(xy_region_exists(P2_ID, 16) == XY_OK);

	assert(xy_region_at(P2_ID, 16, &plen) == P2_ID);
	assert(plen == 16);                       /* not 50 */
	assert(xy_region_exists(P2_ID, 50) == XY_ERR_NOTFOUND);
	assert(xy_region_exists(P2_ID, 17) == XY_ERR_NOTFOUND);

	/* region_each yields the claimed width, not an inferred one. */
	kids_t root = children_of(0, 0);
	assert(has_kid(&root, P2_ID, 16));

	printf("  test_non_canonical_id: PASS (2<<48 is width 16, not 50)\n");
}

/* =========================================================================
 * 8. xy_region_at: deepest cover, and an exact hit that beats its ancestors.
 * ========================================================================= */
static void test_region_at_deepest_cover(void)
{
	uint8_t plen = 0xFF;

	/* Exact hit. */
	assert(xy_region_at(P1_ID, 16, &plen) == P1_ID && plen == 16);
	assert(xy_region_at(P2_ID, 16, &plen) == P2_ID && plen == 16);

	/* A point below planet 1 that no region reaches: 0xDEAD_BEEF in the low
	 * 32 bits misses planet 1's child at plen 32, so the deepest cover is
	 * planet 1 itself.  This is the "ancestor fallback" half of the answer. */
	plen = 0xFF;
	assert(xy_region_at(P1_ID | 0xDEADBEEFULL, 64, &plen) == P1_ID);
	assert(plen == 16);

	/* The same point *is* covered at plen 32 by the child, and the deeper
	 * region wins -- an exact hit must beat its own ancestor. */
	plen = 0xFF;
	assert(xy_region_at(P1C_ID, 64, &plen) == P1C_ID && plen == 32);

	/* The jump planet's deep rung is found and reported at its real width. */
	plen = 0xFF;
	assert(xy_region_at(P3_DEEP, 64, &plen) == P3_DEEP && plen == 64);

	/* A point that only the root covers: high 16 bits 9 match no planet and
	 * no ladder rung, so the fallback is the root at width 0. */
	plen = 0xFF;
	assert(xy_region_at(9ULL << 48, 64, &plen) == XY_REGION_ROOT);
	assert(plen == 0);

	/* A point inside the ladder lands on the deepest rung that actually
	 * covers it -- which is not the deepest rung that exists.  The cover
	 * test masks the HIGH plen bits, so what matters is where the point's
	 * set bits sit, not how small it is:
	 *
	 *   0x0000FFFF  bits 0..15  -> high 16/32/48 bits all zero, so (0,16),
	 *                             (0,32) and (0,48) all cover it but (0,64)
	 *                             does not.  Answer: (0,48), even though
	 *                             (0,64) is a real region below it.
	 *   0xDEADBEEF  bits 16..31 -> its high 48 bits are NOT all zero, so
	 *                             (0,48) misses it and the answer drops to
	 *                             (0,32).  Answer: (0,32).
	 */
	plen = 0xFF;
	assert(xy_region_at(0x0000FFFFULL, 64, &plen) == XY_REGION_ROOT);
	assert(plen == 48);
	plen = 0xFF;
	assert(xy_region_at(0xDEADBEEFULL, 64, &plen) == XY_REGION_ROOT);
	assert(plen == 32);

	/* Narrowing the query's own width narrows the answer the same way: a
	 * region wider than the query point cannot contain it. */
	plen = 0xFF;
	assert(xy_region_at(0x0000FFFFULL, 48, &plen) == XY_REGION_ROOT);
	assert(plen == 48);
	plen = 0xFF;
	assert(xy_region_at(0x0000FFFFULL, 32, &plen) == XY_REGION_ROOT);
	assert(plen == 32);

	/* The out-param is optional. */
	assert(xy_region_at(P1_ID, 16, NULL) == P1_ID);

	/* Failure paths: INVALID, and the width deterministically 0. */
	plen = 0xFF;
	assert(xy_region_at(P1_ID, 65, &plen) == XY_REGION_INVALID);
	assert(plen == 0);
	assert(xy_errno() == XY_ERR_TOOBIG);

	printf("  test_region_at_deepest_cover: PASS\n");
}

/* =========================================================================
 * 9. WHY the out-param is not optional decoration.
 *
 * Querying the (0,64) rung returns id 0 -- the same id as the root, the (0,16)
 * rung, the (0,32) rung and the (0,48) rung.  An id-only return is therefore
 * useless here: the caller could not tell which of the five it landed in and
 * so could not xy_with_region() to it.  This is the concrete instance of the
 * ambiguity CP-3 removed, and the reason xy_region_at() grew the third
 * parameter against ST.md §4.5(3).
 * ========================================================================= */
static void test_region_at_needs_its_width(void)
{
	uint8_t plen = 0xFF;

	assert(xy_region_at(0, 64, &plen) == XY_REGION_ROOT);  /* id is 0 ... */
	assert(plen == 64);                     /* ... only the width disambiguates */
	assert(xy_region_exists(0, 64) == XY_OK);

	/* Round-trip: the reported pair must be a region we can actually enter,
	 * and entering it must report the same width back. */
	struct { uint64_t id; uint8_t plen; } probe = { ~0ULL, 0xFF };

	assert(xy_with_region(XY_REGION_ROOT, plen, read_current_trampoline,
	                      &probe) == XY_OK);
	assert(probe.id == 0 && probe.plen == 64);

	printf("  test_region_at_needs_its_width: PASS (id 0 covers five regions)\n");
}

/* =========================================================================
 * 10. Dispatch scope.
 * ========================================================================= */
static void test_dispatch_scope(void)
{
	ca_counts_t d;
	int st;

	load_into(XY_REGION_ROOT, 0, MOD_ROOT);
	load_into(P1_ID, 16, MOD_P1);
	load_into(P1C_ID, 32, MOD_P1CHILD);
	load_into(P2_ID, 16, MOD_P2);

	/* xy_call from planet 1: the planet and its child, but NOT the root and
	 * NOT the sibling planet. */
	st = dispatch_in(P1_ID, 16, 0, &d);
	show("xy_call from planet 1", &d);
	assert(st == XY_OK);
	assert(d.root == 0 && d.p2 == 0);
	assert(d.p1 == 1 && d.p1child == 1);

	/* xy_call_self from planet 1: only its own module. */
	st = dispatch_in(P1_ID, 16, 1, &d);
	show("xy_call_self from planet 1", &d);
	assert(st == XY_OK);
	assert(d.root == 0 && d.p2 == 0 && d.p1child == 0);
	assert(d.p1 == 1);

	/* xy_call_self from the root: its own module only, no descendants --
	 * the exact counterpart of the subtree case just below. */
	st = dispatch_in(XY_REGION_ROOT, 0, 1, &d);
	show("xy_call_self from root", &d);
	assert(st == XY_OK);
	assert(d.root == 1);
	assert(d.p1 == 0 && d.p1child == 0 && d.p2 == 0);

	/* xy_call from the root reaches the entire subtree. */
	st = dispatch_in(XY_REGION_ROOT, 0, 0, &d);
	show("xy_call from root", &d);
	assert(st == XY_OK);
	assert(d.root == 1 && d.p1 == 1 && d.p1child == 1 && d.p2 == 1);

	printf("  test_dispatch_scope: PASS\n");
}

/* =========================================================================
 * 11. The coarse-to-fine walk: 3 runs, not 7.
 *
 * Walking root -> planet 1 -> planet 1's child with the exact-region scope
 * gives each region one turn: 3 runs total.  The same walk with subtree scope
 * runs 7 -- planet 2 is dragged in by the root's subtree, and planet 1 and
 * its child are re-run at every level they sit inside.  Eliminating exactly
 * that redundancy is the reason xy_call_self() exists.
 * ========================================================================= */
static void test_coarse_to_fine_walk(void)
{
	ca_counts_t before, after, d;
	unsigned exact_runs, subtree_runs;

	snapshot(&before);

	assert(dispatch_in(XY_REGION_ROOT, 0, 1, NULL) == XY_OK);
	assert(dispatch_in(P1_ID, 16, 1, NULL) == XY_OK);
	assert(dispatch_in(P1C_ID, 32, 1, NULL) == XY_OK);

	snapshot(&after);
	delta(&before, &after, &d);
	show("exact walk root->p1->p1c", &d);
	assert(d.root == 1 && d.p1 == 1 && d.p1child == 1);
	assert(d.p2 == 0);              /* never visited */
	exact_runs = total(&d);
	assert(exact_runs == 3);

	/* Same path, subtree scope, for contrast. */
	snapshot(&before);
	assert(dispatch_in(XY_REGION_ROOT, 0, 0, NULL) == XY_OK);
	assert(dispatch_in(P1_ID, 16, 0, NULL) == XY_OK);
	assert(dispatch_in(P1C_ID, 32, 0, NULL) == XY_OK);
	snapshot(&after);
	delta(&before, &after, &d);
	show("subtree walk root->p1->p1c", &d);
	subtree_runs = total(&d);

	printf("  exact walk ran %u listener(s); the subtree walk ran %u\n",
	       exact_runs, subtree_runs);
	assert(subtree_runs == 7);
	assert(exact_runs < subtree_runs);

	printf("  test_coarse_to_fine_walk: PASS (3 vs 7)\n");
}

/* =========================================================================
 * 12. An empty region reports NOTFOUND rather than a zero return.
 * ========================================================================= */
static void test_empty_region_reports_notfound(void)
{
	ca_counts_t d;
	uint8_t plen = 0xFF;
	int st;

	assert(xy_claim_at(0, 32, NULL, NULL) == XY_OK);   /* no modules ever */
	st = dispatch_in(0, 32, 1, &d);
	show("xy_call_self from empty (0,32)", &d);
	assert(st == XY_ERR_NOTFOUND);
	assert(last_dispatch_errno == XY_ERR_NOTFOUND);
	assert(total(&d) == 0);

	/* Same for subtree scope: (0,32) has no descendants either. */
	st = dispatch_in(0, 32, 0, &d);
	assert(st == XY_ERR_NOTFOUND);
	assert(total(&d) == 0);

	/* The empty ladder rungs report the same thing in both scopes: none of
	 * them has a module of its own, and none has a loaded descendant
	 * either, so there is nothing for either scope to reach.  The cases
	 * where the two scopes genuinely differ -- a region with its own module
	 * *and* a child with one -- are in test_dispatch_scope(). */
	plen = 0;
	assert(xy_region_at(0, 32, &plen) == 0 && plen == 32);
	for (uint8_t w = 16; w <= 48; w += 16) {
		st = dispatch_in(0, w, 1, &d);
		assert(st == XY_ERR_NOTFOUND);
		assert(total(&d) == 0);
		st = dispatch_in(0, w, 0, &d);
		assert(st == XY_ERR_NOTFOUND);
		assert(total(&d) == 0);
	}

	printf("  test_empty_region_reports_notfound: PASS\n");
}

/* =========================================================================
 * 13. Deny, as three separate statements (ST.md §16).
 *
 *   (a) a deny in an ancestor refuses a dispatch from a descendant, with
 *       XY_ERR_EPERM;
 *   (b) a region's own deny does NOT refuse that region's own dispatch --
 *       otherwise a region could lock itself out of a hook it still owns;
 *   (c) the refusal applies to xy_call_self() too, not just xy_call().
 *
 * This ordering matters: region_propagate_deny() maintains its summary bit
 * upward, and xy_dispatch() gates its ancestor walk on that bit.  Reading the
 * bit off the caller's own entry instead of the chain's root makes the whole
 * check skip for every non-root caller, which inverts (a) and (b) *at the same
 * time*: a root deny goes inert for all descendants, while still firing for
 * the root.  The assertions below are what pins that shut.
 * ========================================================================= */
static void test_deny_semantics(void)
{
	ca_counts_t d;
	int st;

	/* Arm a root-level deny on the probe hook. */
	int deny_status = -1;

	assert(xy_with_region(XY_REGION_ROOT, 0, deny_probe_trampoline,
	                      &deny_status) == XY_OK);
	assert(deny_status == XY_OK);

	/* (a) + (c): refused from the planet, in both scopes, and no listener
	 * runs -- the refusal must happen before any dispatch, not after. */
	st = dispatch_in(P1_ID, 16, 0, &d);
	show("xy_call from planet1 (root denied)", &d);
	assert(st == XY_ERR_EPERM);
	assert(last_dispatch_errno == XY_ERR_EPERM);
	assert(total(&d) == 0);

	st = dispatch_in(P1_ID, 16, 1, &d);
	show("xy_call_self from planet1 (denied)", &d);
	assert(st == XY_ERR_EPERM);
	assert(total(&d) == 0);

	/* (b): the root's own dispatch is unaffected by the root's own deny. */
	st = dispatch_in(XY_REGION_ROOT, 0, 1, &d);
	show("xy_call_self from root (own deny)", &d);
	assert(st == XY_OK);
	assert(d.root == 1);

	st = dispatch_in(XY_REGION_ROOT, 0, 0, &d);
	show("xy_call from root (own deny)", &d);
	assert(st == XY_OK);
	assert(d.root == 1 && d.p1 == 1 && d.p1child == 1 && d.p2 == 1);

	printf("  test_deny_semantics: PASS (EPERM below, OK at the denier)\n");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
	printf("test_claim_at:\n");

	build_tree();

	/* -- creation and identity (no modules loaded yet) -- */
	test_nested_prefix_ladder();
	test_jump_skips_levels();
	test_idempotent_reclaim();
	test_rejects_misaligned();
	test_rejects_plen_over_64();
	test_same_id_child_accepted();
	test_non_canonical_id();
	test_region_at_deepest_cover();
	test_region_at_needs_its_width();

	/* -- dispatch, which needs modules -- */
	test_dispatch_scope();
	test_coarse_to_fine_walk();
	test_empty_region_reports_notfound();

	/* -- deny last: it is region state with no easy way back -- */
	test_deny_semantics();

	xy_shutdown();
	printf("  all tests passed\n");
	return 0;
}