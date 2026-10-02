all := libxylem
LDLIBS-libxylem := -lqsys -lcorm
libxylem-obj-y := src/libxylem-module.o src/libxylem-dispatch.o src/libxylem-runtime.o
LDLIBS-libxylem-watch := -lxylem -lqsys -lcorm -lpthread

include ../mk/include.mk

objects-set.mk: Makefile

TEST_DIR := tests
TEST_CFLAGS := -Iinclude -pthread
TEST_LDFLAGS := -pthread

${TEST_DIR}:
	@mkdir -p ${TEST_DIR}/mods 2>/dev/null || true

${TEST_DIR}/test_core${EXE}: ${TEST_DIR} ${TEST_DIR}/test_core.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_core.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_errors${EXE}: ${TEST_DIR} ${TEST_DIR}/test_errors.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_errors.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}


${TEST_DIR}/test_macros${EXE}: ${TEST_DIR} ${TEST_DIR}/test_macros.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_macros.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_main${EXE}: ${TEST_DIR} ${TEST_DIR}/test_main.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_main.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/test_deps${EXE}: ${TEST_DIR} ${TEST_DIR}/test_deps.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_deps.c ${CFLAGS} ${TEST_CFLAGS} -Itests \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_objectpath${EXE}: ${TEST_DIR} ${TEST_DIR}/test_objectpath.c \
		${TEST_DIR}/mods/mod_basic.${SO} ${TEST_DIR}/mods/mod_multi.${SO} lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_objectpath.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_auto_init${EXE}: ${TEST_DIR} ${TEST_DIR}/test_auto_init.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_auto_init.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_multi_call${EXE}: ${TEST_DIR} ${TEST_DIR}/test_multi_call.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_multi_call.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_get${EXE}: ${TEST_DIR} ${TEST_DIR}/test_get.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_get.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_xy_last_dispatch${EXE}: ${TEST_DIR} ${TEST_DIR}/test_xy_last_dispatch.c \
		${TEST_DIR}/mods/mod_xylast_inner.${SO} ${TEST_DIR}/mods/mod_xylast_first.${SO} \
		${TEST_DIR}/mods/mod_xylast_second.${SO} ${TEST_DIR}/mods/mod_xylast_third.${SO} \
		lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_xy_last_dispatch.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

# CP-3 gate fixtures: each claims bits out of the region it is loaded into,
# building the (0,0) -> (0,16) -> (0,17) -> (0,64) chain in tests/test_region_identity.c
# Context-ABI gate fixtures. mod_stale_ctx declares a pre-CP-3 144-byte context,
# mod_no_abi a correct one with no handshake at all; both must be refused by
# tests/test_ctx_abi.c without the host overrunning them.
${TEST_DIR}/mods/mod_stale_ctx.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_stale_ctx.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_stale_ctx.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_no_abi.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_no_abi.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_no_abi.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ca_root.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ca_root.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ca_root.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ca_p1.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ca_p1.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ca_p1.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ca_p1child.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ca_p1child.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ca_p1child.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ca_p2.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ca_p2.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ca_p2.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_regionid_a.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_regionid_a.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_regionid_a.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_regionid_b.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_regionid_b.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_regionid_b.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_regionid_c.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_regionid_c.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_regionid_c.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_xylast_inner.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_xylast_inner.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_xylast_inner.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_xylast_first.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_xylast_first.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_xylast_first.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_xylast_second.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_xylast_second.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_xylast_second.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_xylast_third.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_xylast_third.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_xylast_third.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/test_mod.${SO}: ${TEST_DIR} ${TEST_DIR}/test_mod.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_mod.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_basic.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_basic.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_basic.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_multi.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_multi.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_multi.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_void.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_void.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_void.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_bare.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_bare.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_bare.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_dep_provider.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_dep_provider.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_dep_provider.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_dep_consumer.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_dep_consumer.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_dep_consumer.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_auto.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_auto.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_auto.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_adder.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_adder.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_adder.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_multiplier.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_multiplier.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_multiplier.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}






${TEST_DIR}/mods/mod_unload.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_unload.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_unload.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_unload2.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_unload2.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_unload2.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_cascade_child.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_child.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_child.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_cascade_parent.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_parent.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_parent.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_claim_simple.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_claim_simple.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_claim_simple.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_cascade_deep_a.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_a.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_a.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_cascade_deep_b.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_b.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_b.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_cascade_deep_c.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_cascade_deep_c.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_cascade_deep_c.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_region_state.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_region_state.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_region_state.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ptr_args.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ptr_args.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ptr_args.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/mods/mod_ptr_args_caller.${SO}: ${TEST_DIR} ${TEST_DIR}/mods/mod_ptr_args_caller.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/mods/mod_ptr_args_caller.c ${CFLAGS} ${TEST_CFLAGS} \
		-fPIC -shared ${LDFLAGS} -lxylem ${LDLIBS-libxylem}

