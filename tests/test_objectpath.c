/* Unit coverage for module_same_file(), the module-identity compare used on
 * platforms without dlinfo()/RTLD_DI_LINKMAP (macOS, OpenBSD).  It runs
 * everywhere, so the fallback the BSD/macOS CI jobs take is also checked
 * on Linux/glibc. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/libxylem-internal.h"

#define MOD_SO  "./tests/mods/mod_basic.so"
#define MOD_LINK "./tests/mods/mod_basic_link.so"
/* symlink() target is resolved relative to the link itself, which lives in
 * tests/mods/ — so the leaf name, not the path the tests load by. */
#define MOD_LEAF "mod_basic.so"

static void test_identical_strings(void) {
	assert(module_same_file(MOD_SO, MOD_SO) == 1);
	/* strcmp short-circuits, so this holds even for a path that does not
	 * exist: a reload tmp copy is unlinked while the module is loaded. */
	assert(module_same_file("/nonexistent/x.so", "/nonexistent/x.so") == 1);
	printf("  test_identical_strings: PASS\n");
}

static void test_null_args(void) {
	assert(module_same_file(NULL, MOD_SO) == 0);
	assert(module_same_file(MOD_SO, NULL) == 0);
	assert(module_same_file(NULL, NULL) == 0);
	printf("  test_null_args: PASS\n");
}

static void test_dotted_spellings(void) {
	assert(module_same_file(MOD_SO, "./tests/mods/../mods/mod_basic.so") == 1);
	assert(module_same_file("./tests/mods/mod_basic.so", "tests/mods/mod_basic.so") == 1);
	printf("  test_dotted_spellings: PASS\n");
}

static void test_symlink(void) {
	unlink(MOD_LINK);
	assert(symlink(MOD_LEAF, MOD_LINK) == 0);
	assert(module_same_file(MOD_LINK, MOD_SO) == 1);
	assert(module_same_file(MOD_LINK, "./tests/mods/../mods/mod_basic.so") == 1);
	unlink(MOD_LINK);
	/* A dangling symlink names no file: stat() fails, so this is a miss
	 * unless the two strings are literally equal. */
	assert(symlink("does_not_exist.so", MOD_LINK) == 0);
	assert(module_same_file(MOD_LINK, MOD_SO) == 0);
	unlink(MOD_LINK);
	printf("  test_symlink: PASS\n");
}

static void test_distinct_files(void) {
	assert(module_same_file(MOD_SO, "./tests/mods/mod_multi.so") == 0);
	assert(module_same_file(MOD_SO, "/nonexistent/x.so") == 0);
	printf("  test_distinct_files: PASS\n");
}

/* Soname-form loads ("libaxil-auth") must resolve against LD_LIBRARY_PATH.
 * module_load_path() is what xy_load stores as the module identity, and on
 * platforms without dlinfo() (macOS, OpenBSD) module_symbol_is_local()
 * compares that stored string against dladdr()'s full mapped path. A bare
 * "name.so" can never match, so EVERY hook of a soname-loaded module was
 * dropped there -- live incident: sessions never resolved on OpenBSD while
 * path-loaded modules kept their hooks. These tests run with CWD at the
 * repo root (Makefile test target), where no bare "mod_basic.so" exists,
 * so only a real library-path search can resolve it. */
static void test_soname_resolves_via_ld_library_path(void) {
	char mods_abs[PATH_MAX];
	char *old = getenv("LD_LIBRARY_PATH");
	char oldbuf[4096] = {0};
	if (old)
		snprintf(oldbuf, sizeof(oldbuf), "%s", old);

	assert(realpath("./tests/mods", mods_abs) != NULL);
	setenv("LD_LIBRARY_PATH", mods_abs, 1);

	char *resolved = module_load_path("mod_basic");
	assert(resolved != NULL);

	char want[PATH_MAX];
	snprintf(want, sizeof(want), "%s/mod_basic.so", mods_abs);
	/* Canonical absolute path ... */
	assert(strcmp(resolved, want) == 0);
	/* ... agreeing (by file identity, not string) with every spelling
	 * dladdr() or a caller might report. */
	assert(module_same_file(resolved, "./tests/mods/mod_basic.so") == 1);
	assert(module_same_file(resolved, "tests/mods/mod_basic.so") == 1);
	free(resolved);

	if (oldbuf[0])
		setenv("LD_LIBRARY_PATH", oldbuf, 1);
	else
		unsetenv("LD_LIBRARY_PATH");
	printf("  test_soname_resolves_via_ld_library_path: PASS\n");
}

static void test_unresolvable_keeps_bare_name(void) {
	char *old = getenv("LD_LIBRARY_PATH");
	char oldbuf[4096] = {0};
	if (old)
		snprintf(oldbuf, sizeof(oldbuf), "%s", old);

	setenv("LD_LIBRARY_PATH", "/nonexistent-dir-xy-test", 1);
	char *resolved = module_load_path("definitely_not_a_module_xyz");
	assert(resolved != NULL);
	/* Unresolvable stays exactly as before: bare name, dlopen's problem. */
	assert(strcmp(resolved, "definitely_not_a_module_xyz.so") == 0);
	free(resolved);

	if (oldbuf[0])
		setenv("LD_LIBRARY_PATH", oldbuf, 1);
	else
		unsetenv("LD_LIBRARY_PATH");
	printf("  test_unresolvable_keeps_bare_name: PASS\n");
}

int main(void) {
	printf("test_objectpath:\n");

	test_identical_strings();
	test_null_args();
	test_dotted_spellings();
	test_symlink();
	test_distinct_files();
	test_soname_resolves_via_ld_library_path();
	test_unresolvable_keeps_bare_name();

	printf("  all tests passed\n");
	return 0;
}
