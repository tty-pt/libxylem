/* Unit coverage for module_same_file(), the module-identity compare used on
 * platforms without dlinfo()/RTLD_DI_LINKMAP (macOS, OpenBSD).  It runs
 * everywhere, so the fallback the BSD/macOS CI jobs take is also checked
 * on Linux/glibc. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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

int main(void) {
	printf("test_objectpath:\n");

	test_identical_strings();
	test_null_args();
	test_dotted_spellings();
	test_symlink();
	test_distinct_files();

	printf("  all tests passed\n");
	return 0;
}