${TEST_DIR}/test_ptr_args${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ptr_args.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ptr_args.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/bench_dispatch${EXE}: ${TEST_DIR} ${TEST_DIR}/bench_dispatch.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/bench_dispatch.c ${CFLAGS} ${TEST_CFLAGS} -O2 \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_region_state${EXE}: ${TEST_DIR} ${TEST_DIR}/test_region_state.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_region_state.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_region_identity${EXE}: ${TEST_DIR} ${TEST_DIR}/test_region_identity.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_region_identity.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_ctx_abi${EXE}: ${TEST_DIR} ${TEST_DIR}/test_ctx_abi.c \
		${TEST_DIR}/mods/mod_stale_ctx.${SO} ${TEST_DIR}/mods/mod_no_abi.${SO} \
		lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_ctx_abi.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_claim_at${EXE}: ${TEST_DIR} ${TEST_DIR}/test_claim_at.c \
		${TEST_DIR}/mods/mod_ca_root.${SO} ${TEST_DIR}/mods/mod_ca_p1.${SO} \
		${TEST_DIR}/mods/mod_ca_p1child.${SO} ${TEST_DIR}/mods/mod_ca_p2.${SO} \
		lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_claim_at.c ${CFLAGS} ${TEST_CFLAGS} -Itests \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_unload${EXE}: ${TEST_DIR} ${TEST_DIR}/test_unload.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_unload.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}

${TEST_DIR}/test_threads${EXE}: ${TEST_DIR} ${TEST_DIR}/test_threads.c lib/libxylem.${SO}
	${cc} -o $@ ${TEST_DIR}/test_threads.c ${CFLAGS} ${TEST_CFLAGS} \
		${LDFLAGS} -lxylem ${LDLIBS-libxylem} ${TEST_LDFLAGS}











RUST_MANIFEST := rust/Cargo.toml
RUST_TARGET_DIR := /tmp/xy-rust-target

${TEST_DIR}/mods/mod_rust_basic.${SO}: \
		rust/tests/mods/mod_rust_basic/src/lib.rs \
		rust/tests/mods/mod_rust_basic/Cargo.toml \
		rust/xylem/src/lib.rs rust/xylem-macros/src/lib.rs \
		lib/libxylem.${SO}
	cargo build --manifest-path ${RUST_MANIFEST} \
		--package mod-rust-basic \
		--target-dir ${RUST_TARGET_DIR}
	cp ${RUST_TARGET_DIR}/debug/libmod_rust_basic.${SO} $@

${TEST_DIR}/mods/mod_rust_caller.${SO}: \
		rust/tests/mods/mod_rust_caller/src/lib.rs \
		rust/tests/mods/mod_rust_caller/Cargo.toml \
		rust/xylem/src/lib.rs rust/xylem-macros/src/lib.rs \
		lib/libxylem.${SO}
	cargo build --manifest-path ${RUST_MANIFEST} \
		--package mod-rust-caller \
		--target-dir ${RUST_TARGET_DIR}
	cp ${RUST_TARGET_DIR}/debug/libmod_rust_caller.${SO} $@

${TEST_DIR}/mods/mod_rust_definer.${SO}: \
		rust/tests/mods/mod_rust_definer/src/lib.rs \
		rust/tests/mods/mod_rust_definer/Cargo.toml \
		rust/xylem/src/lib.rs rust/xylem-macros/src/lib.rs \
		lib/libxylem.${SO}
	cargo build --manifest-path ${RUST_MANIFEST} \
		--package mod-rust-definer \
		--target-dir ${RUST_TARGET_DIR}
	cp ${RUST_TARGET_DIR}/debug/libmod_rust_definer.${SO} $@


# Rust fixtures are built from cargo sources, not C; they are kept out of
# TEST_MODS so the C gate never depends on a Rust toolchain.  `make rust-fixtures`
# builds them, and `make rust-test` builds and runs the Rust mirror suite.
RUST_FIXTURES := ${TEST_DIR}/mods/mod_rust_basic.${SO} \
	${TEST_DIR}/mods/mod_rust_caller.${SO} \
	${TEST_DIR}/mods/mod_rust_definer.${SO}

