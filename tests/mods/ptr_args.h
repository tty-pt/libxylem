/*
 * ptr_args.h — shared hook declarations for mod_ptr_args modules and
 * test_ptr_args harness.
 *
 * Two-role rule:
 *   Caller modules and test binary include this header for XY_HOOK_DECL.
 *   The provider module (mod_ptr_args.c) implements the hooks via XY_LISTENER
 *   and does NOT include this header. No guard macro.
 */
#ifndef PTR_ARGS_H
#define PTR_ARGS_H

#include <ttypt/xy.h>

XY_HOOK_DECL(const char *, ptr_lookup,  const char *, token);
XY_HOOK_DECL(int,          ptr_len,     const char *, s);
XY_HOOK_DECL(int,          ptr_copy,    char *, dst, const char *, src, size_t, n);
XY_HOOK_DECL(const char *, ptr_resolve, int, which);

#endif /* PTR_ARGS_H */
