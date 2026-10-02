/*
 * test_xy_last_dispatch — regression coverage for libxylem commit 3a2d32e
 * ("xy.last fix").
 *
 * Before that fix, xy_last_ran stayed 0 for the whole dispatch loop and was
 * only set to the total afterwards, so xy.last() called from *inside* a
 * handler always returned XY_ERR_NOTFOUND -- the listener-chain-reads-its-
 * predecessor pattern (NeverDark MODS.md Sec12.2) was unobservable. The fix
 * also closed a second, independent bug: a handler that itself nest-calls
 * another hook clobbers the TLS xy_last state for the duration of that
 * nested dispatch and never restores it, so naively republishing only once
 * (instead of after every dispatch_call) would let the NEXT listener in the
 * outer chain observe the nested call's result instead of its true
 * predecessor's.
 *
 * Topology: three modules listen for probe_value(int), loaded in order
 * first, second, third (dispatch order == load order, confirmed by
 * test_multi_call.c). second's handler reads xy_last() (expecting first's
 * return) and then nest-calls a fourth module's unrelated inner_probe hook
 * before returning. third's handler reads xy_last() again and must see
 * second's return (205), not inner_probe's (1006) and not NOTFOUND.
 */
#include <assert.h>
#include <stdio.h>
#include <ttypt/xy.h>

XY_HOOK_DEF(int, probe_value, int, x);
XY_HOOK_DEF(int, second_observed, int, unused);
XY_HOOK_DEF(int, third_observed, int, unused);

int
main(void)
{
	int ret, second_saw, third_saw;

	ret = xy_load("./tests/mods/mod_xylast_inner");
	assert(ret == XY_OK);
	ret = xy_load("./tests/mods/mod_xylast_first");
	assert(ret == XY_OK);
	ret = xy_load("./tests/mods/mod_xylast_second");
	assert(ret == XY_OK);
	ret = xy_load("./tests/mods/mod_xylast_third");
	assert(ret == XY_OK);

	ret = probe_value(5);
	printf("probe_value(5) = %d (expect 305: last listener wins, 5+300)\n",
		ret);
	assert(ret == 305);

	second_saw = second_observed(0);
	printf("second_observed() = %d "
		"(expect 1105 = 1000+105: first's return, 5+100)\n", second_saw);
	assert(second_saw == 1105);

	third_saw = third_observed(0);
	printf("third_observed() = %d "
		"(expect 1205 = 1000+205: second's return, 5+200 -- NOT "
		"1000+1006 leaked from second's nested inner_probe(7) call, "
		"which is the second bug 3a2d32e closed)\n", third_saw);
	assert(third_saw == 1205);

	puts("test_xy_last_dispatch: ok");
	return 0;
}