TEST_MODS := ${TEST_DIR}/test_mod.${SO} \
	${TEST_DIR}/mods/mod_basic.${SO} \
	${TEST_DIR}/mods/mod_multi.${SO} \
	${TEST_DIR}/mods/mod_void.${SO} \
	${TEST_DIR}/mods/mod_bare.${SO} \
	${TEST_DIR}/mods/mod_dep_provider.${SO} \
	${TEST_DIR}/mods/mod_dep_consumer.${SO} \
	${TEST_DIR}/mods/mod_auto.${SO} \
	${TEST_DIR}/mods/mod_adder.${SO} \
	${TEST_DIR}/mods/mod_multiplier.${SO} \
	${TEST_DIR}/mods/mod_regionid_a.${SO} \
	${TEST_DIR}/mods/mod_stale_ctx.${SO} \
	${TEST_DIR}/mods/mod_no_abi.${SO} \
	${TEST_DIR}/mods/mod_regionid_b.${SO} \
	${TEST_DIR}/mods/mod_regionid_c.${SO} \
	${TEST_DIR}/mods/mod_unload.${SO} \
	${TEST_DIR}/mods/mod_unload2.${SO} \
	${TEST_DIR}/mods/mod_cascade_child.${SO} \
	${TEST_DIR}/mods/mod_cascade_parent.${SO} \
	${TEST_DIR}/mods/mod_claim_simple.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_a.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_b.${SO} \
	${TEST_DIR}/mods/mod_cascade_deep_c.${SO} \
	${TEST_DIR}/mods/mod_region_state.${SO} \
	${TEST_DIR}/mods/mod_ptr_args.${SO} \
	${TEST_DIR}/mods/mod_ptr_args_caller.${SO} \
	${TEST_DIR}/mods/mod_xylast_inner.${SO} \
	${TEST_DIR}/mods/mod_xylast_first.${SO} \
	${TEST_DIR}/mods/mod_xylast_second.${SO} \
	${TEST_DIR}/mods/mod_xylast_third.${SO} \
	${TEST_DIR}/mods/mod_ca_root.${SO} \
	${TEST_DIR}/mods/mod_ca_p1.${SO} \
	${TEST_DIR}/mods/mod_ca_p1child.${SO} \
	${TEST_DIR}/mods/mod_ca_p2.${SO} \


TEST_BINS := ${TEST_DIR}/test_core${EXE} \
	${TEST_DIR}/test_errors${EXE} \
	${TEST_DIR}/test_macros${EXE} \
	${TEST_DIR}/test_main${EXE} \
	${TEST_DIR}/test_deps${EXE} \
	${TEST_DIR}/test_objectpath${EXE} \
	${TEST_DIR}/test_auto_init${EXE} \
	${TEST_DIR}/test_multi_call${EXE} \
	${TEST_DIR}/test_get${EXE} \
	${TEST_DIR}/test_unload${EXE} \
	${TEST_DIR}/test_region_state${EXE} \
	${TEST_DIR}/test_region_identity${EXE} \
	${TEST_DIR}/test_ctx_abi${EXE} \
	${TEST_DIR}/test_claim_at${EXE} \
	${TEST_DIR}/test_ptr_args${EXE} \
	${TEST_DIR}/test_xy_last_dispatch${EXE}
BENCH_BIN := ${TEST_DIR}/bench_dispatch${EXE}
VALIDATION_BINS := ${TEST_BINS} ${BENCH_BIN}

test-build: lib/libxylem.${SO} ${TEST_MODS} ${VALIDATION_BINS}

# Rust-side gate.  Separate from `test` because it needs a cargo toolchain,
# which the C gate must not require.  `cargo test` inside rust/ covers the
# mirror's own unit tests; this target builds the three cdylib fixtures.
rust-fixtures: lib/libxylem.${SO} ${RUST_FIXTURES}

rust-test: rust-fixtures

bench: test-build
	@LD_LIBRARY_PATH=./lib ./${BENCH_BIN}

test: test-build
	@set -e; \
	for bin in ${VALIDATION_BINS}; do \
		name=$$(basename $$bin); \
		echo "Running $${name%${EXE}}..."; \
		LD_LIBRARY_PATH=./lib ./$$bin; \
	done; \
	echo "All validation targets passed!"

clean: test-clean

test-clean:
	rm -f libxylem.o \
		${TEST_DIR}/mods/mod_dep.so \
		${TEST_DIR}/mods/mod_dep_b.so \
		${TEST_DIR}/mods/mod_dep_c.so \
		${TEST_DIR}/mods/mod_dep_provider.o \
		${TEST_MODS} \
		${TEST_BINS} \
		${TEST_DIR}/test_threads${EXE} \
		${TEST_DIR}/bench_dispatch${EXE}

rust-clean:
	rm -rf ${RUST_TARGET_DIR}
