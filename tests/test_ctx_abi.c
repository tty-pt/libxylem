/*
 * test_ctx_abi: the context-ABI gate.
 *
 * The host fills a module's context by writing sizeof(struct xy_ctx) bytes
 * into it — the size *the host* was compiled with.  A module built against a
 * different header has a smaller object, and the tail of that write lands in
 * whatever the linker placed next to it in the module's BSS.
 *
 * That failure is invisible at the source level.  The load succeeds, every
 * function pointer both versions share works, and the damage appears later in
 * an unrelated library as a bad pointer.  It has already happened once here:
 * CP-3 grew struct xy_ctx from 144 to 160 bytes, libaxil-nd and libaxil-tty
 * were built against the older system-installed header, and the 16-byte
 * overrun clobbered libaxil-tty's static mux_map — which then took the server
 * down with SIGSEGV on its first HTTP request.  CP-4 grew it again, to 184.
 *
 * No geometry is hardcoded here.  The sizes, the generation gap and the canary
 * count all come from the host header and from the fixture itself, so an ABI
 * bump keeps this gate meaningful instead of quietly turning it into a test of
 * two stale literals.
 *
 * So this test asserts two separate things, and the second matters as much as
 * the first:
 *
 *   1. the load is refused with XY_ERR_ABI, and
 *   2. the memory just past the module's context is *untouched*.
 *
 * (2) is what distinguishes a real guard from a load-time `return -1`.  A
 * refusal that happened after the write would satisfy (1) and still corrupt
 * the process.  mod_stale_ctx places canaries immediately after its 144-byte
 * context, exactly where the 16-byte overrun lands.
 *
 * Refusing a module that omits the handshake is also tested, because it is a
 * deliberate cost rather than a limitation: mod_no_abi is a perfectly
 * correct module that would work if it merely said so.
 */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <ttypt/xy.h>

#define MOD_STALE "tests/mods/mod_stale_ctx"
#define MOD_NOABI "tests/mods/mod_no_abi"
#define MOD_GOOD  "tests/mods/mod_claim_simple"

/* The host's own view of the contract. */
static void test_descriptor_is_self_consistent(void)
{
	assert(XY_CTX_SIZE == sizeof(struct xy_ctx));
	assert(XY_CTX_ABI_DESC ==
	       (((uint64_t)XY_CTX_ABI_VER << 32) | (uint64_t)XY_CTX_SIZE));
	printf("ok  host descriptor: ABI %d, size %u\n",
	       (unsigned)XY_CTX_ABI_VER, (unsigned)XY_CTX_SIZE);
}

static void test_stale_ctx_is_refused(void)
{
	int ret = xy_load(MOD_STALE);

	printf("    xy_load(stale) -> %d (%s)\n", ret, xy_strerror(ret));
	assert(ret == XY_ERR_ABI);

	/* Refused loudly *and* diagnosably: a caller that only sees a negative
	 * code can tell this apart from "no such module". */
	assert(ret != XY_ERR_NOTFOUND);

	/* The module must not be registered, so nothing can call through its
	 * uninitialised context later. */
	assert(xy_unload(MOD_STALE) != XY_OK);
	printf("ok  stale-context module refused with XY_ERR_ABI\n");
}

static void test_stale_ctx_memory_intact(void)
{
	/* Read the canaries out of the loaded module.  A handle is obtained with
	 * RTLD_NOLOAD because xy_load refused the module: it is mapped and its
	 * .bss initialised, but never bound. */
	void *h = dlopen(MOD_STALE ".so", RTLD_NOLOAD | RTLD_NOW);
	/* POSIX guarantees this conversion; -Wpedantic objects to the spelling. */
	void *(*get_xy)(void) = NULL;
	uintptr_t (*canary_off)(void) = NULL;
	uintptr_t (*declared_size)(void) = NULL;
	unsigned  (*canary_words)(void) = NULL;
	uint64_t  (*canary_value)(unsigned) = NULL;
	uint64_t *canary;
	char *ctx;
	size_t stale, nwords;

	assert(h != NULL);
	*(void **)(&get_xy)          = dlsym(h, "get_xy_ptr");
	*(void **)(&canary_off)      = dlsym(h, "xy_ctx_canary_offset");
	*(void **)(&declared_size)   = dlsym(h, "xy_ctx_declared_size");
	*(void **)(&canary_words)    = dlsym(h, "xy_ctx_canary_words");
	*(void **)(&canary_value)    = dlsym(h, "xy_ctx_canary_value");
	assert(get_xy && canary_off && declared_size && canary_words && canary_value);

	ctx     = (char *)get_xy();   /* also primes the canaries */
	stale   = (size_t)declared_size();
	nwords  = (size_t)canary_words();
	canary  = (uint64_t *)(ctx + canary_off());

	/* The assertion that makes this test mean anything.  Without it the
	 * canary could sit in a different section from the context, be checked
	 * at the wrong offset, or be optimised away — and the test would pass
	 * while proving nothing.  An earlier version of this fixture did exactly
	 * that: a zeroed context in .bss and an initialised canary in .data are
	 * never adjacent, so the "intact" result was vacuous.
	 *
	 * Both conditions are checked here rather than assumed, and both are
	 * derived rather than hardcoded:
	 *   - the canaries start exactly where the stale context ends, and
	 *   - a host-sized write reaches into all of them
	 *     (stale < XY_CTX_SIZE <= stale + nwords*8). */
	assert(stale < sizeof(struct xy_ctx));
	assert(sizeof(struct xy_ctx) <= stale + nwords * sizeof(uint64_t));
	assert(canary == (uint64_t *)(ctx + stale));
	printf("    stale context %zu B, host context %zu B, overrun %zu B "
	       "into %zu canary word(s) at ctx+%zu\n",
	       stale, sizeof(struct xy_ctx), sizeof(struct xy_ctx) - stale,
	       nwords, canary_off());

	/* This is the assertion that matters.  Without the gate the host writes
	 * XY_CTX_SIZE bytes into a stale-sized object and these words come back
	 * holding the addresses of whichever functions the new generation added. */
	for (size_t i = 0; i < nwords; i++)
		assert(canary[i] == canary_value((unsigned)i));
	printf("ok  canaries past the stale context intact (%zu word(s)) "
	       "- no %zu-byte overrun\n",
	       nwords, sizeof(struct xy_ctx) - stale);
}

static void test_missing_handshake_is_refused(void)
{
	int ret = xy_load(MOD_NOABI);

	printf("    xy_load(no-abi) -> %d (%s)\n", ret, xy_strerror(ret));
	assert(ret == XY_ERR_ABI);
	printf("ok  module without xy_ctx_abi() refused\n");
}

static void test_modern_module_still_loads(void)
{
	/* The gate must not cost us anything on the happy path: a module that
	 * declares the current descriptor loads exactly as before. */
	int ret = xy_load(MOD_GOOD);

	printf("    xy_load(good) -> %d (%s)\n", ret, xy_strerror(ret));
	assert(ret == XY_OK);
	printf("ok  modern module unaffected by the gate\n");
}

int main(void)
{
	xy_init();
	printf("test_ctx_abi\n");

	test_descriptor_is_self_consistent();
	test_stale_ctx_is_refused();
	test_stale_ctx_memory_intact();
	test_missing_handshake_is_refused();
	test_modern_module_still_loads();

	printf("PASS test_ctx_abi\n");
	return 0;
}
